/*
32-listener.ino

Dining Philosophers, classical (no prevention). Listener ESP32: WiFi access point, table
setup, hunger trigger, command entry, and log. It is not a philosopher.

Philosophers: B1 (0001), B2 (0010), B3 (0100), B4 (1000), seated in that
order. Fork i lies between philosopher i and philosopher i+1.

On boot (and on "init") it picks a new epoch and assigns the initial forks:
  every board holds its left fork (fork 0 -> 0010, 1 -> 0100, 2 -> 1000, 3 -> 0001)
INIT is resent every second until all four boards acknowledge it (IACK).

Serial commands (9600 baud):
  hungry - every thinking philosopher becomes hungry at once
  init - reset the table with a new epoch
  table - print the last known state of every philosopher
  <boards> <colors> <counts...> - LED command for the boards' next meal, e.g. 1010 101 3 2 (B2 and B4: red x3, then blue x2)

Every message between philosophers is broadcast, so the listener logs fork requests and transfers, 
prints the table on every change, and reports an apparent deadlock when every philosopher is hungry 
and nothing has changed for 5 seconds.
*/
#include <WiFi.h>
#include <WiFiUdp.h>

const char* apSsid = "32_AP";
const char* apPass = "32pass88";
const uint16_t PORT = 4210;
WiFiUDP udp;
IPAddress bcast(192, 168, 4, 255);

const int PHILOSOPHERS = 4;
const int NODE_BITS[PHILOSOPHERS] = {0b0001, 0b0010, 0b0100, 0b1000}; //B1,B2,B3,B4 around the table
const char* INIT_HOLDERS = "0010,0100,1000,0001"; //holder of fork 0,1,2,3
const char* INIT_DIRTY = "0";
const unsigned long INIT_RETRY_MS = 1000;
const unsigned long CMD_RETRY_MS = 1000;
const int CMD_MAX_RETRIES = 3;
const unsigned long DEADLOCK_QUIET_MS = 5000;

unsigned long epoch = 0;
int initAckMask = 0;
unsigned long lastInitSent = 0;

//last known state of each philosopher, from STATE messages
char philState[PHILOSOPHERS] = {'?', '?', '?', '?'};
String philForks[PHILOSOPHERS] = {"-", "-", "-", "-"};
String philMeals[PHILOSOPHERS] = {"0", "0", "0", "0"};
unsigned long lastChangeAt = 0;
bool deadlockReported = false;

//LED command waiting for acknowledgements
unsigned long cmdSeq = 0;
String cmdMsg = "";
int cmdTargets = 0;
int cmdAcked = 0;
int cmdAttempts = 0;
unsigned long cmdSentAt = 0;

String bits4(int v) {
  String out = "";
  for (int bit = 3; bit >= 0; bit--) out += ((v >> bit) & 1) ? '1' : '0';
  return out;
}

int parseBits(const String& s, int width) {
  if ((int)s.length() != width) return -1;
  int v = 0;
  for (int i = 0; i < width; i++) {
    if (s[i] != '0' && s[i] != '1') return -1;
    v = (v << 1) | (s[i] - '0');
  }
  return v;
}

int nodeIndex(int bits) {
  for (int i = 0; i < PHILOSOPHERS; i++) {
    if (NODE_BITS[i] == bits) return i;
  }
  return -1;
}

String field(const String& s, int idx) {
  int start = 0;
  for (int i = 0; i < idx; i++) {
    start = s.indexOf(':', start);
    if (start < 0) return "";
    start++;
  }
  int end = s.indexOf(':', start);
  return (end < 0) ? s.substring(start) : s.substring(start, end);
}

void broadcast(const String& msg) {
  udp.beginPacket(bcast, PORT);
  udp.write((const uint8_t*)msg.c_str(), msg.length());
  udp.endPacket();
}

void sendInit() {
  broadcast(String("INIT:") + String(epoch) + ":" + INIT_HOLDERS + ":" + INIT_DIRTY);
  lastInitSent = millis();
}

