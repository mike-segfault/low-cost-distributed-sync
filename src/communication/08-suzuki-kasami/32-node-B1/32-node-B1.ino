/*
32-node-B1


*/
#include <WiFi.h>
#include <WiFiUdp.h>

const char* apSsid = "32_AP";
const char* apPass = "32pass88";
const uint16_t PORT = 4210;
WiFiUDP udp;
IPAddress bcast(192, 168, 4, 255);

const int MY_NODE = 0b001; //BOARD 1 = 001
const int NODES = 3;
const int NODE_BITS[NODES] = {0b001, 0b010, 0b100}; //B1,B2,B3
const bool CREATES_TOKEN = true; //one node creates token at boot

const int RED_PIN   = 14; //D14
const int GREEN_PIN = 27; //D27
const int BLUE_PIN  = 26; //D26
const int TOKEN_PIN = 25; //D25 token
const int WAIT_PIN  = 33; //D33 wait

const unsigned long BLINK_ON_MS = 200;
const unsigned long BLINK_OFF_MS = 200;
const unsigned long TOKEN_HOLD_MS = 500; //minimum hold so token is visible
const unsigned long TACK_TIMEOUT = 1000; //ms
const int MAX_RETRIES = 2;
const unsigned long REQUEST_MIN_MS = 3000; //random wait before asking for token
const unsigned long REQUEST_MAX_MS = 8000;
const unsigned long REQUEST_RETRY_MS = 15000; //ask again if no token
const int ADD_CHANCE = 50; //chance for random command per turn
const int MAX_RANDOM_COUNT = 3; //keeping blink to 3 max in random command
const int MAX_CMDS = 8;

//blink jobs taken from token
struct BlinkJob {int pin; int count;};
const int MAX_JOBS = 24;
BlinkJob jobQueue[MAX_JOBS];
int jobCount = 0;
int jobIndex = 0;

enum BlinkState {IDLE, ON, OFF};
BlinkState blinkState = IDLE;
int blinkRemaining = 0;
unsigned long blinkTimer = 0;
int currentPin = -1;

//requests RN[i] = highest request number heard from node i
unsigned long RN[NODES] = {0, 0, 0};
int arrival[NODES]; //nodes with requests, in order they reached this board
int arrivalLen = 0;
bool requesting = false;
unsigned long requestedAt = 0;
unsigned long nextRequestAt = 0;

//token state
bool hasToken = false;
unsigned long tokenSeq = 0; //seq of token this board holds/passes
unsigned long highestSeen = 0; //newest token seq seen anywhere on ring
unsigned long lastAcceptedSeq = 0; //so a retried token is re-ACKed, not taken twice
bool passing = false;
int passTarget = -1; //node index being given token
int passAttempt = 0; //0 = first send, 1+ = retries
unsigned long passStartedAt = 0;

void setup() {
  // put your setup code here, to run once:

}

void loop() {
  // put your main code here, to run repeatedly:

}
