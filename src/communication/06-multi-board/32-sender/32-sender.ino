/*
32-sender.ino

Central broadcasting ESP32. Reads "<boards> <colors> <count per color>" 
from Serial all as 3-bit patterns (1st bit = rightmost):
  boards: B1=001, B2=010, B3=100 (reserved); 011 = boards 1 and 2
  colors: R=001,  G=010,  B=100; 101 = red and blue
  counts: one per color bit set, in R -> G -> B order, 1-7 each
e.g. "011 101 3 5" = boards 1 and 2 each blink red x3, then blue x5.
Multiple commands can be separated with ';'.

A command for several boards goes out as ONE broadcast as each board ACKs
and reports DONE on its own. A board runs its commands in order, while
different boards blink at the same time. Nothing blocks, so new commands
can be typed while others are still running.
*/
#include <WiFi.h>
#include <WiFiUdp.h>

const char* apSsid = "32_AP";
const char* apPass = "32pass88";
const uint16_t PORT = 4210;
WiFiUDP udp;
IPAddress bcast(192, 168, 4, 255);

unsigned long seq = 0;
const unsigned long ACK_TIMEOUT = 1500;  //in ms
const int MAX_RETRIES = 2;
const int MAX_PENDING = 8;
const int MAX_ACTIVE = 3;

struct Cmd {
  int boardMask;  //011 would be b1 and b2
  int colorMask;  //101 for red and blue, goes R G B
  String counts;
  int totalBlinks;
};

enum CmdState { AWAIT_ACK,
                RUNNING };  //for token later probably

struct ActiveCmd {
  bool inUse = false;
  Cmd cmd;
  unsigned long seq = 0;
  String msg;
  int ackedMask = 0;
  int doneMask = 0;
  CmdState state = AWAIT_ACK;
  int attempt = 0;
  unsigned long startedAt = 0;
  unsigned long waitMs = 0;
};

Cmd pending[MAX_PENDING];
int pendingCount = 0;
ActiveCmd active[MAX_ACTIVE];
int busyMask = 0;  //boards running cmd

void setup() {
  Serial.begin(9600);
  delay(200);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid, apPass);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  udp.begin(PORT);
  Serial.print("UDP port: ");
  Serial.println(PORT);
  //binary representation
  Serial.println("Enter: <boards> <colors> <count per color>, e.g. 011 101 3 5");
  Serial.println("Separate multiple commands with ';', e.g. 001 001 3; 010 110 2 4");
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
    } else {
      line += c;
    }
  }
  return String();
}

String bits3(int v) {
  String out = "";
  for (int bit = 2; bit >= 0; bit--) out += ((v >> bit) & 1) ? '1' : '0';
  return out;
}

//011 for 3, -1 if not three binary characters
int parseBits3(const String& s) {
  if (s.length() != 3) return -1;
  int v = 0;
  for (int i = 0; i < 3; i++) {
    if (s[i] != '0' && s[i] != '1') return -1;
    v = (v << 1) | (s[i] - '0');
  }
  return v;
}

//add to queue, false if full
bool enqueue(const Cmd& c) {
  if (pendingCount >= MAX_PENDING) return false;
  pending[pendingCount++] = c;
  return true;
}

void transmit(ActiveCmd& a) {
  udp.beginPacket(bcast, PORT);
  udp.write((const uint8_t*)a.msg.c_str(), a.msg.length());
  udp.endPacket();
  Serial.print("Broadcasted: ");
  Serial.print(a.msg);
  if (a.attempt > 0) {
    Serial.print(" (retry ");
    Serial.print(a.attempt);
    Serial.print(")");
  }
  Serial.println();
  a.state = AWAIT_ACK;
  a.startedAt = millis();
  a.waitMs = ACK_TIMEOUT;
}

//pending[index] -> active slot a, sends as broadcast
void dispatch(int index, ActiveCmd& a) {
  a.inUse = true;
  a.cmd = pending[index];
  for (int i = index; i < pendingCount - 1; i++) pending[i] = pending[i + 1];
  pendingCount--;
  seq++;  //assigned at send time so log order matches send order
  a.seq = seq;
  a.msg = String("CMD:") + String(seq) + ":" + bits3(a.cmd.boardMask) + ":" + bits3(a.cmd.colorMask) + ":" + a.cmd.counts;
  a.ackedMask = 0;
  a.doneMask = 0;
  a.attempt = 0;
  busyMask |= a.cmd.boardMask;
  transmit(a);
}