//new epoch
//every board resets and takes starting forks
void startInit() {
  epoch = random(1, 1000000); //ESP32 random() uses the hardware RNG
  initAckMask = 0;
  cmdMsg = "";
  for (int i = 0; i < PHILOSOPHERS; i++) {
    philState[i] = '?';
    philForks[i] = "-";
    philMeals[i] = "0";
  }
  deadlockReported = false;
  lastChangeAt = millis();
  Serial.print("Setting the table, epoch ");
  Serial.print(epoch);
  Serial.print(" | fork holders (fork 0-3): ");
  Serial.println(INIT_HOLDERS);
  sendInit();
}

void printTable() {
  Serial.print("Table:");
  for (int i = 0; i < PHILOSOPHERS; i++) {
    Serial.print("  ");
    Serial.print(bits4(NODE_BITS[i]));
    Serial.print(" ");
    Serial.print(philState[i] == 'T' ? "thinking" : philState[i] == 'H' ? "HUNGRY" : philState[i] == 'E' ? "EATING" : "?");
    Serial.print(" [");
    Serial.print(philForks[i]);
    Serial.print("] x");
    Serial.print(philMeals[i]);
  }
  Serial.println();
}

void skipCommand(const String& line, const char* reason) {
  Serial.print("Skipped \"");
  Serial.print(line);
  Serial.print("\" - ");
  Serial.println(reason);
}

//<boards 4-bit> <colors 3-bit> <one count (1-7) per color, R -> G -> B>
void queueCommand(const String& line) {
  String tokens[5];
  int n = 0;
  int i = 0;
  int len = line.length();
  while (i < len && n <= 5) {
    while (i < len && line[i] == ' ') i++;
    if (i >= len) break;
    int start = i;
    while (i < len && line[i] != ' ') i++;
    if (n == 5) { n++; break; }
    tokens[n++] = line.substring(start, i);
  }
  if (n < 3 || n > 5) return skipCommand(line, "format: <boards> <colors> <count per color>, e.g. 1010 101 3 2");
  int boards = parseBits(tokens[0], 4);
  if (boards <= 0) return skipCommand(line, "boards must be a 4-bit pattern, e.g. 0001, 1010, 1111");
  int colors = parseBits(tokens[1], 3);
  if (colors <= 0) return skipCommand(line, "colors must be a 3-bit pattern, e.g. 001 (R), 010 (G), 100 (B)");
  int colorsSet = (colors & 1) + ((colors >> 1) & 1) + ((colors >> 2) & 1);
  if (n - 2 != colorsSet) return skipCommand(line, "need one count per color bit set, in R G B order");
  String counts = "";
  for (int t = 2; t < n; t++) {
    if (tokens[t].length() != 1 || tokens[t][0] < '1' || tokens[t][0] > '7') return skipCommand(line, "counts must be 1-7");
    if (counts.length() > 0) counts += ",";
    counts += tokens[t];
  }
  if (cmdMsg.length() > 0) Serial.println("(previous command stopped waiting for ACKs)");
  cmdSeq++;
  cmdMsg = String("CMD:") + String(epoch) + ":" + String(cmdSeq) + ":" + tokens[0] + ":" + tokens[1] + ":" + counts;
  cmdTargets = boards;
  cmdAcked = 0;
  cmdAttempts = 1;
  broadcast(cmdMsg);
  cmdSentAt = millis();
  Serial.print("Sent ");
  Serial.println(cmdMsg);
}

void updateCmd() {
  if (cmdMsg.length() == 0 || millis() - cmdSentAt < CMD_RETRY_MS) return;
  if ((cmdAcked & cmdTargets) == cmdTargets) {
    cmdMsg = "";
    return;
  }
  if (cmdAttempts > CMD_MAX_RETRIES) {
    Serial.print("No CACK from ");
    Serial.println(bits4(cmdTargets & ~cmdAcked));
    cmdMsg = "";
    return;
  }
  cmdAttempts++;
  broadcast(cmdMsg);
  cmdSentAt = millis();
}

