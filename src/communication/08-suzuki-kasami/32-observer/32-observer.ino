/*
32-observer.ino

ESP32 access point for the Suzuki-Kasami stage. It only provides the WiFi
network and logs traffic; it takes no part in the algorithm, so the system has
no coordinator. Peers: B1 (001), B2 (010), B3 (100).

Every message is broadcast, so this board hears all of it:
  REQ:<node>:<n> - a board asking for the token
  TOKEN:<seq>:<to>:<LN>:<waiting>:<commands> - the token moving between boards
  TACK:<seq>:<node> - a board confirming it got the token
*/
#include <WiFi.h>
#include <WiFiUdp.h>

const char* apSsid = "32_AP";
const char* apPass = "32pass88";
const uint16_t PORT = 4210;
WiFiUDP udp;

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
  Serial.println("Observing REQ / TOKEN / TACK traffic");
}

//returns field number idx (0-based) of a ':' separated message
String field(const String& s, int idx) {
  int start = 0;
  for (int i = 0; i < idx; i++) {
    start = s.indexOf(':', start);
    if (start < 0) return "";
    start++;
  }
  int end = s.indexOf(':', start);
  return (end < 0) ? s.substring(start) : s.substring(start, end);
}

void loop() {
  while (true) {
    int packetSize = udp.parsePacket();
    if (packetSize <= 0) break;
    char buf[512];
    int len = udp.read(buf, sizeof(buf) - 1);
    if (len <= 0) continue;
    buf[len] = 0;
    String s = String(buf);
    s.trim();

    if (s.startsWith("REQ:")) {
      Serial.print("Request from ");
      Serial.print(field(s, 1));
      Serial.print(" n=");
      Serial.println(field(s, 2));
    } else if (s.startsWith("TOKEN:")) {
      Serial.print("Token seq=");
      Serial.print(field(s, 1));
      Serial.print(" -> ");
      Serial.print(field(s, 2));
      Serial.print(" | LN: ");
      Serial.print(field(s, 3));
      Serial.print(" | waiting: ");
      Serial.print(field(s, 4));
      Serial.print(" | commands: ");
      Serial.println(field(s, 5));
    } else if (s.startsWith("TACK:")) {
      Serial.print("Token accepted by ");
      Serial.print(field(s, 2));
      Serial.print(" (seq=");
      Serial.print(field(s, 1));
      Serial.println(")");
    }
  }
  delay(1);
}
