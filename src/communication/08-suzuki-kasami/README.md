# 08 - Suzuki-Kasami

Decentralized mutual exclusion across three boards, based on the Suzuki-Kasami token algorithm. Boards request a single shared token at random times, and only the board holding it may enter its critical section (blinking its LEDs). The token carries the entire shared state of the system, including a list of commands that boards run, clear, and add to. There is no coordinator: whichever board holds the token decides who gets it next.

This stage builds on [07-token-ring](../07-token-ring/), where a central board created the token and passed it around a fixed ring. Here the token moves only when a board asks for it, and the central board's decision-making role is gone.

## Files

| Folder | Board | Purpose |
|---|---|---|
| `32-node-B1` | ESP32 | Peer, node `001`. Also creates the single token at startup |
| `8266_node_B2` | ESP8266 | Peer, node `010` |
| `8266-node-B3` | ESP8266 | Peer, node `100`, the third peer added in this stage |
| `32-observer` | ESP32 | WiFi access point and traffic log; takes no part in the algorithm |

## How it works

1. At a random interval, a board without the token broadcasts a request, without knowing what's on the token.
2. Every board records the request. Whichever request reaches the current holder first gets queued first.
3. When the holder finishes its critical section, it passes the token to the first board in the queue, or keeps it if nobody is waiting.
4. The new holder runs every command addressed to it, clears its bit from those commands, may add a random command, and the cycle repeats.

## The nodes

The three node sketches run the same code. Once the system is running, no board is special: each one requests the token, waits its turn, and gives it up by the same rules. The only differences are:

| | B1 | B2 | B3 |
|---|---|---|---|
| Sketch | `32-node-B1` | `8266_node_B2` | `8266-node-B3` |
| Board | ESP32 | ESP8266 | ESP8266 |
| Node bits (`MY_NODE`) | `001` | `010` | `100` |
| Creates the token (`CREATES_TOKEN`) | Yes | No | No |
| Random seed | None needed | `randomSeed(ESP.getChipId() ^ micros())` | Same as B2 |

**B1 creates the token.** Exactly one token must exist, and one board has to start with it. B1 boots holding the token with an empty command list and its TOKEN LED on, while B2 and B3 boot with their WAITING LEDs on. Creating the token does not make B1 a coordinator. From then on it follows exactly the same rules as the others.

**Random seeding.** The ESP32's `random()` uses the chip's hardware random number generator, so B1 needs no seed. The ESP8266 does need one. Seeding with the chip ID gives B2 and B3 different random sequences, so they don't request the token at the same times on every boot.

**Three bits, three boards.** Adding B3 filled the last bit of the 3-bit board patterns, so every board pattern from `001` to `111` now targets real boards.

### Wiring

Each node has five LEDs, each wired GPIO → 220–330 Ω resistor → LED anode (long leg), with the cathode (short leg) to GND.

| LED | B1 (ESP32) | B2, B3 (ESP8266) | Meaning |
|---|---|---|---|
| Red | GPIO14 (D14) | GPIO14 (D5) | Command blinks |
| Green | GPIO27 (D27) | GPIO12 (D6) | Command blinks |
| Blue | GPIO26 (D26) | GPIO13 (D7) | Command blinks |
| Token | GPIO25 (D25) | GPIO4 (D2) | On while the board holds the token |
| Waiting | GPIO33 (D33) | GPIO5 (D1) | On whenever the board does not hold the token |

The ESP8266 labels are for NodeMCU-style boards. ESP8266 "D" labels do not match GPIO numbers, and they differ between board layouts (Uno-shaped boards put GPIO4 and GPIO5 on the pins labeled D4 and D3), so wire to the GPIO numbers and check your board's pinout.

## The critical section

### What it protects

In operating systems terms, a critical section is code that must never run on more than one process at a time, usually because it touches a shared resource. In this project the "processes" are separate microcontrollers that share no memory, and the critical section is **running the commands on the token**, which shows up as blinking the red, green, and blue LEDs. The rule being enforced is simple: **a board may only blink while it holds the token, and there is only one token.** If two boards ever blink at the same time, mutual exclusion has failed, so the LEDs double as a live check on the algorithm.

### Entering

There are exactly two ways into the critical section, and both require holding the token:

