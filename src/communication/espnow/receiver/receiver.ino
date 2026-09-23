/*
receiver.ino

Implementation of ESP-NOW library utilizing peer-to-peer connectionless wireless 
communication involving the MAC addresses of each microcontroller. Each connecting 
boards needs each other's MAC addresses. 

This implementation involves initializing ESP-NOW and having the board in station
mode, then registering a callback that fires when a packet arrives from a paired sender.
Incoming bytes of randomly generated fake data are stored in a struct, then parsed 
to make readable.
*/

#include <WiFi.h>
#include <esp_now.h>

//struct matching between sender and receiver
typedef struct struct_message {
  float temperature;
  float humidity;
  int id;
} struct_message;

struct_message incomingData;
const int LED_PIN = 2;
//printing MAC in string and hex
void printLocalMacBytes() {
  //string
  String mac = WiFi.macAddress();
  Serial.print("Local MAC (str): ");
  Serial.println(mac);
  //hex
  Serial.print("Local MAC (hex): ");
  for (int i = 0; i < 6; ++i) {
    uint8_t b = (uint8_t) strtoul(mac.substring(i*3, i*3 + 2).c_str(), NULL, 16);
    Serial.printf("0x%02X", b);
    if (i < 5) Serial.print(", ");
  }
  Serial.println();
}

//callback recieve
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  //memcpy(&incomingData, incomingData, sizeof(incomingData)); //copy number of bytes from source memory location
  if (len != sizeof(incomingData)) {
    Serial.print("Unexpected data length: ");
    Serial.println(len);
    return;
  }
  //copying incoming bytes to incomingData struct
  memcpy(&incomingData, data, sizeof(incomingData));

  Serial.print("Received from MAC: ");
  //peer MAC
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", recv_info->src_addr[i]); //MAC bytes concatenated
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  Serial.print("Temperature: ");
  Serial.println(incomingData.temperature);
  Serial.print("Humidity: ");
  Serial.println(incomingData.humidity);

  //LED indicator
  digitalWrite(LED_PIN, HIGH);
  delay(60);
  digitalWrite(LED_PIN, LOW);
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  delay(150);

  WiFi.mode(WIFI_STA); //wifi station
  WiFi.setSleep(false); //disabling sleep

  printLocalMacBytes();
  //ESP-NOW init
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initiating ESP-NOW");
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(OnDataRecv); //recieve callback
  Serial.println("Reciever ready. Waiting for packets...");
}

void loop() {
  delay(1000); //waiting for callbacks
}
