/*
8266-node-B2.ino

Board 2 (ESP8266), node 010.
Suzuki-Kasami token peer. No coordinator: the ESP32 access point only provides
WiFi and logs traffic. Peers: B1 (001), B2 (010), B3 (100).

Asking for the token: at random intervals this board broadcasts
"REQ:<node>:<n>" without knowing what's on the token. Every board records
request numbers (RN) and the order requests reach it. When two boards ask at
nearly the same time, whichever request lands first at the holder goes first.

The token is "TOKEN:<seq>:<to>:<LN>:<waiting>:<commands>"
  LN - last request number served for B1,B2,B3, e.g. "3,0,2"
  waiting - nodes queued for the token, e.g. "010,100" ('-' if none)
  commands - "boards/colors/counts" entries split by '|' ('-' if none)
The holder runs every command with its bit set, clears its bit , may add 
a random valid command, then passes the token to the next waiting node. 
With nobody waiting, it keeps the token.

LEDs: TOKEN on while holding the token, WAITING on any time it isn't.

Pins are NodeMCU-style D labels; ESP8266 D labels do NOT match GPIO numbers.
*/
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

const char* apSsid = "32_AP";
const char* apPass = "32pass88";
const uint16_t PORT = 4210;
WiFiUDP udp;
IPAddress bcast(192, 168, 4, 255);

const int MY_NODE = 0b010; //BOARD 2 = 010
const int NODES = 3;
const int NODE_BITS[NODES] = {0b001, 0b010, 0b100}; //B1,B2,B3
const bool CREATES_TOKEN = true; //one node creates token at boot

const int RED_PIN   = 14; //D5
const int GREEN_PIN = 12; //D6
const int BLUE_PIN  = 13; //D7
const int TOKEN_PIN = 4; //D2 token
const int WAIT_PIN  = 5; //D1 wait

const unsigned long BLINK_ON_MS = 200;
const unsigned long BLINK_OFF_MS = 200;
const unsigned long TOKEN_HOLD_MS = 500; //minimum hold so token is visible
const unsigned long TACK_TIMEOUT = 500; //ms
const int MAX_RETRIES = 9;
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
void finishCS() {
  maybeAddRandomCommand();
  LN[me()] = RN[me()];
  inCS = false;
  Serial.print("Done | commands: ");
}

// *PASSING THE TOKEN*

void sendToken() {
  String msg = String("TOKEN:") + String(tokenSeq) + ":" + bits3(NODE_BITS[passTarget]) + ":" + lnString() + ":" + qString() + ":" + cmdString();
  broadcast(msg);
  Serial.print("Passed token seq=");
  Serial.print(tokenSeq);
  Serial.print(" to ");
  Serial.print(bits3(NODE_BITS[passTarget]));
  if (passAttempt > 0) {
    Serial.print(" (retry ");
    Serial.print(passAttempt);
    Serial.print(")");
  }
  Serial.print(" | LN: ");
  Serial.print(lnString());
  Serial.print(" | waiting: ");
  Serial.print(qString());
  Serial.print(" | commands: ");
  Serial.println(cmdString());
  passing = true;
  passStartedAt = millis();
}

//adds unserved requests to tokens waiting queue in order they reached this
// board, then gives token to first in line, keeps if nobody waits
void passOn() {
  int kept = 0; //drop queue entrties that were already served
  for (int i = 0; i < tokenQLen; i++) {
    int j = tokenQ[i];
    int (j != me() && outstanding(j)) tokenQ[kept++] = j;
  }
  tokenQLen = kept;
  for (int i = 0; i < arrivalLen; i++) {
    int j = arrival[i];
    if (j != me() && outstanding(j) && !inTokenQ(j) && tokenQLen < NODES) tokenQ[tokenQLen++] = j;
  }
  if (tokenQLen == 0) return;

  passTarget = tokenQ[0];
  for (int i = 0; i < tokenQLen - 1; i++) tokenQ[i] = tokenQ[i + 1];
  tokenQLen--;
  tokenSeq = highestSeen + 1; //newer than any token seen, so old copies are recognizable
  highestSeen = tokenSeq;
  passAttempt = 0;
  sendToken();
}

void releaseToken() {
  hasToken = false;
  passing = false;
  inCS = false;
  setTokenLeds(false);
  if (!requesting) scheduleRequest();
}

