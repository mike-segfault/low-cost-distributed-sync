# 06 - Multi-Board

One ESP32 sender controls the RGB LEDs on two receivers, an ESP32 (B1) and an ESP8266 (B2). Boards and colors are both addressed with 3-bit patterns, so a single broadcast can target several boards at once. Different boards blink at the same time, while commands for the same board run in order.

It builds on [05-dual-esp32-three-led](../05-dual-esp32-three-led/), which controlled one receiver, and leads into [07-token-ring](../07-token-ring/), where boards stop blinking concurrently and take turns instead. This folder holds the final version of the stage. Earlier versions (one board per command, and waiting for each board to finish before sending the next command) are in the git history.

## Files

| Folder | Board | Purpose |
|---|---|---|
| `32-sender` | ESP32 | WiFi access point; reads commands from Serial, broadcasts them, and tracks each board's progress |
| `32-receiver-B1` | ESP32 | Receiver, board `001` |
| `8266-receiver-B2` | ESP8266 | Receiver, board `010` |

The two receivers run the same code apart from board number, WiFi library, and pins.

## Commands

Type commands into the sender's Serial Monitor as `<boards> <colors> <count per color>`. Boards and colors are 3-bit patterns read right to left, with one count (1–7) for each color bit set, in red → green → blue order.

| Bits | Boards | Colors |
|---|---|---|
| `001` | B1 | Red |
| `010` | B2 | Green |
| `100` | (B3, reserved) | Blue |

| Input | Result |
|---|---|
| `001 001 3` | B1 blinks red 3 times |
| `011 101 3 5` | Both boards blink red 3 times, then blue 5 times, at the same time |
| `001 001 2; 010 100 4` | B1 red and B2 blue, at the same time |
| `001 001 2; 001 010 2` | B1 only: red, then green (same board, so in order) |

Separate commands with `;`. A malformed command prints `Skipped "..." - <reason>` and the rest of the line still runs.

## Messages

| Message | Sent by | Meaning |
|---|---|---|
| `CMD:<seq>:<boards>:<colors>:<counts>` | Sender, broadcast | A command, e.g. `CMD:1:011:101:3,5` |
| `OK:<seq>:<board>` | Receiver | "I received this command" |
| `DONE:<seq>:<board>` | Receiver | "I finished blinking this command" |

This stage separates **receipt** from **completion**. An `OK` only means the command arrived; the sender waits for `DONE` before giving that board its next command. Without the distinction, a second command could start before the first one finished.

## How the sender schedules

The sender never blocks. Every pass of `loop()` it reads all incoming replies, checks timeouts, and sends whatever is ready (`updateScheduler()`).

- **Per-board order.** `busyMask` tracks which boards are running a command. A pending command is sent only when all of its boards are free, and a command that has to wait also holds back later commands for the same boards, so each board keeps its order.
- **Concurrency across boards.** Commands for different free boards are sent in the same pass, so their blinking overlaps.
- **Per-board tracking.** A command for both boards is one broadcast, but the sender tracks each board's `OK` and `DONE` separately and frees each board as soon as it finishes.
- **Lost packets.** If any board's `OK` is missing after 1.5 s, the command is re-broadcast, up to 2 retries. A board that never answers is given up on, and a board that never reports `DONE` is freed after the expected blink time plus 1 s.

## How the receivers work

Each receiver checks whether its bit is set in the command's board pattern. If not, it logs `Ignored` and does nothing. Otherwise it replies `OK`, turns the color bits and counts into blink jobs, blinks them with a non-blocking state machine, and replies `DONE` when finished. If the same `seq` arrives again (a retry because some board's `OK` was lost), it re-sends `OK` but doesn't blink again.

## Wiring

Each receiver has three LEDs, each wired GPIO → 220–330 Ω resistor → LED anode, with the cathode to GND. The sender needs no LEDs.

| LED | B1 (ESP32) | B2 (ESP8266) |
|---|---|---|
| Red | GPIO14 (D14) | GPIO14 (D5) |
| Green | GPIO27 (D27) | GPIO12 (D6) |
| Blue | GPIO26 (D26) | GPIO13 (D7) |

ESP8266 labels are for NodeMCU-style boards and differ on other layouts, so wire to the GPIO numbers.

## Serial output

Typing `011 101 3 5` on the sender:

```
[sender] Broadcasted: CMD:1:011:101:3,5
[B1]     Received (for b1 32): CMD:1:011:101:3,5
[B1]     Sent reply: OK:1:001
[B2]     Received (for b2 8266): CMD:1:011:101:3,5
[B2]     Sent reply: OK:1:010
[sender] ACK received for seq=1 from board 001
[sender] ACK received for seq=1 from board 010
[B1]     Blink sequence complete
[B1]     Sent completion: DONE:1:001
[sender] Completed seq=1 on board 001
[B2]     Blink sequence complete
[B2]     Sent completion: DONE:1:010
[sender] Completed seq=1 on board 010
```

## Running it

Flash `32-sender` first, since it creates the network (`32_AP`, password `32pass88`, UDP port 4210). Then flash both receivers and open the sender's Serial Monitor at 9600 baud to type commands.

Nothing here stops the two boards from blinking at the same time. Coordinating timing is not the same as mutual exclusion, and [07-token-ring](../07-token-ring/) adds a token so that only one board can blink at a time.
