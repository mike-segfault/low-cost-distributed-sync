/*
8266-node-B3.ino
*/
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

const char *apSsid = "32_AP";
const char *apPass = "32pass88";
const uint16_t PORT = 4210;
WiFiUDP udp;
IPAddress bcast(192, 168, 4, 255);

const int MY_NODE = 0b0100; // B3 = 0100
const int PHILOSOPHERS = 4;
const int NODE_BITS[PHILOSOPHERS] = {0b0001, 0b0010, 0b0100, 0b1000}; // B1,B2,B3,B4 around the table

const int RED_PIN = 14;   // D5
const int GREEN_PIN = 12; // D6
const int BLUE_PIN = 13;  // D7
const int TOKEN_PIN = 4;  // D2 eating
const int WAIT_PIN = 5;   // D1 hungry

const unsigned long BLINK_ON_MS = 200;
const unsigned long BLINK_OFF_MS = 200;
const unsigned long FORK_RETRY_MS = 500;    // resend a fork until its FACK arrives
const unsigned long REQ_RETRY_MS = 1500;    // ask again for a fork that hasn't come
const unsigned long STATE_REPORT_MS = 2000; // repeat STATE so a lost one is replaced
const unsigned long HELLO_MS = 1000;        // say HELLO while waiting for a table
const bool RANDOM_HUNGER = true;            // get hungry on a random timer while thinking
const unsigned long THINK_MIN_MS = 3000;
const unsigned long THINK_MAX_MS = 8000;
const int MAX_RANDOM_BLINKS = 3; // blinks per color in a random meal
const int MAX_QUEUED_CMDS = 4;
const unsigned long BACKOFF_MIN_MS = 200; // ALL_AT_ONCE: random wait after giving a fork up
const unsigned long BACKOFF_MAX_MS = 1500;

// philosopher state
enum PhilState
{
  THINKING,
  HUNGRY,
  EATING
};
PhilState state = THINKING;
bool initialized = false; // waits for the listener's INIT
unsigned long epoch = 0;  // which table setup we belong to
int me = 0;               // this board's seat (0-3)
unsigned long meals = 0;
unsigned long nextHungerAt = 0;
unsigned long lastStateAt = 0;
unsigned long lastHelloAt = 0;
unsigned long backoffUntil = 0; // ALL_AT_ONCE backoff

// which deadlock prevention method is in use; set by the listener's INIT
enum PreventionMethod
{
  ALL_AT_ONCE,
  ROSENKRANTZ,
  HAVENDER
};
PreventionMethod method = HAVENDER;

// the two forks: slot 0 = left fork, slot 1 = right fork
int forkId[2];            // fork number (0-3)
int neighbor[2];          // seat of the board sharing that fork
bool holding[2];          // this board holds the fork
bool inTransit[2];        // fork sent, waiting for FACK
unsigned long forkSeq[2]; // transfer number of the latest copy of each fork
unsigned long transitAt[2];
bool pendingReq[2]; // neighbor asked while this board kept the fork
bool requested[2];  // this board asked for the fork
unsigned long requestedAt[2];

// LED commands from the listener, run at the next meal
struct QueuedCmd
{
  int colors;
  String counts;
};
QueuedCmd cmdQueue[MAX_QUEUED_CMDS];
int cmdCount = 0;
unsigned long lastCmdSeq = 0;

// blink jobs for the current meal
struct BlinkJob
{
  int pin;
  int count;
};
const int MAX_JOBS = 3;
BlinkJob jobQueue[MAX_JOBS];
int jobCount = 0;
int jobIndex = 0;
enum BlinkState
{
  IDLE,
  ON,
  OFF
};
BlinkState blinkState = IDLE;
int blinkRemaining = 0;
unsigned long blinkTimer = 0;
int currentPin = -1;

// *HELPERS*

// 4-bit value for nodes/boards
String bits4(int v)
{
  String out = "";
  for (int bit = 3; bit >= 0; bit--)
    out += ((v >> bit) & 1) ? '1' : '0';
  return out;
}

// 0011 for 3, -1 if not four binary characters
int parseBits4(const String &s)
{
  if (s.length() != 4)
    return -1;
  int v = 0;
  for (int i = 0; i < 4; i++)
  {
    if (s[i] != '0' && s[i] != '1')
      return -1;
    v = (v << 1) | (s[i] - '0');
  }
  return v;
}