//check for every board ACKing, switch to waiting for completion
void checkAllAcked(ActiveCmd& a) {
  if (!a.inUse || a.state != AWAIT_ACK) return;
  if ((a.ackedMask & a.cmd.boardMask) != a.cmd.boardMask) return;
  a.state = RUNNING;
  a.startedAt = millis();
  //function for total blinks times 400ms plus 1000ms netowrk margin
  a.waitMs = (unsigned long)a.cmd.totalBlinks * 400UL + 1000UL;  //UL = unsigned long
}

//marks boards finished for cmd and frees for next cmd
//cmd itself freed once all boards are finished
void releaseBoards(ActiveCmd& a, int boards) {
  a.doneMask |= boards;  //bitwise inclusive OR
  busyMask &= ~boards;   //flip bits with ~ for identifying board to release
  if ((a.doneMask & a.cmd.boardMask) == a.cmd.boardMask) a.inUse = false;
}

//"OK:<seq>:<board bits>" and "DONE:<seq>:<board bits>"
//one board
void handleReply(const String& reply) {
  bool isAck = reply.startsWith("OK:");
  bool isDone = reply.startsWith("DONE:");
  if (!isAck && !isDone) return;
  int firstColon = reply.indexOf(':');
  int secondColon = reply.indexOf(':', firstColon + 1);
  if (secondColon < 0) return;
  unsigned long rseq = (unsigned long)reply.substring(firstColon + 1, secondColon).toInt();
  int board = parseBits3(reply.substring(secondColon + 1));
  if (board <= 0) return;
  for (int i = 0; i < MAX_ACTIVE; i++) {
    ActiveCmd& a = active[i];
    if (!a.inUse || a.seq != rseq) continue;
    if (!(a.cmd.boardMask & board) || (a.doneMask & board)) return;  //not target, or already finished
    if (isAck) {
      if (a.ackedMask & board) return;  //dup OK: from retry
      a.ackedMask |= board;
      Serial.print("ACK received for seq=");
      Serial.print(rseq);
      Serial.print(" from board ");
      Serial.println(bits3(board));
      checkAllAcked(a);
    } else {
      a.ackedMask |= board;  ///DONe proves receipt for OK: being lost
      Serial.print("Completed seq=");
      Serial.print(rseq);
      Serial.print(" on board ");
      Serial.println(bits3(board));
      releaseBoards(a, board);
      checkAllAcked(a);
    }
    return;
  }
}

//ACK retries and completion timeouts, per command
//EXPLAIN TO BENNET 1
void checkTimeouts() {
  unsigned long now = millis();
  for (int i = 0; i < MAX_ACTIVE; i++) {
    ActiveCmd& a = active[i];
    if (!a.inUse || now - a.startedAt < a.waitMs) continue;
    if (a.state == AWAIT_ACK) {
      int missing = a.cmd.boardMask & ~a.ackedMask;
      Serial.print("No ACK for seq=");
      Serial.print(a.seq);
      Serial.print(" from board(s) ");
      Serial.println(bits3(missing));
      if (a.attempt < MAX_RETRIES) {
        a.attempt++;
        transmit(a);  //boards re-ACK, no restart
      } else {
        Serial.print("Failed to get ACK after ");
        Serial.print(MAX_RETRIES + 1);
        Serial.println(" attempts.");
        releaseBoards(a, missing);  //give up for non-answering boards
        checkAllAcked(a);           //waiting on boards that answered
      }
    } else {  //RUNNING
      int missing = a.cmd.boardMask & ~a.doneMask;
      Serial.print("No completion signal for seq=");
      Serial.print(a.seq);
      Serial.print(" from board(s) ");
      Serial.print(bits3(missing));
      Serial.println(" (timed out - proceeding anyway)");
      releaseBoards(a, missing);
    }
  }
}

