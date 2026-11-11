# Test Results: Classical Dining Philosophers

11/11/2026

Results from running the classical Dining Philosophers version (`09-dining-philosophers/01-classical`) on real hardware: four philosopher boards and one listener. Every excerpt below is copied from the listener's Serial Monitor.

## Setup

| | |
|---|---|
| Version | `01-classical` (left fork first, `RANDOM_HUNGER = false`) |
| Philosophers | B1 `0001` (ESP32), B2 `0010`, B3 `0100`, B4 `1000` (ESP8266MOD) |
| Listener | ESP32, access point `32_AP` |
| Starting forks | Each philosopher holds its left fork: B1 `[3]`, B2 `[0]`, B3 `[1]`, B4 `[2]` |

## How to read the table

The listener prints one line every time a philosopher reports a change:

```
Table:  0001 thinking [3] x0  0010 HUNGRY [0] x0  0100 EATING [1,2] x1  1000 thinking [-] x0
```

Each entry is a board, its state, the forks it holds in brackets (`-` means none), and its meal count after the `x`. Forks are numbered 0–3. Fork *i* lies between seat *i* and seat *i* + 1, so B1 and B2 share fork 0, B2 and B3 share fork 1, B3 and B4 share fork 2, and B4 and B1 share fork 3.

## Results

| Check | Result |
|---|---|
| A lone hungry board gets both forks and eats | Passed |
| Neighbors take turns with their shared fork | Passed (B3–B4 and B1–B2) |
| Non-neighbors eat at the same time | Passed (B1 and B3) |
| Neighbors never eat at the same time | Passed, never observed in the whole run |
| Every hungry board eventually eats when forks are not in the deadlock position | Passed (both `hungry` triggers and the `1111` command) |
| `init` then `hungry` deadlocks | Passed (separate run, below) |

Final meal counts across the run: B1 ×5, B2 ×4, B3 ×10, B4 ×8. B3 and B4 received the most commands.

### One hungry board

`0100 110 2 2` makes only B3 hungry. B3 already holds its left fork (1), asks B4 for fork 2, and B4 gives it up because it is thinking:

```
Sent CMD:570863:10:0100:110:2,2
Table:  0001 thinking [3] x0  0010 thinking [0] x0  0100 HUNGRY [1] x0  1000 thinking [2] x0
Table:  0001 thinking [3] x0  0010 thinking [0] x0  0100 HUNGRY [1] x0  1000 thinking [-] x0
Table:  0001 thinking [3] x0  0010 thinking [0] x0  0100 HUNGRY [1,2] x0  1000 thinking [-] x0
Table:  0001 thinking [3] x0  0010 thinking [0] x0  0100 EATING [1,2] x0  1000 thinking [-] x0
Table:  0001 thinking [3] x0  0010 thinking [0] x0  0100 thinking [1,2] x1  1000 thinking [-] x0
```

A board that finishes eating keeps both forks, since there is no table to put them back on. B3's next three commands ran immediately because it already held both.

### Neighbors take turns

`1100 111 3 2 2` makes B3 and B4 hungry together. They share fork 2. B4 already holds both of its forks, so it eats first, then hands fork 2 to B3:

```
Sent CMD:724098:16:1100:111:3,2,2
Table:  0001 thinking [-] x0  0010 thinking [0] x0  0100 HUNGRY [1] x3  1000 HUNGRY [2,3] x2
Table:  0001 thinking [-] x0  0010 thinking [0] x0  0100 HUNGRY [1] x3  1000 EATING [2,3] x2
Table:  0001 thinking [-] x0  0010 thinking [0] x0  0100 HUNGRY [1] x3  1000 thinking [2,3] x3
Table:  0001 thinking [-] x0  0010 thinking [0] x0  0100 HUNGRY [1] x3  1000 thinking [3] x3
Table:  0001 thinking [-] x0  0010 thinking [0] x0  0100 HUNGRY [1,2] x3  1000 thinking [3] x3
Table:  0001 thinking [-] x0  0010 thinking [0] x0  0100 EATING [1,2] x3  1000 thinking [3] x3
```

B1 and B2 show the same turn-taking with fork 0 after `0011 001 3`: B1 eats, then fork 0 and fork 1 move to B2, which eats next.

### Non-neighbors eat at the same time

`0101 111 1 1 1` makes B1 and B3 hungry. They share no forks (B1 uses 3 and 0, B3 uses 1 and 2), so both eat at once:

```
Table:  0001 EATING [3,0] x3  0010 thinking [-] x3  0100 EATING [1,2] x9  1000 thinking [-] x8
```

With four philosophers, at most two can eat at the same time, and they are always opposite each other.

### Everyone hungry, but no deadlock

`hungry` was sent twice, and `1111 111 1 1 1` once, all without `init` first. Each time, every philosopher ate:

| Trigger | Forks when everyone got hungry | Who ate first | Outcome |
|---|---|---|---|
| `hungry` | B1 `[-]`, B2 `[0]`, B3 `[1]`, B4 `[2,3]` | B4, which already held both forks | All four ate |
| `1111 111 1 1 1` | B1 `[-]`, B2 `[0,1]`, B3 `[2]`, B4 `[3]` | B2, which already held both forks | All four ate |

Earlier meals had moved the forks, so one board started with both of its forks, ate, and released them. That broke the circle before it could form.

### Deadlock (separate run)

Right after `init`, every philosopher holds exactly its left fork. `hungry` then makes each one reach for its right fork, which its neighbor is holding as a left fork:

```
Everyone is hungry!
Table:  0001 HUNGRY [3] x0  0010 thinking [0] x0  0100 thinking [1] x0  1000 thinking [2] x0
Table:  0001 HUNGRY [3] x0  0010 thinking [0] x0  0100 HUNGRY [1] x0  1000 thinking [2] x0
Table:  0001 HUNGRY [3] x0  0010 HUNGRY [0] x0  0100 HUNGRY [1] x0  1000 thinking [2] x0
Table:  0001 HUNGRY [3] x0  0010 HUNGRY [0] x0  0100 HUNGRY [1] x0  1000 HUNGRY [2] x0
DEADLOCK: every philosopher is hungry, holding one fork and waiting on the other
```

No board ever eats again until the table is reset with `init`.

## Findings

Deadlock depends on the state of the system and the command. The same `hungry` command deadlocked the table when every board held exactly one fork, and resolved normally when forks had moved. A system that seems to work in casual testing can still deadlock when the timing and state line up.

The listener's view can briefly be out of date. Some lines momentarily show a fork on two boards:

```
Table:  0001 EATING [3,0] x2  0010 thinking [-] x3  0100 HUNGRY [2] x8  1000 HUNGRY [3] x7
```
