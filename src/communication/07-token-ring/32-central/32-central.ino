/*
32-central.ino

Centralized distributed synchronization implementation via token system

Central ESP32: access point, token creator, and token ring member (node 000) MONITOR
Ring: central (000) -> B1 (001) -> B2 (010) -> back to central

Reads "<boards> <colors> <count per color>" from Serial, all as 3-bit patterns
(1st bit = rightmost):
  boards: B1=001, B2=010; 011 = both boards
  colors: R=001, G=010, B=100; 101 = red and blue
  counts: one per color bit set, in R -> G -> B order, 1-7 each
e.g. "011 101 3 5". Multiple commands can be separated with ';'

Typed commands wait here until the token comes around to central, then get
added to the token's queue. Only the board holding the token blinks. Every 
token pass is broadcast, so central also logs the token moving between the boards.
*/
#include <WiFi.h>
#include <WiFiUdp.h>

const char* apSsid = "32_AP";
const char* apPass = "32pass88";
const uint16_t PORT = 4210;
WiFiUDP udp;
IPAddress bcast(192, 168, 4, 255);

const int MY_NODE = 0b000; //BOARD 0 for central
//order token travels in, wraps back to start
const int RING[] = {0b000, 0b001, 0b010}; //central, B1, B2
const int RING_SIZE = 3;
const int BOARDS_MASK = 0b011; //boards that exist in ring (B1, B2)

const unsigned long TOKEN_HOLD_MS = 500; //minimum hold so token is visible
const unsigned long TACK_TIMEOUT = 1000; //ms
const int MAX_RETRIES = 2;
const unsigned long RETRY_IDLE_MS = 2000; //nobody took token, wait then retry

const int MAX_PENDING = 8; //typed cmds waiting for the token
const int MAX_TOKEN_ENTRIES = 8; //entries token can carry
String pending[MAX_PENDING];
int pendingCount = 0;

//token state
bool hasToken = false;
unsigned long tokenSeq = 0; //seq of token central holds/passes
unsigned long highestSeen = 0; //newest token seq seen anywhere on ring
unsigned long lastAcceptedSeq = 0; //so a retried token re-ACKed, not taken twice
String tokenQueue = "";
unsigned long heldSince = 0;

enum PassState {PASS_NONE, PASS_AWAIT_TACK, PASS_BACKOFF}; //fallback BACKOFF as safteynet
PassState passState = PASS_NONE;
int passIndex = 0; //ring index currently being offered token
int passAttempt = 0; //0 = first send, 1+ = retries
unsigned long passStartedAt = 0;

//passed on
void releaseToken() {
  hasToken = false;
  passState = PASS_NONE;
}

//3-bit value
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

void broadcast(const String& msg) {
  udp.beginPacket(bcast, PORT);
  udp.write((const uint8_t*)msg.c_str(), msg.length());
  udp.endPacket();
}

String showQueue(const String& q) {
  return q.length() > 0 ? q : String("(empty)");
}

int ringIndex(int node) {
  for (int i = 0; i < RING_SIZE; i++) {
    if (RING[i] == node) return i;
  }
  return 0;
}

void sendTack(unsigned long tseq) {
  String msg = String("TACK:") + String(tseq) + ":" + bits3(MY_NODE);
  broadcast(msg);
  Serial.print("Sent ");
  Serial.println(msg);
}

//broadcasts the token to RING[passIndex] and starts waiting for its TACK
void sendToken() {
  String msg = String("TOKEN:") + String(tokenSeq) + ":" + bits3(RING[passIndex]) + ":" + tokenQueue;
  broadcast(msg);
  Serial.print("Passed token seq=");
  Serial.print(tokenSeq);
  Serial.print(" to ");
  Serial.print(bits3(RING[passIndex]));
  if (passAttempt > 0) {
    Serial.print(" (retry ");
    Serial.print(passAttempt);
    Serial.print(")");
  }
  Serial.print(" | queue: ");
  Serial.println(showQueue(tokenQueue));
  passState = PASS_AWAIT_TACK;
  passStartedAt = millis();
}

