# 02 - Broadcast

The first WiFi and UDP link between two boards. An ESP32 creates its own WiFi network, an ESP8266 joins it, and the two exchange messages: the ESP32 broadcasts a command and the ESP8266 replies to confirm it. Nothing happens physically yet. This stage proves the network, the message format, and the reply path that every later stage builds on.

It builds on [01-espnow](../01-espnow/) and leads into [03-esp32-to-8266-led](../03-esp32-to-8266-led/), where the ESP8266 blinks an LED in response.

## Files

| Folder | Board | Purpose |
|---|---|---|
| `32-broadcast` | ESP32 | Creates the WiFi access point and broadcasts a test command on a timer |
| `8266-receiver` | ESP8266 | Joins the network and replies to every command it receives |

## How it works

The ESP32 runs in access point mode (`WiFi.softAP()`), creating the network `32_AP` at `192.168.4.1`. The ESP8266 joins as a station and gets an address such as `192.168.4.2`. Both open UDP port 4210.

Every few seconds the ESP32 sends a message to the subnet's broadcast address, `192.168.4.255`, so every board on the network receives it:

```
CMD:1:<seq>:3.14
```

This defines the message layout used from here on: a `CMD` prefix, a command field (`1`), a sequence number that increases with every message, and a value (a fixed test value, `3.14`). The ESP8266 pulls out the sequence number and replies directly to the sender's address with `OK:<seq>`. The ESP32 waits up to 1.5 s for a reply, prints it or `No reply received`, then pauses 2 s before sending the next message.

## Serial output

```
[ESP32]   Broadcasted: CMD:1:2:3.14
[ESP8266] Received from 192.168.4.1:4210 -> CMD:1:2:3.14
[ESP8266] Sent reply: OK:2
[ESP32]   Reply from 192.168.4.2:4210 -> OK:2
```

## Running it

Flash `32-broadcast` first, then `8266-receiver`. No wiring is needed. Both sketches use **115200 baud**, unlike later stages, which use 9600.

The access point password must be at least 8 characters, or the ESP32 won't create the network. The receiver also has no connection timeout: if it can't find `32_AP`, it prints dots forever, so check that the ESP32 is running and that the SSID and password match.
