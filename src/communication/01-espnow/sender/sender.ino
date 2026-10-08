/*
sender.ino

Implementation of the sender half. Randomly generates "data" of temperature
and humidity every few seconds. The, sends off to known MAC address from
receiverMAC[]. Random delay included to avoid collision.

Requires MAC address of board to send to.
*/

#include <WiFi.h>
#include <esp_now.h>

//b1: receiverMAC to b2 MAC
//b2: receiverMAC to b1 MAC
//uint8_t is fixed-width unsigned integer for 8-bit value
uint8_t receiverMAC[] = {0x5C, 0x01, 0x3B, 0xBF, 0x7B, 0xC4};

//struct matching between sender and receiver
typedef struct struct_message {
  float temperature;
  float humidity;
  int id;
} struct_message;
struct_message myData;
struct_message incomingData;

const int LED_PIN = 2;
//printing MAC in string and hex
void printMac() {
  //string
  String macStr = WiFi.macAddress();
  Serial.print("Local MAC: ");
  Serial.println(macStr);
  //hex
  Serial.print("Local MAC: ");
  for (int i = 0; i < 6; ++i) {
    uint8_t b = (uint8_t) strtoul(macStr.substring(i*3, i*3 + 2).c_str(), NULL, 16);
    Serial.printf("0x%02X", b);
    if (i < 5) Serial.print(", ");
  }
  Serial.println();
}

//callback send
//esp_now_send_status_t is just the status type for displaying success
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
  Serial.print("Dest MAC: ");
  for (int i = 0; i < 6; ++i) {
    Serial.printf("%02X", receiverMAC[i]);
    if (i < 5) Serial.print(":");
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

  //ESP-NOW init
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initiating ESP-NOW");
    return;
  }

  esp_now_register_send_cb(OnDataSent); //sent callback
  esp_now_register_recv_cb(OnDataRecv); //recieve callback

  //peer
  esp_now_peer_info_t peerInfo; //struct for peer-to-peer
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, receiverMAC, 6);
  peerInfo.channel = 0; //0 for current wifi channel
  peerInfo.encrypt = false; //no need for encryptionm
  peerInfo.ifidx = WIFI_IF_STA; //STA interface

  if (esp_now_add_peer(&peerInfo) == ESP_OK) {
    Serial.print("Peer added OK: ");
    for (int i = 0; i < 6; ++i) {
      Serial.printf("%02X", receiverMAC[i]);
      if (i < 5) Serial.print(":");
    }
    Serial.println();
  } else {
    Serial.println("Peer add FAILED (will use broadcast).");
    uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};\
    memcpy(receiverMAC, bcast, 6);
  }
  Serial.println("Setup complete. Sending every ~2s with small jitter.");
}

void loop() {
  //sim sensor data
  myData.temperature = 25.5 + random(-10, 10) / 10.0;
  myData.humidity = 60.0 + random(-50, 50) / 10.0;
  myData.id = 1; //identifier for board

  //sent via ESP-NOW
  //esp_err_t is 32-bit signed int for error codes
  esp_err_t result = esp_now_send(receiverMAC, (uint8_t *) &myData, sizeof(myData));
  if (result == ESP_OK) {
    Serial.println("Sent with success");
  } else {
    Serial.println("esp_now_send returned: ");
    Serial.println(result);
  }
  
  delay(1500 + random(0, 500)); //1.5 seconds + random to reduce collisions
}
