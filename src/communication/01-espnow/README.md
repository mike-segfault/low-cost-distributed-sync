# 01 - ESP-NOW

The project's first board-to-board test. Two ESP32s talk directly using ESP-NOW, Espressif's connectionless protocol, which addresses boards by MAC address instead of IP and needs no WiFi network or access point. One board sends simulated temperature and humidity readings, and the other prints them and flashes its onboard LED for each packet.

It leads into [02-broadcast](../02-broadcast/), where the project switches to a WiFi network and UDP, the approach every later stage uses.

## Files

| Folder | Board | Purpose |
|---|---|---|
| `sender` | ESP32 | Sends a simulated reading to a known MAC address every 1.5–2 s, and can also receive |
| `receiver` | ESP32 | Prints its own MAC address, then prints every reading it receives |

## How it works

Both boards run in WiFi station mode without joining a network, and they initialize ESP-NOW. The sender fills a struct with random values around 25.5 °C and 60% humidity, sends it, and waits 1.5 s plus a random 0–0.5 s. The random part keeps two boards that are both sending from repeatedly transmitting at the same moment. Because the sender also registers a receive callback, both boards can run `sender`, each set to the other's MAC address, to send in both directions.

The rest of this README walks through the ESP-NOW API calls and types the sketches use, in the order they run.

## Starting WiFi and ESP-NOW

```cpp
WiFi.mode(WIFI_STA);   //wifi station
WiFi.setSleep(false);  //disabling sleep

if (esp_now_init() != ESP_OK) {
  Serial.println("Error initiating ESP-NOW");
  return;
}
```

ESP-NOW travels over the WiFi radio, so WiFi must be started before ESP-NOW is initialized. `WiFi.mode(WIFI_STA)` turns the radio on in station mode. The board never connects to a network; station mode just gives ESP-NOW an interface to send and receive on. `WiFi.setSleep(false)` disables WiFi power saving, which would otherwise let the radio doze between beacons and miss incoming packets.

