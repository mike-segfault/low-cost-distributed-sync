/*
32-sender.ino

Central broadcasting ESP32. Reads "<board> <ColorCount pairs>" from Serial, 
e.g. "1 R3 G5" (board 1: red x3, green x5) or "2 B4" (board 2: blue x4), and 
broadcasts a CMD packet addressed to that board only.

Board addressing (one-hot, same style as colors): Board1=001(1), Board2=010(2), Board3=100(4, reserved)
Color (one-hot): R=001(1), G=010(2), B=100(4)
Count: standard binary, 0-7
*/
#include <WiFi.h>
#include <WiFiUdp.h>

const char* apSsid = "32_AP";
const char* apPass = "32pass88";
const uint16_t PORT = 4210;
WiFiUDP udp;
IPAddress bcast(192,168,4,255);

unsigned long seq = 0;
const unsigned long ACK_TIMEOUT = 1500; //in ms
const int MAX_RETRIES = 2;

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
  Serial.println("Enter: <board> <ColorCount pairs>, e.g. 1 R3 G5");
  Serial.println("Separate multiple board commands with ';', e.g. 1 R3 G5; 2 B4");
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

//maps board number (1,2,3) to one-hot bit code: Board1=001(1), Board2=010(2), Board3=100(4)
int boardCode(int boardNum) {
  if (boardNum < 1 || boardNum > 3) return 0;
  return 1 << (boardNum - 1);
}

//same mapping idea but with R G B
int colorCode(char c) {
  switch (c) {
    case 'R': case 'r': return 1;
    case 'G': case 'g': return 2;
    case 'B': case 'b': return 4;
  }
  return 0;
}

//parsing
//count is capped 0-7 since it's transmitted as standard 3-bit binary value
String buildColorPayload(const String& tail, bool& ok, int& totalBlinks) {
  String payload = "";
  totalBlinks = 0;
  int seenMask = 0; //tracks color bits being used
  int i = 0;
  int n = tail.length();
  while (i < n) {
    while (i < n && tail[i] == ' ') i++;
    if (i >= n) break;
    char colorChar = tail[i];
    int code = colorCode(colorChar);
    i++;
    int numStart = i;
    while (i < n && isDigit(tail[i])) i++;
    if (code == 0 || numStart == i) {
      ok = false;
      return "";
    }
    int count = tail.substring(numStart, i).toInt();
    //count must be 1-7
    if (count < 1 || count > 7 || (seenMask & code)) {
      ok = false;
      return "";
    }
    seenMask |= code; //mark color's bit as used
    totalBlinks += count;
    if (payload.length() > 0) payload += ",";
    payload += String(code) + ":" + String(count);
  }
  ok = payload.length() > 0;
  return payload;
}

bool waitForAck(unsigned long expectedSeq, unsigned long timeoutMs) {
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      char buf[128];
      int len = udp.read(buf, sizeof(buf) - 1);
      if (len > 0) {
        buf[len] = 0;
        String reply = String(buf);
        reply.trim();
        if (reply.startsWith("OK:")) {
          long rseq = reply.substring(3).toInt();
          if (rseq == (long)expectedSeq) return true;
        }
      }
    }
    delay(10);
  }
  return false;
}
//syncing so that 2nd board doesnt start blinking before 1st board is finished
bool waitForDone(unsigned long expectedSeq, unsigned long timeoutMs) {
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      char buf[128];
      int len = udp.read(buf, sizeof(buf) - 1);
      if (len > 0) {
        buf[len] = 0;
        String reply = String(buf);
        reply.trim();
        if (reply.startsWith("DONE:")) {
          long rseq = reply.substring(5).toInt();
          if (rseq == (long)expectedSeq) return true;
        }
      }
    }
    delay(10);
  }
  return false;
}

//parsing for multiple board command if inputted
//malformed comand only aborts self, doesnt stop
//other commands on same line from being processed
void processCommand(const String& cmdStr) {
  int firstSpace = cmdStr.indexOf(' ');
  if (firstSpace <= 0) {
    Serial.print("Skipped \"");
    Serial.print(cmdStr);
        Serial.println("\" - format: <Letter><1-7 count> pairs, each color once, e.g. R3 G5 B1");
    return;
  }
  String boardStr = cmdStr.substring(0, firstSpace);
  String tail = cmdStr.substring(firstSpace + 1);
  boardStr.trim();
  //check board number
  if (!isDigit(boardStr[0])) {
    Serial.print("Skipped \"");
    Serial.print(cmdStr);
    Serial.println("\" - board must be a number, e.g. 1 R3 G5");
    return;
  }
  int boardNum = boardStr.toInt();
  int bCode = boardCode(boardNum);
  if (bCode == 0) {
    Serial.print("Skipped \"");
    Serial.print(cmdStr);
    Serial.println("\" - board must be 1, 2, or 3");
    return;
  }

  bool ok = true;
  int totalBlinks = 0;
  String colorPayload = buildColorPayload(tail, ok, totalBlinks);
  if (!ok) {
    Serial.print("Skipped \"");
    Serial.print(cmdStr);
        Serial.println("\" - format: <Letter><1-7 count> pairs, each color once, e.g. R3 G5 B1");
    return;
  }
  seq++;
  //putting it together
  String msg = String("CMD:") + String(seq) + ":" + String(bCode) + ":" + colorPayload;
  bool acked = false;
  for (int attempt = 0; attempt <= MAX_RETRIES && !acked; attempt++) {
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

    if (waitForAck(seq, ACK_TIMEOUT)) {
      acked = true;
      Serial.print("ACK received for seq =");
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

  unsigned long doneTimeout = (unsigned long)totalBlinks * 400UL + 1000UL; //unsigned long suffix
  if (waitForDone(seq, doneTimeout)) {
    Serial.print("Completed seq=");
    Serial.println(seq);
  } else {
    Serial.print("No completion signal for seq=");
    Serial.print(seq);
    Serial.println(" (timed out - proceeding anyway)");
  }
}

void loop() {
  String line = readSerialLine();
  if (line.length() == 0) return;

  int start = 0;
  int n = line.length();
  while (start < n) {
    int semi = line.indexOf(';', start);
    String cmdStr = (semi == -1) ? line.substring(start) : line.substring(start, semi);
    cmdStr.trim();
    if (cmdStr.length() > 0) {
      processCommand(cmdStr);
    }
    if (semi == -1) break;
    start = semi + 1;
  }
}
