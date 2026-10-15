# 04 - Dual ESP32, Single LED

One ESP32 sends a blink count, and a second ESP32 blinks an LED that many times. This is the first stage with **acknowledgements**: each command carries a sequence number, the receiver confirms it with `OK`, and the sender retries if no confirmation arrives. That send, confirm, retry pattern is used in every later stage.

It builds on [03-esp32-to-8266-led](../03-esp32-to-8266-led/) and leads into [05-dual-esp32-three-led](../05-dual-esp32-three-led/), which adds red, green, and blue LEDs.

## Files

| Folder | Board | Purpose |
|---|---|---|
| `32-send` | ESP32 | WiFi access point; reads a blink count from Serial and broadcasts it |
| `32-receiver` | ESP32 | Joins the network, confirms each command, and blinks the LED |

## How it works

Type a positive whole number into the sender's Serial Monitor. The sender broadcasts:

```
CMD:1:<seq>:<count>
```

The `1` is a fixed command field, `seq` increases with every command, and `count` is the number of blinks. The receiver replies `OK:<seq>`. If no matching `OK` arrives within 1.5 s, the sender broadcasts again, up to 2 retries, before giving up. Matching on `seq` means a late reply to an old command is never mistaken for a reply to the current one.

The receiver blinks with a non-blocking state machine (200 ms on, 200 ms off) driven by `millis()` instead of `delay()`, so it keeps checking for packets while the LED blinks.

## Wiring

On the receiver, wire GPIO18 (D18) → 220–330 Ω resistor → LED anode (long leg), with the cathode (short leg) to GND. The sender needs no LEDs.

## Serial output

Typing `3` on the sender:

```
[sender]   Broadcasted: CMD:1:1:3
[receiver] Received: CMD:1:1:3
[receiver] Sent reply: OK:1
[sender]   ACK received for seq=1
[receiver] Blink sequence complete
```

## Running it

Flash `32-send` first, since it creates the network (`32_AP`, password `32pass88`, UDP port 4210), then flash `32-receiver`. Both sketches use 9600 baud.
