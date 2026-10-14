/*
32-node-B1

Modifiable token. Functions were needed for every aspect of the cmd parsing and
handling of the tasks. Everything is self-contained.
*/
#include <WiFi.h>
#include <WiFiUdp.h>

const char* apSsid = "32_AP";
const char* apPass = "32pass88";
const uint16_t PORT = 4210;
WiFiUDP udp;
IPAddress bcast(192, 168, 4, 255);

const int MY_NODE = 0b001; //BOARD 1 = 001
const int NODES = 3;
const int NODE_BITS[NODES] = {0b001, 0b010, 0b100}; //B1,B2,B3
const bool CREATES_TOKEN = true; //one node creates token at boot

const int RED_PIN   = 14; //D14
const int GREEN_PIN = 27; //D27
const int BLUE_PIN  = 26; //D26
const int TOKEN_PIN = 25; //D25 token
const int WAIT_PIN  = 33; //D33 wait

const unsigned long BLINK_ON_MS = 200;
const unsigned long BLINK_OFF_MS = 200;
const unsigned long TOKEN_HOLD_MS = 500; //minimum hold so token is visible
const unsigned long TACK_TIMEOUT = 1000; //ms
const int MAX_RETRIES = 2;
const unsigned long REQUEST_MIN_MS = 3000; //random wait before asking for token
const unsigned long REQUEST_MAX_MS = 8000;
const unsigned long REQUEST_RETRY_MS = 15000; //ask again if no token
const int ADD_CHANCE = 50; //chance for random command per turn
const int MAX_RANDOM_COUNT = 3; //keeping blink to 3 max in random command
const int MAX_CMDS = 8;

//blink jobs taken from token
struct BlinkJob {int pin; int count;};
const int MAX_JOBS = 24;
BlinkJob jobQueue[MAX_JOBS];
int jobCount = 0;
int jobIndex = 0;

enum BlinkState {IDLE, ON, OFF};
BlinkState blinkState = IDLE;
int blinkRemaining = 0;
unsigned long blinkTimer = 0;
int currentPin = -1;

//requests RN[i] = highest request number heard from node i
unsigned long RN[NODES] = {0, 0, 0};
int arrival[NODES]; //nodes with requests, in order they reached this board
int arrivalLen = 0;
bool requesting = false;
unsigned long requestedAt = 0;
unsigned long nextRequestAt = 0;

//token state
bool hasToken = false;
unsigned long tokenSeq = 0; //seq of token this board holds/passes
unsigned long highestSeen = 0; //newest token seq seen anywhere on ring
unsigned long lastAcceptedSeq = 0; //so a retried token is re-ACKed, not taken twice
bool passing = false;
int passTarget = -1; //node index being given token
int passAttempt = 0; //0 = first send, 1+ = retries
unsigned long passStartedAt = 0;

//3-bit value
String bits3(int v) {
  String out = "";
  for (int bit = 2; bit >= 0; bit--) out += ((v >> bit) & 1) ? '1' : '0';
  return out;
}

//001 for 3, 01 if not three binary characters
int parseBits3(const String& s) {
  if (s.length() != 3) return -1;
  int v = 0;
  for (int i = 0; i < 3; i++) {
    if (s[i] != '0' && s[i] != '1') return -1;
    v = (v << 1) | (s[i] - '0');
  }
  return v;
}

//node bits, index (001 -> 0, 010 -> 1, 100 -> 2 ),  -1 if not a node
int nodeIndex(int bits) {
  for (int i = 0; i < NODES; i++) {
    if (NODE_BITS[i] == bits) return i;
  }
  return -1;
}

//for what its doing
int me() {
  return nodeIndex(MY_NODE);
}

void broadcast(const String& msg) {
  udp.beginPacket(bcast, PORT);
  udp.write((const uint8_t*)msg.c_str(), msg.length());
  udp.endPacket();
}

//3-bit color code GPIO 001=Red | 010=Green | 100=Blue
int pinForCode(int code) {
  switch (code) {
    case 1: return RED_PIN;
    case 2: return GREEN_PIN;
    case 4: return BLUE_PIN;
  }
  return -1;
}

void setTokenLeds(bool holding) {
  digitalWrite(TOKEN_PIN, holding ? HIGH : LOW);
  digitalWrite(WAIT_PIN, holding ? LOW : HIGH);
}

void stopBlinking() {
  if (currentPin >= 0) digitalWrite(currentPin, LOW);
  blinkState = IDLE;
  jobCount = 0;
}

void scheduleRequest() {
  nextRequestAt = millis() + random(REQUEST_MIN_MS, REQUEST_MAX_MS + 1);
}

// *BOOKKEEPING*

bool inArrival(int j) {
  for (int i = 0; i < arrivalLen; i++) {
    if (arrival[i] == j) return true;
  }
  return false;
}

void arrivalAdd(int j) {
  if (!inArrival(j) && arrivalLen < NODES) arrival[arrivalLen++] = j;
}

void arrivalRemove(int j) {
  for (int i = 0; i < arrivalLen; i++) {
    if (arrival[i] == j) {
      for (int k = i; k < arrivalLen - 1; k++) arrival[k] = arrival[k + 1];
      arrivalLen--;
      return;
    }
  }
}

//node j has asjed for token and hasnt been sreved yet
bool outstanding(int j) {
  return RN[j] > LN[j];
}

bool inTokenQ(int j) {
  for (int i = 0; i < tokenQLen; i++) {
    if (tokenQ[i] == j) return true;
  }
  return false;
}

