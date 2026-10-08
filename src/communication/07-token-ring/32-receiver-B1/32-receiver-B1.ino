/*
32-receiver-B1.ino

Board 1 (ESP32), node 001
Token ring member: central (000) -> B1 (001) -> B2 (010) -> back to central

Only the board holding the token blinks (mutual exclusion). The token carries
the whole command queue: "boards/colors/counts" entries separated by '|',
e.g. "011/101/3,5|010/001/2". When this board gets the token it takes every
entry with its bit set, clears its bit (entries with no boards left are
removed), blinks them in order, then passes the token on.

LEDs: TOKEN on while holding the token, WAITING on any time it isn't.
*/
#include <WiFi.h>
#include <WiFiUdp.h>

const char* apSsid = "32_AP";
const char* apPass = "32pass88";
const uint16_t PORT = 4210;
WiFiUDP udp;
IPAddress bcast(192, 168, 4, 255);

const int MY_NODE = 0b001; //BOARD 1 = 001
//order token travels in, then wraps back to start
const int RING[] = { 0b000, 0b001, 0b010 }; //central, B1, B2
const int RING_SIZE = 3;

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
const unsigned long RETRY_IDLE_MS = 2000; //nobody took token, wait then retry

//blink jobs taken from token
struct BlinkJob { int pin; int count; };
const int MAX_JOBS = 24;
BlinkJob jobQueue[MAX_JOBS];
int jobCount = 0;
int jobIndex = 0;

enum BlinkState { IDLE, ON, OFF };
BlinkState blinkState = IDLE;
int blinkRemaining = 0;
unsigned long blinkTimer = 0;
int currentPin = -1;

//token state
bool hasToken = false;
unsigned long tokenSeq = 0; //seq of token this board holds/passes
unsigned long highestSeen = 0; //newest token seq seen anywhere on ring
unsigned long lastAcceptedSeq = 0; //so a retried token is re-ACKed, not taken twice
String tokenQueue = "";
unsigned long heldSince = 0;

enum PassState {PASS_NONE, PASS_AWAIT_TACK, PASS_BACKOFF};
PassState passState = PASS_NONE;
int passIndex = 0; //ring index currently being offered token
int passAttempt = 0; //0 = first send, 1+ = retries
unsigned long passStartedAt = 0;

//maps 3-bit color code to GPIO pin: 001=Red | 010=Green | 100=Blue
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

//passed on, or newer token replaced it
void releaseToken() {
  hasToken = false;
  passState = PASS_NONE;
  setTokenLeds(false);
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

//broadcasts token to RING[passIndex] and starts waiting for TACK
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

//TACK timeouts, retries, and skipping nodes that don't answer
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

//TACK:<seq>:<from bits> the node we offered the token to has it now
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

//adds blink jobs for color bits in R -> G -> B order, one count each from "3,5"
void addJobs(int colorMask, const String& counts) {
  int start = 0;
  for (int bit = 0; bit < 3 && jobCount < MAX_JOBS; bit++) {
    int code = 1 << bit; //001 = R, 010 = G, 100 = B
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

//takes board's entries out of token: queues their blinks and clears board's bit
//entries with no boards left are removed from token
void takeMyEntries() {
  jobCount = 0;
  String kept = "";
  int start = 0;
  int n = tokenQueue.length();
  while (start < n) {
    int bar = tokenQueue.indexOf('|', start);
    String entry = (bar == -1) ? tokenQueue.substring(start) : tokenQueue.substring(start, bar);
    start = (bar == -1) ? n : bar + 1;
    int s1 = entry.indexOf('/');
    int s2 = entry.indexOf('/', s1 + 1);
    if (s1 < 0 || s2 < 0) continue; //malformed entry, drop it
    int boards = parseBits3(entry.substring(0, s1));
    int colors = parseBits3(entry.substring(s1 + 1, s2));
    String counts = entry.substring(s2 + 1);
    if (boards <= 0 || colors <= 0) continue;
    if (boards & MY_NODE) {
      Serial.print("Running: ");
      Serial.println(entry);
      addJobs(colors, counts);
      boards &= ~MY_NODE; //this boards part is done
    }
    if (boards != 0) {
      if (kept.length() > 0) kept += "|";
      kept += bits3(boards) + "/" + bits3(colors) + "/" + counts;
    }
  }
  tokenQueue = kept;
}

void startJob(int index) {
  currentPin = jobQueue[index].pin;
  blinkRemaining = jobQueue[index].count;
  blinkState = ON;
  digitalWrite(currentPin, HIGH);
  blinkTimer = millis();
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
    if (tseq > highestSeen) highestSeen = tseq;
    //newer token is moving
    //if ours was being passed it got through (TACK lost), otherwise copy board holds is stale
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
  tokenQueue = queue;
  heldSince = millis();
  passState = PASS_NONE;
  setTokenLeds(true);
  Serial.print("Token received seq=");
  Serial.print(tseq);
  Serial.print(" | queue: ");
  Serial.println(showQueue(queue));

  takeMyEntries();
  jobIndex = 0;
  if (jobCount > 0) startJob(0);
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
  if (s.startsWith("TOKEN:")) handleToken(s);
  else if (s.startsWith("TACK:")) handleTack(s);
}

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

//pass token on once board's blinks are done and minimum hold is up
void updateHold() {
  if (!hasToken || passState != PASS_NONE) return;
  if (blinkState != IDLE) return;
  if (millis() - heldSince < TOKEN_HOLD_MS) return;
  startPass();
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
  setTokenLeds(false); //waiting until token arrives

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
}

void loop() {
  handleUdp();
  handleBlink();
  updatePass();
  updateHold();
  delay(1);
}
