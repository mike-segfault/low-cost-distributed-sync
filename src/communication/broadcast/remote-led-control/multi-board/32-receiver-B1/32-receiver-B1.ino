/*
32-receiver-B1.ino

Board 1 (ESP32). Only acts on CMD packets addressed to board code 001 (1);
all other board codes are silently ignored (no ACK sent, no blink triggered),
so the ESP8266 (board 2) and this board never both respond to the same command.
*/
#include <WiFi.h>
#include <WiFiUdp.h>

const char* apSsid = "32_AP";
const char* apPass = "32pass88";
const uint16_t PORT = 4210;
WiFiUDP udp;

const int MY_BOARD_CODE = 0b001; //Board 1 = 001, can pass through binary prefix string of 0b001

const int RED_PIN   = 14; //D14
const int GREEN_PIN = 27; //D27
const int BLUE_PIN  = 26; //D26
const unsigned long BLINK_ON_MS = 200;
const unsigned long BLINK_OFF_MS = 200;

//maps 3-bit color code to GPIO pin: 001=Red | 010=Green | 100=Blue
int pinForCode(int code) {
  switch (code) {
    case 1: return RED_PIN;
    case 2: return GREEN_PIN;
    case 4: return BLUE_PIN;
  }
  return -1;
}

struct BlinkJob { int pin; int count; };
const int MAX_JOBS = 3;
BlinkJob jobQueue[MAX_JOBS];
int jobCount = 0;
int jobIndex = 0;

enum BlinkState { IDLE, ON, OFF };
BlinkState blinkState = IDLE;
int blinkRemaining = 0;
unsigned long blinkTimer = 0;
int currentPin = -1;

IPAddress lastRemoteIp;
uint16_t lastRemotePort = 0;
int lastSeq = -1;

void sendDone(int seqToAck) {
  String doneMsg = String("DONE:") + String(seqToAck);
  udp.beginPacket(lastRemoteIp, lastRemotePort);
  udp.write((const uint8_t*)doneMsg.c_str(), doneMsg.length());
  udp.endPacket();
  Serial.print("Sent completion: ");
  Serial.println(doneMsg);
}

void startJob(int index) {
  currentPin = jobQueue[index].pin;
  blinkRemaining = jobQueue[index].count;
  digitalWrite(currentPin, LOW);
  if (blinkRemaining <= 0) return;
  blinkState = ON;
  digitalWrite(currentPin, HIGH);
  blinkTimer = millis();
}

void startSequence() {
  jobIndex = 0;
  if (jobCount > 0) startJob(jobIndex);
}

void setup() {
  Serial.begin(9600);
  delay(200);
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  digitalWrite(RED_PIN, LOW);
  digitalWrite(GREEN_PIN, LOW);
  digitalWrite(BLUE_PIN, LOW);

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
  Serial.print("Board code: ");
  Serial.println(MY_BOARD_CODE);
}

//parses "1:3,2:5,4:1" into up to MAX_JOBS BlinkJob entries
void parsePayload(const String& payload) {
  jobCount = 0;
  int start = 0;
  int n = payload.length();
  while (start < n && jobCount < MAX_JOBS) {
    int comma = payload.indexOf(',', start);
    String entry = (comma == -1) ? payload.substring(start) : payload.substring(start, comma);
    int colon = entry.indexOf(':');
    if (colon > 0) {
      int code = entry.substring(0, colon).toInt();
      int count = entry.substring(colon + 1).toInt();
      int pin = pinForCode(code);
      if (pin != -1 && count > 0) {
        jobQueue[jobCount].pin = pin;
        jobQueue[jobCount].count = count;
        jobCount++;
      }
    }
    if (comma == -1) break;
    start = comma + 1;
  }
}

void handleUdp() {
  int packetSize = udp.parsePacket();
  if (packetSize <= 0) return;
  char buf[256];
  int len = udp.read(buf, sizeof(buf) - 1);
  if (len <= 0) return;
  buf[len] = 0;
  String s = String(buf);
  s.trim();

  if (!s.startsWith("CMD:")) return;

  int firstColon = s.indexOf(':');
  int secondColon = s.indexOf(':', firstColon + 1);
  int thirdColon = s.indexOf(':', secondColon + 1);
  if (secondColon < 0 || thirdColon < 0) return;

  String seqStr = s.substring(firstColon + 1, secondColon);
  String boardStr = s.substring(secondColon + 1, thirdColon);
  String payload = s.substring(thirdColon + 1);
  int seq = seqStr.toInt();
  int boardCode = boardStr.toInt();
  //if not for this board, no ACK, no blink
  if (boardCode != MY_BOARD_CODE) {
    Serial.print("Ignored seq=");
    Serial.print(seq);
    Serial.print(" (addressed to board code ");
    Serial.print(boardCode);
    Serial.println(")");
    return;
  }

  Serial.print("Received (for b1 32): ");
  Serial.println(s);

  IPAddress remoteIp = udp.remoteIP();
  uint16_t remotePort = udp.remotePort();
  String reply = String("OK:") + String(seq);
  udp.beginPacket(remoteIp, remotePort);
  udp.write((const uint8_t*)reply.c_str(), reply.length());
  udp.endPacket();
  Serial.print("Sent reply: ");
  Serial.println(reply);

  lastRemoteIp = remoteIp;
  lastRemotePort = remotePort;
  lastSeq = seq;

  parsePayload(payload);
  startSequence();
  if (jobCount == 0) {
    sendDone(seq);
  }
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
          sendDone(lastSeq);
        }
      } else {
        digitalWrite(currentPin, HIGH);
        blinkTimer = now;
        blinkState = ON;
      }
    }
  }
}

void loop() {
  handleUdp();
  handleBlink();
  delay(1);
}