// *TOKEN FIELD FORMATTING*
String lnString() {
  String out = "";
  for (int i = 0; i < NODES; i++) {
    if (i > 0) out += ",";
    out += String(LN[i]);
  }
  return out;
}

String qString() {
  if (tokenQLen == 0) return "-";
  String out = "";
  for (int i = 0; i < tokenQLen; i++) {
    if (i > 0) out += ",";
    out += bits3(NODE_BITS[tokenQ[i]]);
  }
  return out;
}

String cmdString() {
  return cmdQueue.length() > 0 ? cmdQueue : String("-");
}

void parseLN(const String& s) {
  int start = 0;
  for (int i = 0; i < NODES; i++) {
    int comma = s.indexOf(',', start);
    String part = (comma == -1) ? s.substring(start) : s.substring(start, comma);
    LN[i] = (unsigned long)part.toInt();
    if (comma == -1) break;
    start = comma + 1;
  }
}

void parseQ(const String& s) {
  tokenQLen = 0;
  if (s == "-") return;
  int start = 0;
  int n = s.length();
  while (start < n && tokenQLen < NODES) {
    int comma = s.indexOf(',', start);
    String part = (comma == -1) ? s.substring(start) : s.substring(start, comma);
    int j = nodeIndex(parseBits3(part));
    if (j >= 0 && !inTokenQ(j)) tokenQ[tokenQLen++] = j;
    if (comma == -1) break;
    start = comma + 1;
  }
}

int coundCmds(const String& q) {
  if (q.length() == 0) return 0;
  int count 1;
  for (unsigned int i = 0; i < q.length(); i++) {
    if (q[i] == '|') count++;
  }
  return count;
}

// *COMMANDs*

//adds blink jobs for color bits in R -> G -> B order, one cound each from "3,5"
void addJobs(int colorMask, const String& counts) {
  int start = 0;
  for (int bit = 0; bit < 3 && jobCount < MAX_JOBS; bit++) {
    int code = 1 << bit; //001 R, 010 G, 100 B
    if (!(colorMask & code)) continue;
    if (start > (int)counts.length()) break; //ran out of counts
    int comma = counts.indexOf(',', start);
    String countStr = (comma == -1) ? counts.substring(start) : counts.substring(start, comma);
    start = (comma == -1) ? counts.length() + 1 : comma + 1;
    int count = countStr.toInt();
    int pin = pinForCode(code);
    if (pin != -1 && count > 0) {
      jobQueue[jobCount].pin = pin;
      jobQueue[jobCount].count = count;
      jobCount++;
    }
  }
}

//takes this boards cmds off token: queues their blinks and clears this 
// boards bit, cmds with no boards removed
void takeMyCommands() {
  jobCount = 0;
  String kept = "";
  int start = 0;
  int n = cmdQueue.length();
  while (start < n) {
    int bar = cmdQueue.indexOf('|', start);
    String entry = (bar == -1) ? cmdQueue.substring(start) : cmdQueue.substring(start, bar);
    start = (bar == -1) ? n : bar + 1;
    int s1 = entry.indexOf('/');
    int s2 = entry.indexOf('/', s1 + 1);
    if (s1 < 0 || s2 < 0) continue; //malformed, drop
    int boards = parseBits3(entry.substring(0, s1));
    int colors = parseBits3(entry.substring(s1 + 1, s2));
    String counts = entry.substring(s1 + 1);
    if (boards <= 0 || colors <= 0) continue;
    if (boards & MY_NODE) { //bitwise
      Serial.print("Running: ");
      Serial.println(entry);
      addJobs(colors, counts);
      boards &= ~MY_NODE; //this baords part is done
    }
    if (boards != 0) {
      if (kept.length() > 0) kept += "|";
      kept += bits3(boards) + "/" + bits3(colors) + "/" + counts;
    }
  }
  cmdQueue = kept;
}

//random valid cmd
//and boards, any colors, 1-MAX_RANDOM_COUNT blinks per color
void maybeAddRandomCommand() {
  if (random(100) >= ADD_CHANCE) return;
  if (countCmds(cmdQueue) >= MAX_CMDS) return;
  int boards = random(1, 8); //001-111
  int colors = random(1, 8); 
  String counts = "";
  for (int bit = 0; bit < 3; bit++) {
    if (!(colors & (1 << bit))) continue;
    if (counts.length() > 0) counts += ",";
    counts += String(random(1, MAX_RANDOM_COUNT + 1));
  }
  String entry = bits3(boards) + "/" + bits3(colors) + "/" + counts;
  if (cmdQueue.length() > 0) cmdQueue += "|";
  cmdQueue += entry;
  Serial.print("Added random command: ");
  Serial.println(entry);
}

void startJob(int index) {
  String entry = bits3(boards) + "/" + bits3(colors) + "/" + counts;
  if (cmdQueue.length() > 0) cmdQueue += "|";
  cmdQueue += entry;
  Serial.print("Added random command: ");
  Serial.println(entry);
}

// *CRITICAL SECTION*

void enterCS() {
  inCS = true;
  takeMyCommands();
  jobIndex = 0;
  if (jobCount > 0) startJob(0);
}

//this boards cmds done, maybe add one, mark own request served
void finishCS()M{
  maybeAddRandomCommand();
  LN[me()] = RN[me()];
  inCS = false;
  Serial.print("Done | commands: ");
}

// *PASSING THE TOKEN*

void sendToken() {
  
}

void setup() {
  // put your setup code here, to run once:

}

void loop() {
  // put your main code here, to run repeatedly:

}