// 011 for 3, -1 if not three binary characters (colors)
int parseBits3(const String &s)
{
  if (s.length() != 3)
    return -1;
  int v = 0;
  for (int i = 0; i < 3; i++)
  {
    if (s[i] != '0' && s[i] != '1')
      return -1;
    v = (v << 1) | (s[i] - '0');
  }
  return v;
}

int nodeIndex(int bits)
{
  for (int i = 0; i < PHILOSOPHERS; i++)
  {
    if (NODE_BITS[i] == bits)
      return i;
  }
  return -1;
}

// field number idx (0-based) of a ':'-separated message
String field(const String &s, int idx)
{
  int start = 0;
  for (int i = 0; i < idx; i++)
  {
    start = s.indexOf(':', start);
    if (start < 0)
      return "";
    start++;
  }
  int end = s.indexOf(':', start);
  return (end < 0) ? s.substring(start) : s.substring(start, end);
}

// which slot (0 = left, 1 = right) holds fork f, -1 if not one of ours
int slotForFork(int f)
{
  if (f == forkId[0])
    return 0;
  if (f == forkId[1])
    return 1;
  return -1;
}

void broadcast(const String &msg)
{
  udp.beginPacket(bcast, PORT);
  udp.write((const uint8_t *)msg.c_str(), msg.length());
  udp.endPacket();
}

String ep()
{
  return String(epoch);
}

char stateChar()
{
  return state == THINKING ? 'T' : (state == HUNGRY ? 'H' : 'E');
}

String forksHeld()
{
  String out = "";
  for (int k = 0; k < 2; k++)
  {
    if (!holding[k])
      continue;
    if (out.length() > 0)
      out += ",";
    out += String(forkId[k]);
  }
  return out.length() > 0 ? out : String("-");
}

// tells the listener (and anyone else) what this philosopher is doing
void broadcastState()
{
  lastStateAt = millis();
  broadcast(String("STATE:") + ep() + ":" + bits4(MY_NODE) + ":" + String(stateChar()) + ":" + forksHeld() + ":" + String(meals));
}

void setLeds()
{
  digitalWrite(WAIT_PIN, state == HUNGRY ? HIGH : LOW);
  digitalWrite(TOKEN_PIN, state == EATING ? HIGH : LOW);
}

// 3-bit color code GPIO 001=Red | 010=Green | 100=Blue
int pinForCode(int code)
{
  switch (code)
  {
  case 1:
    return RED_PIN;
  case 2:
    return GREEN_PIN;
  case 4:
    return BLUE_PIN;
  }
  return -1;
}

// *FORKS*

void sendFork(int k)
{
  broadcast(String("FORK:") + ep() + ":" + String(forkId[k]) + ":" + String(forkSeq[k]) + ":" +
            bits4(NODE_BITS[neighbor[k]]) + ":" + "-");
  transitAt[k] = millis();
}

// hands fork k to the neighbor sharing it; resent until the FACK arrives
void giveFork(int k)
{
  holding[k] = false;
  pendingReq[k] = false;
  requested[k] = false; // if still hungry, this board asks for it again later
  if (state == HUNGRY && method == ALL_AT_ONCE)
    backoffUntil = millis() + random(BACKOFF_MIN_MS, BACKOFF_MAX_MS + 1);
  forkSeq[k]++;
  inTransit[k] = true;
  sendFork(k);
  Serial.print("Gave fork ");
  Serial.print(forkId[k]);
  Serial.print(" to ");
  Serial.println(bits4(NODE_BITS[neighbor[k]]));
  broadcastState();
}

// asks the neighbor for fork k, again every REQ_RETRY_MS until it arrives
void askFor(int k)
{
  unsigned long now = millis();
  if (requested[k] && now - requestedAt[k] < REQ_RETRY_MS)
    return;
  broadcast(String("REQ:") + ep() + ":" + String(forkId[k]) + ":" + bits4(MY_NODE));
  if (!requested[k])
  {
    Serial.print("Asked ");
    Serial.print(bits4(NODE_BITS[neighbor[k]]));
    Serial.print(" for fork ");
    Serial.println(forkId[k]);
  }
  requested[k] = true;
  requestedAt[k] = now;
}

// resends forks whose FACK hasn't arrived (forks are never dropped)
void updateForks()
{
  for (int k = 0; k < 2; k++)
  {
    if (inTransit[k] && millis() - transitAt[k] >= FORK_RETRY_MS)
      sendFork(k);
  }
}