//TACK timeouts and retries, a node that never answers has request dropped
void updatePass() {
  if (!passing || millis() - passStartedAt < TACK_TIMEOUT) return;
  Serial.print("No TACK from ");
  Serial.print(bits3(NODE_BITS[passTarget]));
  Serial.print(" for seq=");
  Serial.println(tokenSeq);
  if (passAttempt < MAX_RETRIES) {
    passAttempt++;
    sendToken();
    return;
  }
  Serial.print("Giving up on ");
  Serial.print(bits3(NODE_BITS[passTarget]));
  Serial.println(" - dropping its request");
  LN[passTarget] = RN[passTarget]; //it will ask again with new request number
  arrivalRemove(passTarget);
  passing = false;
  passOn(); //next
}

// *MESSAGE HANDLERS*

void sendRequest() {
  RN[me()]++;
  requesting = true;
  requestedAt = millis();
  String msg = String("REQ:") + bits3(MY_NODE) + ":" + String(RN[me()]);
  broadcast(msg);
  Serial.print("Requested token: ");
  Serial.println(msg);
}

//REQ:<node bits>:<request number>
void handleRequest(const String& s) {
  int c1 = s.indexOf(':');
  int c2 = s.indexOf(':', c1 + 1);
  if (c2 < 0) return;
  int j = nodeIndex(parseBits3(s.substring(c1 + 1, c2)));
  unsigned long n = (unsigned long)s.substring(c2 + 1).toInt();
  if (j < 0 || j == me() || n <= RN[j]) return;
  RN[j] = n;
  arrivalAdd(j); //first arrival keeps its place, this is the race
  Serial.print("Request from ");
  Serial.print(bits3(NODE_BITS[j]));
  Serial.print(" n=");
  Serial.println(n);
}

//TACK:<seq>:<from bits>
void handleTack(const String& s) {
  int c1 = s.indexOf(':');
  int c2 = s.indexOf(':', c1 + 1);
  if (c2 < 0) return;
  unsigned long tseq = (unsigned long)s.substring(c1 + 1, c2).toInt();
  int from = parseBits3(s.substring(c2 + 1));
  if (passing && tseq == tokenSeq && from == NODE_BITS[passTarget]) {
    Serial.print("Token accepted by ");
    Serial.print(bits3(from));
    Serial.print(" (seq=");
    Serial.print(tseq);
    Serial.println(")");
    arrivalRemove(passTarget);
    releaseToken();
  }
}

void sendTack(unsigned long tseq) {
  String msg = String("TACK:") + String(tseq) + ":" + bits3(MY_NODE);
  broadcast(msg);
  Serial.print("Sent ");
  Serial.println(msg);
}

//TOKEN:<seq>:<to>:<LN>:<waiting>:<commands>
void handleToken(const String& s) {
  int c1 = s.indexOf(':');
  int c2 = s.indexOf(':', c1 + 1);
  int c3 = s.indexOf(':', c2 + 1);
  int c4 = s.indexOf(':', c3 + 1);
  int c5 = s.indexOf(':', c4 + 1);
  if (c2 < 0 || c3 < 0 || c4 < 0 || c5 < 0) return;
  unsigned long tseq = (unsigned long)s.substring(c1 + 1, c2).toInt();
  int to = parseBits3(s.substring(c2 + 1, c3));
  if (to < 0) return;

  if (to != MY_NODE) {
    if (tseq > highestSeen) highestSeen = tseq;
    arrivalRemove(nodeIndex(to)); //that node is being served
    //a newer token is moving, if it was being passed it got through (TACK lost),
    // otherwise copy this board holds is stale
    if (hasToken && tseq > tokenSeq) {
      if (passing) {
        Serial.print("Saw token seq=");
        Serial.print(tseq);
        Serial.println(" moving on - pass confirmed");
      } else {
        Serial.print("Dropped stale token seq=");
        Serial.print(tokenSeq);
        Serial.print(" (newer seq=");
        Serial.print(tseq);
        Serial.println(" seen)");
        stopBlinking();
      }
      releaseToken();
    }
    return;
  }

  if (tseq == lastAcceptedSeq) { //retry of token already taken, re-ACK only
    sendTack(tseq);
    return;
  }
  if (tseq < highestSeen) return; //older copy, ignore
  highestSeen = tseq;
  lastAcceptedSeq = tseq;
  sendTack(tseq);

  hasToken = true;
  tokenSeq = tseq;
  parseLN(s.substring(c3 + 1, c4));
  parseQ(s.substring(c4 + 1, c5));
  cmdQueue = s.substring(c5 + 1);
  if (cmdQueue == "-") cmdQueue = "";
  requesting = false;
  passing = false;
  heldSince = millis();
  setTokenLeds(true);
  Serial.print("Token received seq=");
  Serial.print(tseq);
  Serial.print(" | LN: ");
  Serial.print(lnString());
  Serial.print(" | waiting: ");
  Serial.print(qString());
  Serial.print(" | commands: ");
  Serial.println(cmdString());
  enterCS();
}