1. **The token arrives** (`handleToken()`). After confirming the token is addressed to it and isn't an old copy, the board sends a `TACK`, turns the TOKEN LED on, and calls `enterCS()`.
2. **The board already holds the token and its request timer fires** (`updateRequest()`). Because the token stays with the last holder when nobody else wants it, a board that still has it simply takes another turn without asking anyone.

`enterCS()` sets `inCS = true` and calls `takeMyCommands()`, which walks the token's command list. Every command with the board's bit set is turned into blink jobs, and the board's bit is cleared from it. A command addressed only to that board is removed entirely, while a command for several boards stays on the token with the remaining boards' bits still set, waiting for them to receive the token.

```cpp
void enterCS() {
  inCS = true;
  takeMyCommands();
  jobIndex = 0;
  if (jobCount > 0) startJob(0);
}
```

### Inside

The blinking is done by a non-blocking state machine (`handleBlink()`, states `IDLE` → `ON` → `OFF`) driven by `millis()` instead of `delay()`. This matters: while the board is in its critical section, `loop()` keeps running, so requests from other boards are still received and recorded. Those boards are not ignored; they are queued, and they get the token after the current holder is finished.

What the board will **not** do while `inCS` is true is give the token away. `updateHold()` returns early until the blinking is finished:

```cpp
void updateHold() {
  if (!hasToken || passing) return;
  if (inCS) {
    if (blinkState == IDLE) finishCS();
    return;
  }
  if (millis() - heldSince < TOKEN_HOLD_MS) return;
  passOn();
}
```

This is the core guarantee: the token cannot leave a board in the middle of its critical section, so no other board can enter one.

### Exiting

When the last blink job finishes, `finishCS()` runs:

```cpp
void finishCS() {
  maybeAddRandomCommand();
  LN[me()] = RN[me()];
  inCS = false;
  Serial.print("Done | commands: ");
  Serial.println(cmdString());
}
```