// *DEADLOCK PREVENTIONJ RULES*

void startMeal(); // defined below, with the critical section

const char *methodName()
{
  switch (method)
  {
  case ALL_AT_ONCE:
    return "all at once (no hold and wait)";
  case ROSENKRANTZ:
    return "Rosenkrantz priority preemption (allows preemption)";
  default:
    return "Havender resource ordering (no circular wait)";
  }
}

// the fork this board asks for first:
// Havender = its lower-numbered fork; Rosenkrantz = its left fork, as in the classical problem
int firstSlot()
{
  if (method == HAVENDER)
    return (forkId[0] < forkId[1]) ? 0 : 1;
  return 0;
}

// asked for fork k: a thinking board always gives it and an eating board never does;
// what a hungry (blocked) board does depends on the prevention method
bool shouldGive(int k)
{
  if (state == EATING)
    return false;
  if (state == THINKING)
    return true;
  switch (method)
  {
  case ALL_AT_ONCE:
    // no hold and wait: a board holds nothing until it holds everything
    return true;
  case ROSENKRANTZ:
    // preempted only by a higher-priority requester (lower board ID = higher priority)
    return neighbor[k] < me;
  default:
  {
    // HAVENDER: keep the lower fork, and keep the higher one only once the lower is held
    int first = firstSlot();
    return !(k == first || holding[first]);
  }
  }
}

// hungry: collect the forks, then eat
void updateHunger()
{
  if (state != HUNGRY)
    return;
  if (holding[0] && holding[1])
  {
    startMeal();
    return;
  }
  if (method == ALL_AT_ONCE)
  {
    // ask for everything missing, but wait out the backoff after giving a fork up
    if ((long)(millis() - backoffUntil) < 0)
      return;
    for (int k = 0; k < 2; k++)
    {
      if (!holding[k])
        askFor(k);
    }
    return;
  }
  // HAVENDER and ROSENKRANTZ: first fork, then second
  int first = firstSlot();
  askFor(holding[first] ? 1 - first : first);
}

// *EATING (CS)

void addJobs(int colorMask, const String &counts)
{
  jobCount = 0;
  int start = 0;
  for (int bit = 0; bit < 3 && jobCount < MAX_JOBS; bit++)
  {
    int code = 1 << bit; // 001 = R, 010 = G, 100 = B
    if (!(colorMask & code))
      continue;
    if (start > (int)counts.length())
      break;
    int comma = counts.indexOf(',', start);
    String countStr = (comma == -1) ? counts.substring(start) : counts.substring(start, comma);
    start = (comma == -1) ? counts.length() + 1 : comma + 1;
    int count = countStr.toInt();
    int pin = pinForCode(code);
    if (pin != -1 && count > 0)
    {
      jobQueue[jobCount].pin = pin;
      jobQueue[jobCount].count = count;
      jobCount++;
    }
  }
}

void startJob(int index)
{
  currentPin = jobQueue[index].pin;
  blinkRemaining = jobQueue[index].count;
  blinkState = ON;
  digitalWrite(currentPin, HIGH);
  blinkTimer = millis();
}

void stopBlinking()
{
  if (currentPin >= 0)
    digitalWrite(currentPin, LOW);
  blinkState = IDLE;
  jobCount = 0;
}

void becomeHungry()
{
  state = HUNGRY;
  requested[0] = requested[1] = false;
  setLeds();
  Serial.println("Hungry");
  broadcastState();
}

// holding both forks: run the next queued command, or a random LED sequence
void startMeal()
{
  state = EATING;
  setLeds();
  if (cmdCount > 0)
  {
    addJobs(cmdQueue[0].colors, cmdQueue[0].counts);
    for (int i = 0; i < cmdCount - 1; i++)
      cmdQueue[i] = cmdQueue[i + 1];
    cmdCount--;
    Serial.print("Eating (command) with forks ");
  }
  else
  {
    int colors = random(1, 8);
    String counts = "";
    for (int bit = 0; bit < 3; bit++)
    {
      if (!(colors & (1 << bit)))
        continue;
      if (counts.length() > 0)
        counts += ",";
      counts += String(random(1, MAX_RANDOM_BLINKS + 1));
    }
    addJobs(colors, counts);
    Serial.print("Eating (random) with forks ");
  }
  Serial.println(forksHeld());
  broadcastState();
  jobIndex = 0;
  if (jobCount > 0)
    startJob(0);
}

