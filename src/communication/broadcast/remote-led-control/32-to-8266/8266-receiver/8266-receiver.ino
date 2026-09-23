/*
8266-receiver.ino

ESP8266MOD implementation of user controlled LED from inputs. Connects
to broadcasting board and waits for user inputted ingteger. Receives
inputted integer from broadcasting board (ESP32 for this) and flashes LED
in accordance with the inputted integer.
*/

#include <ESP8266WiFi.h> //for 8266
#include <WiFiUdp.h>

const char* ssid = "32_AP";
const char* pass = "32pass88";
const uint16_t PORT = 4210;
WiFiUDP udp;

const int LED_PIN = 14; //D5 silkscreen is GPIO14
const unsigned long BLINK_ON_MS = 200;
const unsigned long BLINK_OFF_MS = 200;

enum BlinkState {IDLE, ON, OFF};
BlinkState blinkState = IDLE;
int blinkRemaining = 0;
unsigned long blinkTimer = 0;

void startBlink(int count) {
  if (count <= 0) return;
  blinkRemaining = count;
  digitalWrite(LED_PIN, LOW);
  blinkState = ON;
  digitalWrite(LED_PIN, HIGH);
  blinkTimer = millis();
}

void setup() {
  Serial.begin(9600); //9600 from now on
  delay(200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
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
  Serial.print("UDP port");
  Serial.println(PORT);
}

void handleUdp() {
  int packetSize = udp.parsePacket();
  if (packetSize <= 0) return;
  char buf[256];
  int len = udp.read(buf, sizeof(buf)-1);
  if(len <= 0) return;
  buf[len] = 0;
  String s = String(buf);
  s.trim();
  Serial.print("Received: ");
  Serial.println(s);
  //parse
  if (s.startsWith("CMD:")) {
    int firstColon = s.indexOf(':');
    int secondColon = s.indexOf(':', firstColon + 1);
    int thirdColon = s.indexOf(':', secondColon + 1);
    if (secondColon >= 0 && thirdColon >= 0) {
      String seqStr = s.substring(secondColon + 1, thirdColon);
      String countStr = s.substring(thirdColon + 1);
      int seq = seqStr.toInt();
      int count = countStr.toInt();
      //IP & PORT
      IPAddress remoteIp = udp.remoteIP();
      uint16_t remotePort = udp.remotePort();
      String reply = String("OK:") + String(seq);
      udp.beginPacket(remoteIp, remotePort);
      udp.write((const uint8_t*)reply.c_str(), reply.length());
      udp.endPacket();
      Serial.print("Sent reply: ");
      Serial.println(reply);
      startBlink(count);
    }
  }
}

void handleBlink() {
  if (blinkState == IDLE) return;
  unsigned long now = millis();
  if (blinkState == ON) {
    if (now - blinkTimer >= BLINK_ON_MS) {
      digitalWrite(LED_PIN, LOW);
      blinkTimer = now;
      blinkState = OFF;
    }
  } else if (blinkState == OFF) {
    if (now - blinkTimer >= BLINK_OFF_MS) {
      blinkRemaining--;
      if (blinkRemaining <= 0) {
        blinkState = IDLE;
        digitalWrite(LED_PIN, LOW);
        Serial.println("Blink sequence complete");
      } else {
        digitalWrite(LED_PIN, HIGH);
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