`esp_now_init()` sets up the ESP-NOW layer and returns an `esp_err_t` (see [Error codes](#error-codes-esp_err_t)). Every ESP-NOW function returns one, so each call can be checked for success.

## Registering callbacks

```cpp
esp_now_register_send_cb(OnDataSent); //sent callback
esp_now_register_recv_cb(OnDataRecv); //receive callback
```

ESP-NOW doesn't make the sketch check for packets. Instead, the sketch hands ESP-NOW a **callback**: a function that ESP-NOW calls by itself when something happens. "Registering" a callback means passing ESP-NOW a pointer to that function, which it stores and calls later. That's why the function names appear without parentheses: `OnDataSent` is the function itself, not a call to it.

| Function | Registers | Called when |
|---|---|---|
| `esp_now_register_send_cb()` | `OnDataSent` | A send has finished, with whether it succeeded |
| `esp_now_register_recv_cb()` | `OnDataRecv` | A packet arrives |

Each callback must match the exact signature ESP-NOW expects, or the sketch won't compile:

```cpp
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status)
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
```

The send callback's signature depends on the ESP32 core version. Starting with ESP-IDF 5.5, which recent Arduino ESP32 cores (3.3 and later) are built on, the first parameter changed from the destination MAC address (`const uint8_t *mac_addr`) to a pointer to send information (`wifi_tx_info_t`). Older tutorials use the old signature, which no longer compiles on current cores. The sketch prints the destination from its own `receiverMAC[]` instead, since the MAC is no longer passed in.

**Callbacks run in the WiFi task, not in `loop()`.** Espressif recommends keeping them short and doing longer work elsewhere, such as passing data to a queue that another task handles. `OnDataRecv` here prints several lines and pauses 60 ms to flash the LED. That's fine for a slow test like this one, but in anything time-sensitive, the callback should only copy the data and set a flag, and `loop()` should do the printing.

## Peers (`esp_now_peer_info_t`)

Before a board can send to another board, it has to add that board as a **peer**. A peer is described by an `esp_now_peer_info_t` struct:

```cpp
esp_now_peer_info_t peerInfo;               //struct for peer-to-peer
memset(&peerInfo, 0, sizeof(peerInfo));     //zero every field first
memcpy(peerInfo.peer_addr, receiverMAC, 6); //the peer's 6-byte MAC address
peerInfo.channel = 0;                       //0 = current WiFi channel
peerInfo.encrypt = false;                   //no encryption
peerInfo.ifidx = WIFI_IF_STA;               //send on the station interface

esp_now_add_peer(&peerInfo);
```

| Field | Type | Meaning |
|---|---|---|
| `peer_addr` | `uint8_t[6]` | The peer's MAC address |
| `channel` | `uint8_t` | WiFi channel (0–14). `0` means "whatever channel this board is on now" |
| `ifidx` | `wifi_interface_t` | Which radio interface to use: station (`WIFI_IF_STA`) or access point (`WIFI_IF_AP`) |
| `encrypt` | `bool` | Whether packets to and from this peer are encrypted |
| `lmk` | `uint8_t[16]` | Encryption key for this peer, only used when `encrypt` is true |
| `priv` | `void *` | Optional pointer for the application's own data |

`peerInfo` is a local variable, so its memory starts out holding leftover garbage. **`memset()` sets every byte to 0** first, so fields the sketch doesn't assign, like `lmk` and `priv`, have known values instead of random ones. Then `memcpy()` copies the six MAC bytes into `peer_addr`. An array can't be assigned with `=` in C++, so it has to be copied byte by byte.

`esp_now_add_peer()` copies the struct into ESP-NOW's internal peer list. Only the sender needs to do this: a board can receive unencrypted packets from anyone without adding them as a peer.

## Sending (`esp_now_send`)

```cpp
//esp_err_t is 32-bit signed int for error codes
esp_err_t result = esp_now_send(receiverMAC, (uint8_t *) &myData, sizeof(myData));
```

| Argument | Meaning |
|---|---|
| `receiverMAC` | Which peer to send to. Passing `NULL` sends to every peer in the list |
| `(uint8_t *) &myData` | The address of the data, viewed as raw bytes |
| `sizeof(myData)` | How many bytes to send |

`esp_now_send()` only accepts bytes, so the struct's address is **cast** to `uint8_t *`, a pointer to unsigned 8-bit values. Nothing about the data changes; the cast just tells the compiler to treat the struct as a plain sequence of bytes.

There are two separate results. The **return value** says whether the packet was accepted for sending, for example `ESP_OK`, or an error if the peer was never added. The **send callback** fires later with `ESP_NOW_SEND_SUCCESS` or `ESP_NOW_SEND_FAIL`, saying whether the packet was actually delivered at the radio level. Even "success" only means the other board's radio received it. ESP-NOW doesn't guarantee the receiving sketch processed it, which is why later stages add their own `OK` replies and retries.

## Receiving and `memcpy`

```cpp
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  if (len != sizeof(incomingData)) { /* print error and return */ }
  memcpy(&incomingData, data, sizeof(incomingData));
  ...
}
```

The receive callback gets three things:

| Parameter | Meaning |
|---|---|
| `recv_info` | Information about the packet. `recv_info->src_addr` is the sender's MAC address, and it also holds the destination address and radio details |
| `data` | A pointer to the received bytes |
| `len` | How many bytes arrived |

**`memcpy(destination, source, n)` copies `n` bytes from one place in memory to another.** Here it copies the received bytes into the `incomingData` struct, after which the fields can be read normally (`incomingData.temperature`). The incoming data is only valid during the callback, so it has to be copied out to be used.

`memcpy` does no checking at all. It copies exactly `n` bytes, whatever is actually at those addresses. If the packet were shorter than the struct, it would copy past the end of the real data. That's why the length check comes first: a packet that isn't exactly `sizeof(incomingData)` bytes is rejected before `memcpy` ever runs.

Copying raw bytes straight into a struct works only because **both boards lay out the struct identically**. That means the same fields in the same order, the same sizes (`float` and `int` are 4 bytes each on the ESP32), and the same byte order. Changing the struct on one board but not the other gives wrong values or a length mismatch. It's also why the struct is defined at the top of both sketches.

## Types used

| Type | What it is |
|---|---|
| `uint8_t` | An unsigned 8-bit integer (0–255), one byte. MAC addresses are arrays of six of them |
| `esp_err_t` | Espressif's error code type: a 32-bit signed integer, where `0` means success |
| `esp_now_peer_info_t` | The struct describing a peer (see [Peers](#peers-esp_now_peer_info_t)) |
| `esp_now_send_status_t` | An enum passed to the send callback: `ESP_NOW_SEND_SUCCESS` or `ESP_NOW_SEND_FAIL` |
| `esp_now_recv_info_t` | Information about a received packet, including the sender's MAC (`src_addr`) |
| `wifi_tx_info_t` | Information about a finished send, passed to the send callback on current cores |
| `struct_message` | This project's own struct: `temperature`, `humidity`, and `id` |

## Error codes (`esp_err_t`)

ESP-IDF functions report errors by returning an `esp_err_t`, a plain 32-bit signed integer. `ESP_OK` is `0` and means success. Any other value is a specific error code. That's why the sketches compare results against `ESP_OK` rather than checking true or false.

ESP-NOW has its own error codes, such as `ESP_ERR_ESPNOW_NOT_INIT` (ESP-NOW wasn't initialized), `ESP_ERR_ESPNOW_NOT_FOUND` (sending to a peer that was never added), and `ESP_ERR_ESPNOW_EXIST` (adding a peer twice). `Serial.println(result)` prints the code as a number. `esp_err_to_name(result)` turns it into the readable name instead, which makes failures much easier to diagnose.

## Limits

ESP-NOW (v1.0) packets carry at most **250 bytes** of data, which is plenty for this 12-byte struct. Newer ESP-IDF versions add ESP-NOW v2.0, which allows up to 1470 bytes on supported chips. Packets can optionally be encrypted with a per-peer key, but this stage leaves encryption off.

## Setup

ESP-NOW needs each board's MAC address ahead of time:

1. Flash `receiver` and open its Serial Monitor. It prints its MAC address in both text and hex form (`0x5C, 0x01, ...`).
2. Paste the hex form into `receiverMAC[]` at the top of `sender`.
3. Flash `sender` to the other board.

No wiring is needed. Both boards flash their onboard LED (GPIO2) when a packet arrives. Both sketches use 115200 baud.

## Serial output

```
[sender]   Peer added OK: 5C:01:3B:BF:7B:C4
[sender]   Sent with success
[sender]   Last Packet Send Status:	Success
[receiver] Received from MAC: <sender MAC>
[receiver] Temperature: 25.80
[receiver] Humidity: 61.30
```

## References

Espressif Systems, "ESP-NOW," *ESP-IDF Programming Guide*, v5.5. [Online]. Available: https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/network/esp_now.html

Espressif Systems, "Migration from 5.4 to 5.5: Wi-Fi," *ESP-IDF Programming Guide*, v5.5. [Online]. Available: https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/migration-guides/release-5.x/5.5/wifi.html
