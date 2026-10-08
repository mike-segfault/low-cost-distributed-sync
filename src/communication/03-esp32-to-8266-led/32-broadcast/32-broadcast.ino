/*
32-broadcast.ino

Implementation of user controlled LED flashes. Broadcasting board 
listens for connection, connects, then waits for user input of a
positive integer. If no connection, waits and retrys. Takes user
input and send off to connected board that has LED wired into it.

Input is positive integer.
*/
#include <WiFi.h>
#include <WiFiUdp.h> //UDP library

const char* apSsid = "32_AP"; //ssid
const char* apPass = "32pass88"; //pw
const uint16_t PORT = 4210; //TCP and UDP port
WiFiUDP udp; //setting for UDP

unsigned long seq = 0;

void setup() {
  Serial.begin(9600); //9600 moving forward
  delay(200);

  //softAP start
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid, apPass);
  IPAddress apIP = WiFi.softAPIP(); //192.168.4.1
  Serial.print("AP IP: ");
  Serial.println(apIP);

  //start UDP on PORT
  udp.begin(PORT);
  Serial.print("UDP listening on port ");
  Serial.println(PORT);
  Serial.println("Waiting a few seconds for clients to connect...");
  delay(3000);
}

void loop() {
  seq++;
  //instruction: "CMD:code:seq:value"
  String msg = String("CMD:") + "1" + ":" + String(seq) + ":" + String(3.14, 2);

  //broadcast on AP subnet 192.168.4.255
  IPAddress bcast(192,168,4,255);
  udp.beginPacket(bcast, PORT);
  udp.write((const uint8_t*)msg.c_str(), msg.length());
  udp.endPacket();

  Serial.print("Broadcasted: ");
  Serial.println(msg);

  //1500ms wait for reply
  bool gotReply = false;
  unsigned long start = millis();
  while (millis() - start < 1500) {
    int packetSize = udp.parsePacket();
    if (packetSize) {
      char buf[128];
      int len = udp.read(buf, sizeof(buf)-1);
      if (len > 0) {
        buf[len] = 0;
        Serial.print("Reply from ");
        Serial.print(udp.remoteIP());
        Serial.print(":");
        Serial.print(udp.remotePort());
        Serial.print(" -> ");
        Serial.println(buf);
        gotReply = true;
      }
      break;
    }
    delay(10);
  }
  if(!gotReply) Serial.println("No reply received");
  delay(2000); //every 2s
}