void scheduleHunger()
{
  if (RANDOM_HUNGER)
    nextHungerAt = millis() + random(THINK_MIN_MS, THINK_MAX_MS + 1);
}

// meal over: back to thinking, then hand over any forks neighbors asked for
void finishMeal()
{
  meals++;
  state = THINKING;
  setLeds();
  Serial.print("Done eating (meal ");
  Serial.print(meals);
  Serial.println(")");
  broadcastState();
  for (int k = 0; k < 2; k++)
  {
    if (pendingReq[k] && holding[k] && !inTransit[k])
      giveFork(k);
  }
  scheduleHunger();
}

void handleBlink()
{
  if (blinkState == IDLE)
    return;
  unsigned long now = millis();
  if (blinkState == ON)
  {
    if (now - blinkTimer >= BLINK_ON_MS)
    {
      digitalWrite(currentPin, LOW);
      blinkTimer = now;
      blinkState = OFF;
    }
  }
  else if (blinkState == OFF)
  {
    if (now - blinkTimer >= BLINK_OFF_MS)
    {
      blinkRemaining--;
      if (blinkRemaining > 0)
      {
        digitalWrite(currentPin, HIGH);
        blinkTimer = now;
        blinkState = ON;
      }
      else if (++jobIndex < jobCount)
      {
        startJob(jobIndex);
      }
      else
      {
        blinkState = IDLE;
      }
    }
  }
}

void updateMeal()
{
  if (state == EATING && blinkState == IDLE)
    finishMeal();
}

void updateThinking()
{
  if (RANDOM_HUNGER && initialized && state == THINKING && (long)(millis() - nextHungerAt) >= 0)
    becomeHungry();
}

// *MSG HANDLERS*

// INIT:<epoch>:<holder of fork 0>,<fork 1>,<fork 2>,<fork 3>:<method A/R/H>
void handleInit(const String &s)
{
  unsigned long e = (unsigned long)field(s, 1).toInt();
  if (initialized && e == epoch)
  { // repeat of the current setup
    broadcast(String("IACK:") + ep() + ":" + bits4(MY_NODE));
    return;
  }
  String holders = field(s, 2);
  String m = field(s, 3);
  method = (m == "A") ? ALL_AT_ONCE : (m == "R") ? ROSENKRANTZ
                                                 : HAVENDER;
  epoch = e;
  backoffUntil = 0;
  initialized = true;
  stopBlinking();
  state = THINKING;
  meals = 0;
  cmdCount = 0;
  lastCmdSeq = 0;
  for (int k = 0; k < 2; k++)
  {
    String h = holders.substring(forkId[k] * 5, forkId[k] * 5 + 4); // 4 bits + comma per fork
    holding[k] = (parseBits4(h) == MY_NODE);
    inTransit[k] = false;
    forkSeq[k] = 0;
    pendingReq[k] = false;
    requested[k] = false;
  }
  setLeds();
  Serial.print("Table initialized (epoch ");
  Serial.print(epoch);
  Serial.print("), holding forks ");
  Serial.print(forksHeld());
  Serial.print(" | method: ");
  Serial.println(methodName());
  broadcast(String("IACK:") + ep() + ":" + bits4(MY_NODE));
  broadcastState();
  scheduleHunger();
}

// REQ:<epoch>:<fork>:<from>
void handleReq(const String &s)
{
  int k = slotForFork(field(s, 2).toInt());
  if (k < 0 || nodeIndex(parseBits4(field(s, 3))) != neighbor[k])
    return;
  if (!holding[k] || inTransit[k])
    return; // not ours to give right now
  if (shouldGive(k))
  {
    giveFork(k);
  }
  else if (!pendingReq[k])
  {
    pendingReq[k] = true;
    Serial.print("Keeping fork ");
    Serial.print(forkId[k]);
    Serial.print(", ");
    Serial.print(bits4(NODE_BITS[neighbor[k]]));
    Serial.println(" has to wait");
  }
}

// FORK:<epoch>:<fork>:<seq>:<to>:<clean>
void handleFork(const String &s)
{
  if (parseBits4(field(s, 4)) != MY_NODE)
    return;
  int f = field(s, 2).toInt();
  int k = slotForFork(f);
  if (k < 0)
    return;
  unsigned long seq = (unsigned long)field(s, 3).toInt();
  String fack = String("FACK:") + ep() + ":" + String(f) + ":" + String(seq) + ":" + bits4(MY_NODE);
  if (seq <= forkSeq[k])
  { // a resend of a fork already received
    broadcast(fack);
    return;
  }
  inTransit[k] = false; // it came back, so our own send got through
  holding[k] = true;
  forkSeq[k] = seq;
  requested[k] = false;
  broadcast(fack);
  Serial.print("Got fork ");
  Serial.println(f);
  broadcastState();
}