void handleLine(const String& line) {
  if (line == "hungry") {
    for (int i = 0; i < 3; i++) broadcast(String("HUNGRY:") + String(epoch)); //sent 3 times in case one is lost
    Serial.println("Everyone is hungry!");
  } else if (line == "init") {
    startInit();
  } else if (line == "table") {
    printTable();
  } else {
    queueCommand(line);
  }
}

String readSerialLine() {
  static String line = "";
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      String out = line;
      line = "";
      out.trim();
      return out;
    }
    line += c;
  }
  return String();
}

void handleUdp() {
  while (true) {
    int packetSize = udp.parsePacket();
    if (packetSize <= 0) return;
    char buf[256];
    int len = udp.read(buf, sizeof(buf) - 1);
    if (len <= 0) continue;
    buf[len] = 0;
    String s = String(buf);
    s.trim();
    if ((unsigned long)field(s, 1).toInt() != epoch) continue; //old table setup

    if (s.startsWith("IACK:")) {
      int i = nodeIndex(parseBits(field(s, 2), 4));
      if (i < 0 || (initAckMask & (1 << i))) continue;
      initAckMask |= (1 << i);
      Serial.print(field(s, 2));
      Serial.println(" is seated");
      if (initAckMask == 0b1111) Serial.println("Table set: all four philosophers seated");
    } else if (s.startsWith("STATE:")) {
      int i = nodeIndex(parseBits(field(s, 2), 4));
      if (i < 0) continue;
      char st = field(s, 3)[0];
      String forks = field(s, 4);
      String meals = field(s, 5);
      if (st == philState[i] && forks == philForks[i] && meals == philMeals[i]) continue; //periodic repeat, nothing new
      philState[i] = st;
      philForks[i] = forks;
      philMeals[i] = meals;
      lastChangeAt = millis();
      deadlockReported = false;
      printTable();
    } else if (s.startsWith("FORK:")) {
      unsigned long seq = (unsigned long)field(s, 3).toInt();
      static unsigned long lastSeq[PHILOSOPHERS] = {0, 0, 0, 0};
      int f = field(s, 2).toInt();
      if (f < 0 || f >= PHILOSOPHERS || seq <= lastSeq[f]) continue; //skip resends
      lastSeq[f] = seq;
      lastChangeAt = millis();
      Serial.print("Fork ");
      Serial.print(f);
      Serial.print(" -> ");
      Serial.print(field(s, 4));
      Serial.println("");
    } else if (s.startsWith("CACK:")) {
      if ((unsigned long)field(s, 2).toInt() != cmdSeq) continue;
      int i = nodeIndex(parseBits(field(s, 3), 4));
      if (i >= 0) cmdAcked |= (1 << i);
    }
  }
}

//every philosopher hungry, and no fork has moved for a while
void updateDeadlock() {
  if (deadlockReported || initAckMask != 0b1111) return;
  for (int i = 0; i < PHILOSOPHERS; i++) {
    if (philState[i] != 'H') return;
  }
  if (millis() - lastChangeAt < DEADLOCK_QUIET_MS) return;
  deadlockReported = true;
  Serial.println("DEADLOCK: every philosopher is hungry, holding one fork and waiting on the other");
}

void setup() {
  Serial.begin(9600);
  delay(200);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid, apPass);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  udp.begin(PORT);
  Serial.println("Commands: hungry | init | table | <boards> <colors> <counts>, e.g. 1010 101 3 2");
  delay(3000); //give philosophers time to join
  startInit();
}

void loop() {
  String line = readSerialLine();
  if (line.length() > 0) handleLine(line);
  handleUdp();
  if (initAckMask != 0b1111 && millis() - lastInitSent >= INIT_RETRY_MS) sendInit();
  updateCmd();
  updateDeadlock();
  delay(1);
}
