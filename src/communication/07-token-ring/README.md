# 07 - Token Ring

Mutual exclusion across two boards using a token that travels around a fixed ring. The central ESP32 creates the token and adds the commands you type to it; the boards pass it from one to the next, and only the board holding the token may blink. This is the first stage where boards coordinate **exclusive access** rather than just timing.

It builds on [06-multi-board](../06-multi-board/), where both boards could blink at the same time, and leads into [08-suzuki-kasami](../08-suzuki-kasami/), which removes central's role and lets the token move only on request.

## Files

| Folder | Board | Purpose |
|---|---|---|
| `32-central` | ESP32 | WiFi access point, creates the token, adds typed commands to it, and is a member of the ring |
| `32-receiver-B1` | ESP32 | Ring member, node `001` |
| `8266-receiver-B2` | ESP8266 | Ring member, node `010` |

The two receiver sketches run the same code apart from node number, WiFi library, and pins.

## How it works

The token travels in a fixed order and wraps around:

```
central (000) -> B1 (001) -> B2 (010) -> central (000) -> ...
```

1. You type commands into central's Serial Monitor. They wait on central until the token comes back around.
2. When central holds the token, it adds the waiting commands to the token's queue and passes it to B1.
3. Each board that receives the token takes every queue entry with its bit set, clears its bit (entries with no boards left are removed), blinks them, and passes the token to the next node.
4. With nothing to do, the token keeps circulating, held for at least 500 ms at each stop so the token and waiting LEDs show it moving.

## The critical section

Blinking is the critical section, and the token is permission to enter it. A board blinks only between receiving the token and passing it on, and there is only one token, so two boards never blink at the same time.

When a token arrives, the board turns its TOKEN LED on and its WAITING LED off, pulls out its entries (`takeMyEntries()`), and starts blinking. Blinking is non-blocking, so the board keeps receiving messages while it blinks. `updateHold()` refuses to pass the token until blinking has finished and the minimum hold time is up. That check is what keeps the token from leaving a board mid-blink. Once the next node confirms it has the token, the board turns its WAITING LED back on.

## Central's role

This design is a hybrid. Central decides **what** work gets done, because every command enters the token at central. The ring decides **who may act**, because the token passes board to board without central in between. Central is part of the ring for exactly this reason: the token has to come back to it so new commands can be added.

That makes the system partly centralized. If central stops, no new work can enter the system, even though the boards could keep passing the token. Stage 08 removes this dependency: boards generate their own commands, and no node is required for the algorithm to keep running.

## The token

```
TOKEN:<seq>:<to>:<queue>
```

| Field | Meaning | Example |
|---|---|---|
| `seq` | Sequence number, increased on every pass | `5` |
| `to` | Node bits this pass is addressed to | `010` |
| `queue` | Entries separated by `\|`, each `boards/colors/counts` | `010/101/3,5\|010/001/2` |

Every pass is broadcast, so central hears and logs the board-to-board passes too.

## Passing reliably

UDP packets can be lost, so each pass must be confirmed. The receiving node replies `TACK:<seq>:<node>`.

| Problem | How it's handled |
|---|---|
| Token or `TACK` lost | Resend after `TACK_TIMEOUT` (1 s), up to 2 retries |
| Node never answers | Skip it and offer the token to the next node in the ring |
| Nobody answers | Keep the token and try again after `RETRY_IDLE_MS` (2 s) |
| Token resent after it was already taken | The receiver recognizes the repeated `seq`, re-sends its `TACK`, and doesn't blink again |
| An old copy of the token is still held | Any node that hears a newer `seq` drops its copy |

## Commands

Commands are typed on central as `<boards> <colors> <count per color>`, with boards and colors as 3-bit patterns read right to left (boards: B1 `001`, B2 `010`; colors: red `001`, green `010`, blue `100`). For example, `011 101 3 5` means both boards blink red 3 times, then blue 5 times. Separate several commands with `;`. Central only accepts board patterns `001`, `010`, and `011`, since an entry for a board that doesn't exist would never be cleared from the token.

## Wiring

Each receiver has five LEDs, each wired GPIO → 220–330 Ω resistor → LED anode, with the cathode to GND. Central needs no LEDs.

| LED | B1 (ESP32) | B2 (ESP8266) |
|---|---|---|
| Red | GPIO14 (D14) | GPIO14 (D5) |
| Green | GPIO27 (D27) | GPIO12 (D6) |
| Blue | GPIO26 (D26) | GPIO13 (D7) |
| Token | GPIO25 (D25) | GPIO4 (D2) |
| Waiting | GPIO33 (D33) | GPIO5 (D1) |

ESP8266 labels are for NodeMCU-style boards and differ on other layouts, so wire to the GPIO numbers.

## Serial output

Typing `011 101 3 5` on central:

```
[central] Queued 011/101/3,5 - joins the token at central
[central] Token back at central seq=3 | queue: (empty)
[central] Added 1 command(s) to token | queue: 011/101/3,5
[central] Passed token seq=4 to 001 | queue: 011/101/3,5
[B1]      Token received seq=4 | queue: 011/101/3,5
[B1]      Running: 011/101/3,5
[B1]      Blink sequence complete
[B1]      Passed token seq=5 to 010 | queue: 010/101/3,5
[central] Token seq=5 -> 010 | queue: 010/101/3,5
[B2]      Token received seq=5 | queue: 010/101/3,5
[B2]      Running: 010/101/3,5
```

B1 runs its part, clears its bit, and passes `010/101/3,5` on for B2. The two boards blink the same command one after the other, never together.

## Running it

Flash `32-central` first, since it creates the network (`32_AP`, password `32pass88`, UDP port 4210) and the token. Then flash both receivers and open central's Serial Monitor at 9600 baud to type commands.

- **Reset all boards together.** Central restarts its sequence numbers at 0, while receivers that weren't reset remember higher ones and would ignore the new token.
- **Board-to-board passing depends on the access point forwarding broadcasts between its stations.** If B2 never logs `Token received` after B1 passes to it, that forwarding isn't happening.

## What changed in 08

Stage 08 keeps the token, the critical section, and the reliability layer, but changes how the token moves and who supplies the work: the token goes only to boards that request it instead of circling a fixed ring, central's role is reduced to WiFi and logging, boards add their own random commands, and a third board joins. Testing for 08 also showed that this stage's retry settings (1 s timeout, 2 retries) can occasionally produce a duplicate token under heavy packet loss, so 08 uses 10 faster attempts instead.