//open
int freeSlot() {
  for (int i = 0; i < MAX_ACTIVE; i++) {
    if (!active[i].inUse) return i;
  }
  return -1;
}

/*
sends pending cmd to free boards, cmd that has to wait also holds
back later cmds for same boards, each board keeps order
*/
//EXPLAIN TO BENNETT 2
void dispatchPending() {
  int blocked = busyMask;
  int i = 0;
  while (i < pendingCount) {
    int boards = pending[i].boardMask;
    if ((boards & blocked) == 0) {
      int slot = freeSlot();
      if (slot < 0) return;
      dispatch(i, active[slot]);  //removes pending[i], i statys same
    } else {
      i++
    }
    blocked |= boards;
  }
}

//called every loop(): read ALL replies, handle timeouts, send what's ready
void updateScheduler() {
  while (true) {
    int packetSize = udp.parsePacket();
    if (packetSize <= 0) break;
    char buf[128];
    int len = udp.read(buf, sizeof(buf) - 1);
    if (len <= 0) continue;
    buf[len] = 0;
    String reply = String(buf);
    reply.trim();
    handleReply(reply);
  }
  checkTimeouts();
  dispatchPending();
}

//for malformed cmd, prints valid input template
void skipCommand(const String& cmdStr, const char* reason) {
  Serial.print("Skipped \"");
  Serial.print(cmdStr);
  Serial.print("\" - ");
  Serial.println(reason);
}

//board, color, count, count... for cmd
//malformed command only skips itself, rest of line still runs
void queueCommand(const String& cmdStr) {
  String tokens[5];  //boards, color, up to 3 counts
  int tokenCount = 0;
  int i = 0;
  int n = cmdStr.length();
  while (i < n) {
    while (i < n && cmdStr[i] == ' ') i++;
    if (i >= n) break;
    int start = i;
    while (i < n && cmdStr[i] != ' ') i++;
    if (tokenCount == 5) {
      tokenCount++;  //too many tokens
      break;
    }
    tokens[tokenCount++] = cmdStr.substring(start, i);
  }

  //for invalid inputs
  if (tokenCount < 3 || tokenCount > 5) {  //too low, too many token
    skipCommand(cmdStr, "format: <boards> <colors> <count per color>, e.g. 011 101 3 5");
    return;
  }
  int boardMask = parseBits3(tokens[0]);
  if (boardMask <= 0) {  //invalid board
    skipCommand(cmdStr, "boards must be a 3-bit pattern, e.g. 001, 010, 011");
    return;
  }
  int colorMask = parseBits3(tokens[1]);
  if (colorMask <= 0) {  //invalid color
    skipCommand(cmdStr, "colors must be a 3-bit pattern, e.g. 001 (R), 010 (G), 100 (B), 101 (R+B)");
    return;
  }
  int colorsSet = (colorMask & 1) + ((colorMask >> 1) & 1) + ((colorMask >> 2) & 1);
  if (tokenCount - 2 != colorsSet) {  //need count
    skipCommand(cmdStr, "need one count per color bit set, in R G B order");
    return;
  }

  Cmd c;
  c.boardMask = boardMask;
  c.colorMask = colorMask;
  c.counts = "";
  c.totalBlinks = 0;
  for (int t = 2; t < tokenCount; t++) {
    if (tokens[t].length() != 1 || tokens[t][0] < '1' || tokens[t][0] > '7') {
      skipCommand(cmdStr, "counts must be 1-7");
      return;
    }
    int count = tokens[t].toInt();
    if (c.counts.length() > 0) c.counts += ",";
    c.counts += String(count);
    c.totalBlinks += count;
  }
  if (!enqueue(c)) skipCommand(cmdStr, "queue full");
}

void loop() {
  String line = readSerialLine();
  if (line.length() > 0) {
    int start = 0;
    int n = line.length();
    while (start < n) {
      int semi = line.indexOf(';', start);
      String cmdStr = (semi == -1) ? line.substring(start) : line.substring(start, semi);
      cmdStr.trim();
      if (cmdStr.length() > 0) {
        queueCommand(cmdStr);
      }
      if (semi == -1) break;
      start = semi + 1;
    }
  }
  updateScheduler(); //sends, handles OK:/DONE:, retries and never blocks
}
