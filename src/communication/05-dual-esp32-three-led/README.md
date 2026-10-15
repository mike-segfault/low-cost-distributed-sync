# 05 - Dual ESP32, Three LEDs

One ESP32 sender controls red, green, and blue LEDs on a second ESP32. This stage introduces the **3-bit color codes** used in every later stage: red is `001`, green is `010`, and blue is `100`. One command can blink several colors in sequence.

It builds on [04-dual-esp32-single-led](../04-dual-esp32-single-led/), which blinked a single LED, and leads into [06-multi-board](../06-multi-board/), which adds a second receiver and uses the same 3-bit idea to address boards.

## Files

| Folder | Board | Purpose |
|---|---|---|
| `32-send-3-led` | ESP32 | WiFi access point; reads color/count pairs from Serial and broadcasts them |
| `32-receiver-3-led` | ESP32 | Joins the network and blinks the requested colors |

## Commands

Type color/count pairs into the sender's Serial Monitor, separated by spaces: `R`, `G`, or `B` (either case) followed by a blink count. For example, `R3 G5 B1` blinks red 3 times, then green 5 times, then blue once. Colors blink one after another, in the order typed. The receiver runs at most three color/count pairs per command.

## Messages

The sender turns each letter into its 3-bit code (`colorCode()`) and broadcasts one message per command:

```
CMD:<seq>:<code>:<count>,<code>:<count>,...
```

`R3 G5 B1` becomes `CMD:4:1:3,2:5,4:1`, where `1`, `2`, and `4` are `001`, `010`, and `100` written in decimal. The receiver replies `OK:<seq>`. If no `OK` arrives within 1.5 s, the sender re-broadcasts, up to 2 retries.

The receiver maps each code back to a pin (`pinForCode()`), queues one blink job per pair, and blinks them with a non-blocking state machine (200 ms on, 200 ms off), so it can still receive packets while blinking.

## Wiring

Each LED is wired GPIO → 220–330 Ω resistor → LED anode, with the cathode to GND. The sender needs no LEDs.

| LED | Receiver GPIO |
|---|---|
| Red | GPIO14 (D14) |
| Green | GPIO27 (D27) |
| Blue | GPIO26 (D26) |

## Serial output

```
[sender]   Broadcasted: CMD:4:1:3,2:5,4:1
[receiver] Received: CMD:4:1:3,2:5,4:1
[receiver] Sent reply: OK:4
[sender]   ACK received for seq=4
[receiver] Blink sequence complete
```

## Running it

Flash `32-send-3-led` first, since it creates the network (`32_AP`, password `32pass88`, UDP port 4210), then flash `32-receiver-3-led`. Both sketches use **115200 baud**, unlike later stages, which use 9600.
