# 01 - ESP-NOW

The project's first board-to-board test. Two ESP32s talk directly using ESP-NOW, Espressif's connectionless protocol, which addresses boards by MAC address instead of IP and needs no WiFi network or access point. One board sends simulated temperature and humidity readings, and the other prints them and flashes its onboard LED for each packet.

It leads into [02-broadcast](../02-broadcast/), where the project switches to a WiFi network and UDP, the approach every later stage uses.

## Files

| Folder | Board | Purpose |
|---|---|---|
| `sender` | ESP32 | Sends a simulated reading to a known MAC address every 1.5–2 s, and can also receive |
| `receiver` | ESP32 | Prints its own MAC address, then prints every reading it receives |

## How it works

Both boards run in WiFi station mode without joining a network, and they initialize ESP-NOW (`esp_now_init()`). ESP-NOW is callback-driven: instead of polling for packets, each sketch registers functions that run when a packet arrives (`OnDataRecv`) or finishes sending (`OnDataSent`).

The data is a struct, and both sketches must define it identically, since the raw bytes are copied straight into it on arrival:

```cpp
typedef struct struct_message {
  float temperature;
  float humidity;
  int id;
} struct_message;
```

The receiver checks that each packet is exactly the struct's size before copying it with `memcpy`, so a mismatched struct prints `Unexpected data length` instead of producing garbage values.

The sender fills the struct with random values around 25.5 °C and 60% humidity, sends it with `esp_now_send()`, and waits 1.5 s plus a random 0–0.5 s. The random part keeps two boards that are both sending from repeatedly transmitting at the same moment. Because the sender also registers a receive callback, both boards can run `sender`, each set to the other's MAC address, to send in both directions.

## Setup

ESP-NOW needs each board's MAC address ahead of time:

1. Flash `receiver` and open its Serial Monitor. It prints its MAC address in both text and hex form (`0x5C, 0x01, ...`).
2. Paste the hex form into `receiverMAC[]` at the top of `sender`.
3. Flash `sender` to the other board.

The sender registers the receiver as a peer (`esp_now_add_peer()`) before sending. No wiring is needed. Both boards flash their onboard LED (GPIO2) when a packet arrives. Both sketches use 115200 baud.

## Serial output

```
[sender]   Peer added OK: 5C:01:3B:BF:7B:C4
[sender]   Sent with success
[sender]   Last Packet Send Status:	Success
[receiver] Received from MAC: <sender MAC>
[receiver] Temperature: 25.80
[receiver] Humidity: 61.30
```
