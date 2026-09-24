/*
32-send-3-led.ino

Implementation of broadcasting lighting instructions for 3 LEDs on a receiving board. Between
two ESP32s, this sender board reads a color and count pair (e.g. "R3 G5 B1") and broadcasts the
CMD to the receiver board.

The color/count pairs are stored in a 3-bit representation, with RED being 001, GREEN being 010,
and BLUE being 100. For a specific light to light up, the sender board sends the matching bit
pattern a specific amount of times, based on the user's input.
*/
#include <WiFi.h>
#include <WiFiUdp.h>

const char* apSsid = "32_AP"; //ssid
const char* apPass = "32pass88"; //pw
const uint16_t PORT = 4210; //TDP and UDP port
WiFiUDP udp;
IPAddress bcast(192,168,4,255);

unsigned long seq = 0;
const unsigned long ACK_TIMEOUT = 1500; //in ms
const int MAX_RETRIES = 2;

void setup() {
  Serial.begin(115200);
  delay(200);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid, apPass);
  //ap
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  udp.begin(PORT);
  //udp
  Serial.print("UDP port: ");
  Serial.println(PORT);
  Serial.println("Enter color/count pairs, e.g. R3 G5 B1");
}

String readSerialLine() {
  static String line = "";
  while (Serial.available()) {
    char c = Serial.read();
    // ignore \r and \n as EOL
    if (c == '\r') continue;
    if (c == '\n') {
      String out = line;
      line = "";
      out.trim(); //trimmed
      return out;
    } else {
      line += c;
    }
  }
  return String();
}

//3-bit code: R=001(1), G=010(2), B=100(4)
int colorCode(char c) {
  //able to return the binary literal, or corresponding decimal
  switch (c) {
    case 'R': case 'r': return 0b001; //binary literal of 1
    case 'G': case 'g': return 0b010; //binary literal of 2
    case 'B': case 'b': return 0b100; //binary literal of 4
  }
  return 0;
}

//parses "R3 G5 B1" into comma-separated "code:count" payload
//e.g. "1:3,2:5,4:1"
String buildPayload(const String& line, bool& ok) {
  String payload = ""; //output string
  int i = 0; //read pos
  int n = line.length();
  while (i < n) {
    while (i < n && line[i] == ' ') i++; //skipping spaces
    if (i >= n) break;
    char colorChar = line[i];
    int code = colorCode(colorChar); //storing color char
    i++;
    int numStart = i; //nark where number begin, then advance i forward (line after)
    while (i < n && isDigit(line[i])) i++;
    if (code == 0 || numStart == i) {
      ok = false;
      return "";
    }
    int count = line.substring(numStart, i).toInt(); //getting digits between numStart and i, convert with toInt()
    if (payload.length() > 0) payload += ",";
    payload += String(code) + ":" + String(count);
  }
  ok = payload.length() > 0;
  return payload;
}

bool waitForAck(unsigned long expectedSeq, unsigned long timeoutMs) {
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    int packetSize = udp.parsePacket(); //parse packet
    if (packetSize > 0) {
      char buf[128]; //payload plus null term
      int len = udp.read(buf, sizeof(buf) - 1);
      if (len > 0) {
        buf[len] = 0;
        String reply = String(buf);
        reply.trim(); //trim
        if (reply.startsWith("OK:")) {
          long rseq = reply.substring(3).toInt(); //String::toInt() returns long
          if (rseq == (long)expectedSeq) return true;
        }
      }
    }
    delay(10);
  }
  return false;
}

void loop() {
  String line = readSerialLine();
  if (line.length() == 0) return;

  bool ok = true;
  String payload = buildPayload(line, ok);
  if (!ok) {
    Serial.println("Format: <Letter><Count> pairs separated by spaces, e.g. R3 G5 B1");
    return;
  }

  seq++;
  String msg = String("CMD:") + String(seq) + ":" + payload;

  bool acked = false;
  for (int attempt = 0; attempt <= MAX_RETRIES && !acked; attempt++) {
    //printing connection
    udp.beginPacket(bcast, PORT);
    udp.write((const uint8_t*)msg.c_str(), msg.length());
    udp.endPacket();
    Serial.print("Broadcasted: ");
    Serial.print(msg);
    if (attempt > 0) {
      Serial.print(" (retry ");
      Serial.print(attempt);
      Serial.print(")");
    }
    Serial.println();
    //ACK_TIMEOUT now integrated into this
    if (waitForAck(seq, ACK_TIMEOUT)) {
      acked = true;
      Serial.print("ACK received for seq=");
      Serial.println(seq);
    } else {
      Serial.print("No ACK for seq=");
      Serial.println(seq);
    }
    delay(100);
  }

  if (!acked) {
    Serial.print("Failed to get ACK after ");
    Serial.print(MAX_RETRIES + 1);
    Serial.println(" attempts.");
  }
}