//offers token to node after passIndex in ring
void offerToNext() {
  passIndex = (passIndex + 1) % RING_SIZE;
  if (RING[passIndex] == MY_NODE) {
    //went all the way around and nobody took it, keep it, try again later
    Serial.println("No node accepted the token - holding it and retrying");
    passState = PASS_BACKOFF;
    passStartedAt = millis();
    return;
  }
  tokenSeq++; //every offer gets new seq, so older copy can always be told apart
  if (tokenSeq > highestSeen) highestSeen = tokenSeq;
  passAttempt = 0;
  sendToken();
}

void startPass() {
  passIndex = ringIndex(MY_NODE);
  offerToNext();
}

//TACK timeouts, retries, skipping nodes that don't answer
void updatePass() {
  if (passState == PASS_NONE) return;
  unsigned long now = millis();
  if (passState == PASS_BACKOFF) {
    if (now - passStartedAt >= RETRY_IDLE_MS) startPass();
    return;
  }
  if (now - passStartedAt < TACK_TIMEOUT) return;
  Serial.print("No TACK from ");
  Serial.print(bits3(RING[passIndex]));
  Serial.print(" for seq=");
  Serial.println(tokenSeq);
  if (passAttempt < MAX_RETRIES) {
    passAttempt++;
    sendToken();
  } else {
    Serial.print("Skipping ");
    Serial.println(bits3(RING[passIndex]));
    offerToNext();
  }
}

//TACK:<seq>:<from bits> handing off to new
void handleTack(const String& s) {
  int c1 = s.indexOf(':');
  int c2 = s.indexOf(':', c1 + 1);
  if (c2 < 0) return;
  unsigned long tseq = (unsigned long)s.substring(c1 + 1, c2).toInt();
  int from = parseBits3(s.substring(c2 + 1));
  if (passState == PASS_AWAIT_TACK && tseq == tokenSeq && from == RING[passIndex]) {
    Serial.print("Token accepted by ");
    Serial.print(bits3(from));
    Serial.print(" (seq=");
    Serial.print(tseq);
    Serial.println(")");
    releaseToken();
  }
}

int queueLength(const String& q) {
  if (q.length() == 0) return 0;
  int count = 1;
  for (unsigned int i = 0; i < q.length(); i++) {
    if (q[i] == '|') count++;
  }
  return count;
}

//moves typed cmds into token while central holds it
void injectPending() {
  int room = MAX_TOKEN_ENTRIES - queueLength(tokenQueue);
  int added = 0;
  while (pendingCount > 0 && room > 0) {
    if (tokenQueue.length() > 0) tokenQueue += "|";
    tokenQueue += pending[0];
    for (int i = 0; i < pendingCount - 1; i++) pending[i] = pending[i + 1];
    pendingCount--;
    room--;
    added++;
  }
  if (added > 0) {
    Serial.print("Added ");
    Serial.print(added);
    Serial.print(" command(s) to token | queue: ");
    Serial.println(tokenQueue);
  }
}

