/*
8266-receiver.ino 

ESP8266MOD implementation for receiving end for connecting to UDP broadcasting 
board. Broadcasting board needs to have SSID set, and at least 8 character password,
which also needs to be replicated on the receiving end.

Port 4210 is used here, which is the TCP and UDP port.
*/
#include <ESP8266WiFi.h> //for 8266
#include <WiFiUdp.h>

const char* ssid = "32_AP";
const char* pass = "32pass88";
const uint16_t PORT = 4210;
WiFiUDP udp;

void setup() {
  Serial.begin(115200);
  delay(200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Serial.print("Connecting to AP");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected. IP: ");
  Serial.println(WiFi.localIP());

  udp.begin(PORT);
  Serial.print("UDP listening on port");
  Serial.println(PORT);
}

void loop() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    IPAddress remoteIp = udp.remoteIP();
    uint16_t remotePort = udp.remotePort();
    char buf[128];
    int len = udp.read(buf, sizeof(buf)-1);
    if (len > 0) {
      buf[len] = 0;
      Serial.print("Received from ");
      Serial.print(remoteIp);
      Serial.print(":");
      Serial.print(remotePort);
      Serial.print(" -> ");
      Serial.println(buf);

      //parse for extracting seq CMD:code:seq:value
      String s = String(buf);
      int firstColon = s.indexOf(':');
      int secondColon = s.indexOf(':', firstColon + 1);
      int thirdColon = s.indexOf(':', secondColon + 1);
      String seq = (secondColon >= 0 && thirdColon >= 0) ? s.substring(secondColon + 1, thirdColon) : "0";

      //OK:seq reply
      String reply = String("OK:") + seq;
      udp.beginPacket(remoteIp, remotePort);
      udp.write((const uint8_t*)reply.c_str(), reply.length());
      udp.endPacket();
      Serial.print("Sent reply: ");
      Serial.println(reply);
    }
  }
  delay(10);
}