Three things happen on the way out. The board may add one random command to the token (see [Commands on the token](#commands-on-the-token)). It records its own request as served by copying its request number into the token's `LN` array, which is how every other board will later know this request doesn't need the token again. Finally it clears `inCS`. After a minimum hold of `TOKEN_HOLD_MS` (500 ms, so the TOKEN LED is visible), `passOn()` hands the token to the next waiting board, or keeps it if nobody is waiting.

### When the critical section is cut short

If a board hears a token with a **newer** sequence number moving between other boards while it believes it holds the token, its own copy must be stale (see [Handling lost messages](#handling-lost-messages)). In that case `stopBlinking()` turns off the active LED and abandons the remaining jobs, and `releaseToken()` gives up the token. The design choice here is deliberate: aborting a critical section is always preferable to two boards being in one at the same time.

## Decentralization

There is no coordinator. No board assigns turns, and no board knows the global state at all times. Every decision is made locally by whichever board currently holds the token, using two sources of information:

- **What the holder has heard.** Every board keeps `RN[]`, the highest request number it has heard from each node, and `arrival[]`, the order those requests reached it. Every request is broadcast, so every board builds its own copy, but these copies can differ because packets arrive at different boards at slightly different times.
- **What the token carries.** The token holds the shared state of the whole system: `LN[]` (the last request served for each node), the waiting queue, and the command list. Whoever holds the token holds the authoritative state, and it changes hands with the token.

This is what makes the system distributed rather than centralized: the "server" moves. Any board can make scheduling decisions, but only while holding the token, and only one board holds it at a time.

One distinction worth keeping in mind: the **algorithm** is decentralized, but the **network** is not. All boards connect through the observer's WiFi access point (see [The observer](#the-observer)), so if that board loses power, the network disappears. The observer still takes no part in deciding who gets the token. It provides the network the messages travel over, much like a network switch would.

## Requesting the token (the race)

At a random interval of 3–8 seconds (`REQUEST_MIN_MS` to `REQUEST_MAX_MS`), a board that doesn't hold the token asks for it by broadcasting:

```
REQ:<node bits>:<request number>
```

The request number increases by one with every request (`RN[me()]++`). The board doesn't know what's on the token when it asks. It finds out what it has to do only once the token arrives.

When a board receives a request, `handleRequest()` ignores it if the number isn't newer than what it has already heard. Otherwise it stores the number in `RN[]` and appends the sender to `arrival[]`, unless the sender is already in it. Because a board keeps its place in `arrival[]` once it's there, **arrival order decides who goes first**. If B2 and B3 request at nearly the same moment, whichever request reaches the holder first is queued first. That race is a deliberate part of this implementation.

A node `j` still needs the token while its latest request hasn't been served:

```cpp
bool outstanding(int j) {
  return RN[j] > LN[j];
}
```

If no token arrives within `REQUEST_RETRY_MS` (15 s), for example because the request packet was lost, the board asks again with a new request number.

## The token

The token is a single broadcast message:

```
TOKEN:<seq>:<to>:<LN>:<waiting>:<commands>
```

| Field | Meaning | Example |
|---|---|---|
| `seq` | Token sequence number, increased on every pass | `19` |
| `to` | The node bits this pass is addressed to | `010` |
| `LN` | Last request number served for B1, B2, B3 | `13,12,11` |
| `waiting` | Nodes queued for the token, in order (`-` if none) | `100` |
| `commands` | Commands still to run, separated by `\|` (`-` if none) | `100/111/3,2,3\|011/010/2` |

Every board hears every pass, because it's broadcast, but only the addressed board takes it.

## Passing the token

`passOn()` decides who gets the token next:

1. It drops any board in the token's waiting queue whose request has already been served.
2. It appends every board with an outstanding request, in the order their requests reached **this** board (`arrival[]`).
3. If the queue is empty, it keeps the token. Otherwise it removes the first board from the queue and sends the token to it.

Before each pass, the sequence number is set higher than any token the board has seen (`tokenSeq = highestSeen + 1`). That number is what lets every board tell a current token apart from an older copy.

The receiving board answers with `TACK:<seq>:<node bits>`. Until that arrives, the token still belongs to the sender (`passing = true`), and both of its LEDs stay as they are.

## Handling lost messages

Suzuki and Kasami's original algorithm assumes messages are never lost. UDP over WiFi makes no such promise, so this implementation adds a reliability layer:

| Problem | How it's handled |
|---|---|
| Token pass lost | The sender resends every `TACK_TIMEOUT` (500 ms), up to 10 attempts (`MAX_RETRIES = 9`) |
| `TACK` lost, so the token is resent | The receiver recognizes the repeated `seq` (`lastAcceptedSeq`), re-sends its `TACK`, and does **not** run the commands again |
| `TACK` lost every time, but the target did get the token | The sender sees the target pass the token on with a higher `seq` and treats that as confirmation ("pass confirmed") |
| Target never answers | After 10 attempts the sender drops that board's request (`LN[j] = RN[j]`) and moves to the next in line; the dropped board asks again later |
| A board holds an old copy of the token | Any board that hears a token with a higher `seq` than its own drops its copy ("Dropped stale token") |
| Request lost | The requester asks again after 15 s |

The retry settings matter. With fewer, slower retries, heavy packet loss could make the sender give up on a board that actually received the token, which briefly leaves two tokens. Ten fast attempts make that extremely unlikely, and in simulation with 10% and 20% packet loss, no two boards were ever in their critical sections at the same time.

## Commands on the token

Each command is `boards/colors/counts`, with boards and colors as 3-bit patterns read right to left:

| Bits | Boards | Colors |
|---|---|---|
| `001` | B1 | Red |
| `010` | B2 | Green |
| `100` | B3 | Blue |

There is one count (1–7) for each color bit set, in red → green → blue order. For example, `011/101/3,5` means boards 1 and 2 each blink red 3 times, then blue 5 times.

On the way out of its critical section, a board has an `ADD_CHANCE` (50%) chance of adding one random valid command for any combination of boards and colors, with 1 to `MAX_RANDOM_COUNT` (3) blinks per color. The token carries at most `MAX_CMDS` (8) commands. A board that is offline never clears its bit, so its commands stay on the token until it returns or the token fills up.

## The main loop

`loop()` calls five functions in order, and none of them block:

| Function | Job |
|---|---|
| `handleUdp()` | Reads one packet and dispatches it to `handleRequest()`, `handleToken()`, or `handleTack()` |
| `handleBlink()` | Steps the blink state machine |
| `updatePass()` | Resends the token or gives up if no `TACK` has arrived |
| `updateHold()` | Finishes the critical section when blinking ends, then passes the token |
| `updateRequest()` | Sends a request when the random timer fires, or retries one |

## The observer

`32-observer` runs on an ESP32 in access point mode. It creates the WiFi network the three nodes join and prints every message they send. It never requests the token, never holds it, and never sends a message.

In the earlier stages, the access point ESP32 was also the central board, which read your commands and decided where they went. This stage is decentralized, so that role had to go. The boards still need a network to talk over, though, and the observer is what's left once the coordinator role is removed: network infrastructure plus a log.

Every message in this stage is broadcast, so the observer hears all of it. It splits each packet on `:` and prints a readable line:

| Message | Sent by | Observer prints |
|---|---|---|
| `REQ:<node>:<n>` | A board asking for the token | `Request from 010 n=4` |
| `TOKEN:<seq>:<to>:<LN>:<waiting>:<commands>` | The holder passing the token | `Token seq=19 -> 100 \| LN: 13,12,11 \| waiting: - \| commands: ...` |
| `TACK:<seq>:<node>` | A board confirming it received the token | `Token accepted by 100 (seq=19)` |

Because the token carries the whole shared state, every `Token seq=...` line is a complete snapshot of the system at that moment. Reading them in order shows commands being added, boards clearing their bits, and the queue forming as requests race each other.

The observer is also the best board to capture logs from. Each node only prints its own side of the conversation, while the observer sees every board's traffic on one timeline from one clock.

## Serial output

Every board prints at 9600 baud. A typical turn on B1 looks like this:

```
Requested token: REQ:001:13
Token received seq=18 | LN: 12,12,11 | waiting: - | commands: 100/100/2|001/011/2,2|101/111/3,2,3
Sent TACK:18:001
Running: 001/011/2,2
Running: 101/111/3,2,3
Blink sequence complete
Added random command: 011/010/2
Done | commands: 100/100/2|100/111/3,2,3|011/010/2
Passed token seq=19 to 100 | LN: 13,12,11 | waiting: - | commands: 100/100/2|100/111/3,2,3|011/010/2
Token accepted by 100 (seq=19)
```

B1 ran `001/011/2,2` (addressed only to B1, so it was removed) and its part of `101/111/3,2,3` (B1's bit was cleared, leaving `100/111/3,2,3` for B3). It then marked its request served (`LN` went from `12` to `13` for B1) and passed the token to B3.

Other node messages: `Request from <node> n=<n>` (another board asked), `No TACK from ...` and `(retry n)` (resending), `Giving up on ...` (a board didn't answer), `Saw token seq=... moving on - pass confirmed`, `Dropped stale token ...`, and `Turn while already holding the token`.

## Settings

| Constant | Value | Purpose |
|---|---|---|
| `BLINK_ON_MS` / `BLINK_OFF_MS` | 200 / 200 | One blink takes 400 ms |
| `TOKEN_HOLD_MS` | 500 | Minimum time the token is held, so the TOKEN LED is visible |
| `TACK_TIMEOUT` | 500 | Time to wait for a `TACK` before resending |
| `MAX_RETRIES` | 9 | Resends before giving up on a board (10 attempts total) |
| `REQUEST_MIN_MS` / `REQUEST_MAX_MS` | 3000 / 8000 | Random wait before requesting the token |
| `REQUEST_RETRY_MS` | 15000 | Time before asking again if the token never came |
| `ADD_CHANCE` | 50 | Percent chance of adding a random command per turn |
| `MAX_RANDOM_COUNT` | 3 | Maximum blinks per color in a random command |
| `MAX_CMDS` | 8 | Maximum commands the token carries |

## How this differs from the original Suzuki-Kasami algorithm

1. **Queue order comes from arrival, not node number.** The original algorithm adds waiting nodes to the queue by scanning node numbers in order. Here they are queued in the order their requests reached the holder, which makes the race between boards visible.
2. **The token carries work.** Besides `LN` and the queue, the token carries a command list that boards read, execute, clear, and add to.
3. **Requests are blind and random.** Boards request on a timer without knowing whether the token has anything for them.
4. **A reliability layer was added.** `TACK`s, retries, and token sequence numbers handle lost UDP packets, which the original algorithm assumes never happen.

## Running it

1. Flash `32-observer` to the access point ESP32 first. It creates the network `32_AP` (password `32pass88`) at `192.168.4.1` and listens on UDP port 4210. It needs no LEDs.
2. Flash `32-node-B1`, `8266_node_B2`, and `8266-node-B3`, each wired as in [Wiring](#wiring).
3. Open a Serial Monitor at 9600 baud on any board. Use the observer to see all traffic in one place.

**Reset all boards together.** B1 creates the token when it boots, so restarting B1 alone creates a second token. Boards that weren't reset also remember higher sequence numbers and would ignore a fresh token.
