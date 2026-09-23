/*
32-receiver.ino

ESP32 implementation of user controlled LED flashes. Broadcasting board 
listens for connection, connects, then waits for user input of a
positive integer. If no connection, waits and retrys. Takes user
input and send off to connected board that has LED wired into it.

This is also utilizing an enum for the stage it's in while it waits
for user input. IDLE for waiting on new input, ON for LED being on, 
OFF for LED being off. This is very important for the later stages of
the project as the representation of processes being in the critical
stage will require its stages to be shown.
*/

//RECEIVER esp32 - blinking LED off user CMD
#include <WiFi.h>
#include <WiFiUdp.h> //UDP library

const char* apSsid = "32_AP"; //ssid
const char* apPass = "32pass88"; //pw
const uint16_t PORT = 4210; //TCP and UDP port
WiFiUDP udp; //setting for UDP
//IPAddress bcast(192,168,4,255);

const int LED_PIN = 18; //put LED digital pin here
const unsigned long BLINK_ON_MS = 200; //200ms on const
const unsigned long BLINK_OFF_MS = 200; //200ms off const

enum BlinkState {IDLE,ON,OFF}; //**USE FOR DINING PHILOSOPHER STATES**
BlinkState blinkState = IDLE; //initial
int blinkRemaining = 0;
unsigned long blinkTimer = 0;

//blinky function
void startBlink(int count) {
  if (count <= 0) return;
  blinkRemaining = count;
  digitalWrite(LED_PIN, LOW);
  blinkState = ON;
  digitalWrite(LED_PIN, HIGH);
  blinkTimer = millis();
}

void setup() {
  Serial.begin(9600);
  delay(200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  //softAP start
  WiFi.mode(WIFI_STA);
  WiFi.begin(apSsid, apPass);
  //access point
  Serial.print("Connecting to AP");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) { //status constant from library
    Serial.print(".");
    delay(300);
    if (millis() - start > 15000) {
      Serial.println();
      Serial.println("Failed to connect to AP - check SSID/password");
      break;
    }
  }
  Serial.println();
  Serial.print("Connected. IP: ");
  Serial.println(WiFi.localIP());
  udp.begin(PORT);
  //UDP port
  Serial.print("UDP port: ");
  Serial.println(PORT);
}
//UDP connection
void handleUdp() {
  int packetSize = udp.parsePacket();
  if (packetSize <= 0) return;
  char buf[256];
  int len = udp.read(buf, sizeof(buf)-1);
  if (len <= 0) return;
  buf[len] = 0; //clear buffer
  String s = String(buf);
  s.trim();
  Serial.print("Received: ");
  Serial.println(s);
  //for CMD, parsing
  if (s.startsWith("CMD:")) {
    int firstColon = s.indexOf(':');
    int secondColon = s.indexOf(':', firstColon + 1);
    int thirdColon = s.indexOf(':', secondColon + 1);
    if (secondColon >= 0 && thirdColon >= 0) {
      String seqStr = s.substring(secondColon + 1, thirdColon);
      String countStr = s.substring(thirdColon + 1);
      int seq = seqStr.toInt();
      int count = countStr.toInt();
      //connecting to correct IP and PORT
      IPAddress remoteIp = udp.remoteIP();
      uint16_t remotePort = udp.remotePort();
      String reply = String("OK:") + String(seq);
      udp.beginPacket(remoteIp, remotePort);
      udp.write((const uint8_t*)reply.c_str(), reply.length());
      udp.endPacket();
      Serial.print("Sent reply: ");
      Serial.println(reply);
      //record count
      startBlink(count);
    }
  }
}

//setting enum blinkState and how many times/long blinks happen
void handleBlink() {
  if (blinkState == IDLE) return;
  unsigned long now = millis();
  //for ON
  if (blinkState == ON) {
    if (now - blinkTimer >= BLINK_ON_MS) {
      digitalWrite(LED_PIN, LOW);
      blinkTimer = now;
      blinkState = OFF; //finish blink
    }
  } else if (blinkState == OFF) { //for OFF
    if (now - blinkTimer >= BLINK_OFF_MS) {
      blinkRemaining--; //decrement blink count cause it blinked
      if (blinkRemaining <= 0) {
        blinkState = IDLE; //waiting for next blink
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