//TOKEN:<seq>:<to bits>:<queue>
void handleToken(const String& s) {
  int c1 = s.indexOf(':');
  int c2 = s.indexOf(':', c1 + 1);
  int c3 = s.indexOf(':', c2 + 1);
  if (c2 < 0 || c3 < 0) return;
  unsigned long tseq = (unsigned long)s.substring(c1 + 1, c2).toInt();
  int to = parseBits3(s.substring(c2 + 1, c3));
  String queue = s.substring(c3 + 1);
  if (to < 0) return;

  if (to != MY_NODE) {
    //board-to-board pass, overheard on broadcast
    Serial.print("Token seq=");
    Serial.print(tseq);
    Serial.print(" -> ");
    Serial.print(bits3(to));
    Serial.print(" | queue: ");
    Serial.println(showQueue(queue));
    if (tseq > highestSeen) highestSeen = tseq;
    if (hasToken && tseq > tokenSeq) {
      if (passState == PASS_AWAIT_TACK) {
        Serial.print("Saw token seq=");
        Serial.print(tseq);
        Serial.println(" moving on - pass confirmed");
      } else {
        Serial.print("Dropped stale token seq=");
        Serial.print(tokenSeq);
        Serial.print(" (newer seq=");
        Serial.print(tseq);
        Serial.println(" seen)");
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
  tokenQueue = queue;
  heldSince = millis();
  passState = PASS_NONE;
  Serial.print("Token back at central seq=");
  Serial.print(tseq);
  Serial.print(" | queue: ");
  Serial.println(showQueue(queue));
  injectPending();
}

void handleUdp() {
  while (true) {
    int packetSize = udp.parsePacket();
    if (packetSize <= 0) return;
    char buf[512];
    int len = udp.read(buf, sizeof(buf) - 1);
    if (len <= 0) continue;
    buf[len] = 0;
    String s = String(buf);
    s.trim();
    if (s.startsWith("TOKEN:")) handleToken(s);
    else if (s.startsWith("TACK:")) handleTack(s);
  }
}

//central has nothing to blink
//add waiting cmds, hold briefly, pass it on
void updateHold() {
  if (!hasToken || passState != PASS_NONE) return;
  if (millis() - heldSince < TOKEN_HOLD_MS) return;
  injectPending(); //catches cmds typed while central was holding
  startPass();
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

//for malformed cmd, prints valid input template
void skipCommand(const String& cmdStr, const char* reason) {
  Serial.print("Skipped \"");
  Serial.print(cmdStr);
  Serial.print("\" - ");
  Serial.println(reason);
}

//board, color, count, count... NOW "boards/colors/counts" entry waiting for the token
//malformed cmd only skips itself, rest of line still runs
void queueCommand(const String& cmdStr) {
  String tokens[5]; //boards, color, up to 3 counts
  int tokenCount = 0;
  int i = 0;
  int n = cmdStr.length();
  while (i < n) {
    while (i < n && cmdStr[i] == ' ') i++;
    if (i >= n) break;
    int start = i;
    while (i < n && cmdStr[i] != ' ') i++;
    if (tokenCount == 5) {
      tokenCount++; //too many tokens
      break;
    }
    tokens[tokenCount++] = cmdStr.substring(start, i);
  }

  if (tokenCount < 3 || tokenCount > 5) { //too low, too many token
    skipCommand(cmdStr, "format: <boards> <colors> <count per color>, e.g. 011 101 3 5");
    return;
  }
  int boardMask = parseBits3(tokens[0]);
  if (boardMask <= 0 || (boardMask & ~BOARDS_MASK)) { //invalid board
    skipCommand(cmdStr, "boards must be 001, 010, or 011");
    return;
  }
  int colorMask = parseBits3(tokens[1]);
  if (colorMask <= 0) { //invalid color
    skipCommand(cmdStr, "colors must be a 3-bit pattern, e.g. 001 (R), 010 (G), 100 (B), 101 (R+B)");
    return;
  }
  int colorsSet = (colorMask & 1) + ((colorMask >> 1) & 1) + ((colorMask >> 2) & 1);
  if (tokenCount - 2 != colorsSet) { //need count
    skipCommand(cmdStr, "need one count per color bit set, in R G B order");
    return;
  }

  String counts = "";
  for (int t = 2; t < tokenCount; t++) {
    if (tokens[t].length() != 1 || tokens[t][0] < '1' || tokens[t][0] > '7') {
      skipCommand(cmdStr, "counts must be 1-7");
      return;
    }
    if (counts.length() > 0) counts += ",";
    counts += tokens[t];
  }

  if (pendingCount >= MAX_PENDING) {
    skipCommand(cmdStr, "queue full");
    return;
  }
  String entry = bits3(boardMask) + "/" + bits3(colorMask) + "/" + counts;
  pending[pendingCount++] = entry;
  Serial.print("Queued ");
  Serial.print(entry);
  Serial.println(" - joins the token at central");
}

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
  Serial.println("Enter: <boards> <colors> <count per color>, e.g. 011 101 3 5");
  Serial.println("Separate multiple commands with ';', e.g. 001 001 3; 010 110 2 4");

  //central creates one token
  hasToken = true;
  tokenSeq = 0;
  tokenQueue = "";
  heldSince = millis();
  Serial.println("Token created at central");
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
      if (cmdStr.length() > 0) queueCommand(cmdStr);
      if (semi == -1) break;
      start = semi + 1;
    }
  }
  handleUdp();
  updatePass();
  updateHold();
}
