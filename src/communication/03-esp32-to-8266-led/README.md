# 03 - ESP32 to ESP8266, LED

The first stage where a command over the network makes something physical happen. An ESP32 broadcasts a command, and an ESP8266 blinks an LED in response and replies to confirm it received the command. It's also the first time the two chip families work together, since the ESP8266 uses its own WiFi library and its own pin labeling.

It builds on [02-broadcast](../02-broadcast/), which only exchanged messages, and leads into [04-dual-esp32-single-led](../04-dual-esp32-single-led/), which adds typed input and retries.

## Files

| Folder | Board | Purpose |
|---|---|---|
| `32-broadcast` | ESP32 | WiFi access point; broadcasts a command on a timer and prints any reply |
| `8266-receiver` | ESP8266 | Joins the network, blinks the LED, and replies `OK` |

## How it works

The ESP32 creates the network, waits 3 seconds for the receiver to join, then repeats a loop: broadcast a command, wait up to 1.5 s for a reply, then pause 2 s. The command is fixed:

```
CMD:1:<seq>:3.14
```

The `1` is a command field, `seq` increases each time, and the last field is the value. The receiver reads the value as a blink count with `toInt()`, which stops at the decimal point, so `3.14` becomes **3 blinks**. It replies `OK:<seq>` to the sender's address, and the sender prints the reply along with the IP and port it came from. There are no retries yet. If nothing arrives within 1.5 s, the sender prints `No reply received` and moves on.

The receiver blinks with a non-blocking state machine (200 ms on, 200 ms off) driven by `millis()`, so it keeps checking for packets while the LED blinks. Later stages reuse this state machine.

## Wiring

On the ESP8266, wire GPIO14 → 220–330 Ω resistor → LED anode (long leg), with the cathode (short leg) to GND. On NodeMCU-style boards GPIO14 is labeled **D5**. ESP8266 "D" labels don't match GPIO numbers, which is why the sketch uses `14`, not `5`.

## Serial output

```
[ESP32]   Broadcasted: CMD:1:2:3.14
[ESP8266] Received: CMD:1:2:3.14
[ESP8266] Sent reply: OK:2
[ESP32]   Reply from 192.168.4.2:4210 -> OK:2
[ESP8266] Blink sequence complete
```

## Running it

Flash `32-broadcast` first, since it creates the network (`32_AP`, password `32pass88`, UDP port 4210), then flash `8266-receiver`. Both use 9600 baud. Once connected, the LED blinks 3 times every few seconds with no input needed.