void handleUdp() {
  int packetSize = udp.parsePacket();
  if (packetSize <= 0) return;
  char buf[512];
  int len = udp.read(buf, sizeof(buf) - 1);
  if (len <= 0) return;
  buf[len] = 0;
  String s = String(buf);
  s.trim();
  if (s.startsWith("REQ:")) handleRequest(s);
  else if (s.startsWith("TOKEN:")) handleToken(s);
  else if (s.startsWith("TACK:")) handleTack(s);
}

// *LOOP HELOPERS*

void handleBlink() {
  if (blinkState == IDLE) return;
  unsigned long now = millis();
  if (blinkState == ON) {
    if (now - blinkTimer >= BLINK_ON_MS) {
      digitalWrite(currentPin, LOW);
      blinkTimer = now;
      blinkState = OFF;
    }
  } else if (blinkState == OFF) {
    if (now - blinkTimer >= BLINK_OFF_MS) {
      blinkRemaining--;
      if (blinkRemaining <= 0) {
        jobIndex++;
        if (jobIndex < jobCount) {
          startJob(jobIndex);
        } else {
          blinkState = IDLE;
          Serial.println("Blink sequence complete");
        }
      } else {
        digitalWrite(currentPin, HIGH);
        blinkTimer = now;
        blinkState = ON;
      }
    }
  }
}

//holding token
//finish own commands, then pass it to whoever is waiting
void updateHold() {
  if (!hasToken || passing) return;
  if (inCS) {
    if (blinkState == IDLE) finishCS();
    return;
  }
  if (millis() - heldSince < TOKEN_HOLD_MS) return;
  passOn();
}

//random requests
//board already holding token just takes another turn
void updateRequest() {
  unsigned long now = millis();
  if (hasToken) {
    if (!inCS && !passing && (long)(now - nextRequestAt) >= 0) {
      Serial.println("Turn while already holding the token");
      RN[me()]++;
      enterCS();
      scheduleRequest();
    }
    return;
  }
  if (!requesting && (long)(now - nextRequestAt) >= 0) {
    sendRequest();
  } else if (requesting && now - requestedAt >= REQUEST_RETRY_MS) {
    Serial.println("No token yet - asking again");
    sendRequest();
  }
}

void setup() {
  Serial.begin(9600);
  delay(200);
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(TOKEN_PIN, OUTPUT);
  pinMode(WAIT_PIN, OUTPUT);
  digitalWrite(RED_PIN, LOW);
  digitalWrite(GREEN_PIN, LOW);
  digitalWrite(BLUE_PIN, LOW);
  setTokenLeds(false);
  //ESP32 random() uses the hardware RNG, no seed needed

  WiFi.mode(WIFI_STA);
  WiFi.begin(apSsid, apPass);
  Serial.print("Connecting to AP");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
    if (millis() - start > 15000) {
      Serial.println();
      Serial.print("Failed to connect. WiFi.status() = ");
      Serial.println(WiFi.status());
      break;
    }
  }
  Serial.println();
  Serial.print("Connected. IP: ");
  Serial.println(WiFi.localIP());
  udp.begin(PORT);
  Serial.print("UDP port: ");
  Serial.println(PORT);
  Serial.print("Node: ");
  Serial.println(bits3(MY_NODE));

  if (CREATES_TOKEN) {
    hasToken = true;
    heldSince = millis();
    setTokenLeds(true);
    Serial.println("Token created");
  }
  scheduleRequest();
}

void loop() {
  handleUdp();
  handleBlink();
  updatePass();
  updateHold();
  updateRequest();
  delay(1);
}
