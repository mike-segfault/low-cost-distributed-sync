//SENDER esp32 - asks for user input and sends CMD to RECEIVER
#include <WiFi.h>
#include <WiFiUdp.h> //UDP library

const char* apSsid = "32_AP"; //ssid
const char* apPass = "32pass88"; //pw
const uint16_t PORT = 4210; //TCP and UDP port
WiFiUDP udp; //setting for UDP
IPAddress bcast(192,168,4,255);

unsigned long seq = 0;
const unsigned long ACK_TIMEOUT = 1500; //in ms
const int MAX_RETRIES = 2;

void setup() {
  Serial.begin(115200);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.softAP(apSsid, apPass); //softAP for sender
  //access point
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  udp.begin(PORT);
  //UDP port
  Serial.print("UDP port: ");
  Serial.println(PORT);
  Serial.println("Type a positive integer and press Enter to send blink count.");
}

String readSerialLine() {
  static String line = "";
  while(Serial.available()) {
    char c = Serial.read();
    //ignores \r and uses \n as EOL
    if (c == '\r') continue;
    if (c == '\n') {
      String out = line;
      line = "";
      out.trim(); //trimmed of whitespace
      return out;
    } else {
      line += c;
    }
  }
  return String();
}

bool waitForAck(unsigned long expectedSeq, unsigned long timeoutMs) {
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    int packetSize = udp.parsePacket(); //parsing through sent packet
    if (packetSize > 0) {
      char buf [128]; //UDP payload plus a null term
      int len = udp.read(buf, sizeof(buf)-1);
      if (len > 0) {
        buf[len] = 0;
        String reply = String(buf);
        reply.trim(); //trimming whitespace
        if (reply.startsWith("OK:")) {
          long rseq = reply.substring(3).toInt(); //String::toInt() returns a long
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
  int count = line.toInt();
  if (count <= 0) {
    Serial.println("Enter a positive integer.");
    return;
  }
  seq++;
  String msg = String("CMD:1:") + String(seq) + ":" + String(count);

  bool acked = false;
  for (int attempt = 0; attempt <= MAX_RETRIES && !acked; attempt++) {
    //formatting and printing connection
    udp.beginPacket(bcast, PORT);
    udp.write((const uint8_t*)msg.c_str(), msg.length());
    udp.endPacket();
    Serial.print("Broadcasted: "); 
    Serial.print(msg);
    if (attempt > 0) Serial.print(" (retry ");
    if (attempt > 0) { 
      Serial.print(attempt); 
      Serial.print(")"); 
    }
    Serial.println();

    unsigned long waitMs = ACK_TIMEOUT;
    //checking for ACK
    if (waitForAck(seq, waitMs)) {
      acked = true;
      Serial.print("ACK received for seq=");
      Serial.println(seq);
    } else {
      Serial.print("No ACK for seq=");
      Serial.print(seq);
      Serial.println();
    }
    delay(100); //gap before retry
  }

  if (!acked) {
    Serial.print("Failed to get ACK after ");
    Serial.print(MAX_RETRIES + 1);
    Serial.println(" attempts.");
  }
}