// FACK:<epoch>:<fork>:<seq>:<from>
void handleFack(const String &s)
{
  int k = slotForFork(field(s, 2).toInt());
  if (k < 0 || !inTransit[k])
    return;
  if ((unsigned long)field(s, 3).toInt() == forkSeq[k] && nodeIndex(parseBits4(field(s, 4))) == neighbor[k])
  {
    inTransit[k] = false;
  }
}

// CMD:<epoch>:<seq>:<boards>:<colors>:<counts>
void handleCmd(const String &s)
{
  int boards = parseBits4(field(s, 3));
  if (boards < 0 || !(boards & MY_NODE))
    return;
  unsigned long seq = (unsigned long)field(s, 2).toInt();
  broadcast(String("CACK:") + ep() + ":" + String(seq) + ":" + bits4(MY_NODE));
  if (seq <= lastCmdSeq)
    return; // resend of a command already queued
  lastCmdSeq = seq;
  int colors = parseBits3(field(s, 4));
  if (colors <= 0 || cmdCount >= MAX_QUEUED_CMDS)
    return;
  cmdQueue[cmdCount].colors = colors;
  cmdQueue[cmdCount].counts = field(s, 5);
  cmdCount++;
  Serial.print("Queued command for next meal: ");
  Serial.print(field(s, 4));
  Serial.print("/");
  Serial.println(field(s, 5));
  if (state == THINKING)
    becomeHungry();
}

void handleUdp()
{
  int packetSize = udp.parsePacket();
  if (packetSize <= 0)
    return;
  char buf[256];
  int len = udp.read(buf, sizeof(buf) - 1);
  if (len <= 0)
    return;
  buf[len] = 0;
  String s = String(buf);
  s.trim();
  if (s.startsWith("INIT:"))
  {
    handleInit(s);
    return;
  }
  if (!initialized || (unsigned long)field(s, 1).toInt() != epoch)
    return; // old table setup
  if (s.startsWith("REQ:"))
    handleReq(s);
  else if (s.startsWith("FORK:"))
    handleFork(s);
  else if (s.startsWith("FACK:"))
    handleFack(s);
  else if (s.startsWith("CMD:"))
    handleCmd(s);
  else if (s.startsWith("HUNGRY:") && state == THINKING)
    becomeHungry();
}

void setup()
{
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
  setLeds();
  randomSeed(ESP.getChipId() ^ micros()); // different random sequence per board

  // seat at the table: left fork is shared with the previous board, right with the next
  me = nodeIndex(MY_NODE);
  forkId[0] = (me + PHILOSOPHERS - 1) % PHILOSOPHERS;
  neighbor[0] = (me + PHILOSOPHERS - 1) % PHILOSOPHERS;
  forkId[1] = me;
  neighbor[1] = (me + 1) % PHILOSOPHERS;
  holding[0] = holding[1] = false;
  inTransit[0] = inTransit[1] = false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(apSsid, apPass);
  Serial.print("Connecting to AP");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(300);
    if (millis() - start > 15000)
    {
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
  Serial.print("Philosopher ");
  Serial.print(bits4(MY_NODE));
  Serial.print(" | left fork ");
  Serial.print(forkId[0]);
  Serial.print(" (shared with ");
  Serial.print(bits4(NODE_BITS[neighbor[0]]));
  Serial.print("), right fork ");
  Serial.print(forkId[1]);
  Serial.print(" (shared with ");
  Serial.print(bits4(NODE_BITS[neighbor[1]]));
  Serial.println(")");
  Serial.println("Waiting for the listener to set the table...");
}

void loop()
{
  handleUdp();
  handleBlink();
  updateForks();
  updateHunger();
  updateMeal();
  updateThinking();
  if (initialized && millis() - lastStateAt >= STATE_REPORT_MS)
    broadcastState();
  if (!initialized && millis() - lastHelloAt >= HELLO_MS)
  { // lets the listener know this board is here
    broadcast(String("HELLO:") + bits4(MY_NODE));
    lastHelloAt = millis();
  }
  delay(1);
}
