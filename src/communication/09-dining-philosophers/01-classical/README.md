# 01 - Classical Dining Philosophers

The dining philosophers problem, introduced by Dijkstra [1], on real hardware. Four philosopher boards share four forks around a table and follow the classical rules with no changes: pick up the left fork, then the right, and hold on to whatever has been picked up while waiting. A fifth board, the listener, sets the table and can make every philosopher hungry at once. When it does, every philosopher ends up holding one fork and waiting forever for the other. **This version exists to deadlock.** [02-prevention](../02-prevention/) (work in progress) shows how to prevent it.

The design decisions and hardware limitations behind this implementation are described in [DINING_PHILOSOPHERS.md](../../../documentation/DINING_PHILOSOPHERS.md). Results from running it on the boards are in `logs/09-dining-philosophers/01-classical/`.

## Files

| Folder | Board | Purpose |
|---|---|---|
| `32-listener` | ESP32 | WiFi access point; sets the table, sends the hunger trigger and LED commands, logs the table, and detects deadlock. Not a philosopher |
| `32-node-B1` | ESP32 | Philosopher `0001` |
| `8266-node-B2` | ESP8266MOD | Philosopher `0010` |
| `8266-node-B3` | ESP8266MOD | Philosopher `0100` |
| `8266-node-B4` | ESP8266MOD | Philosopher `1000` |

The four philosopher sketches run the same code apart from node number, WiFi library, and pins.

## The table

The philosophers sit in board-ID order. Fork *i* lies between seat *i* and seat *i* + 1, so each philosopher shares its left fork with the previous board and its right fork with the next one, and the last fork closes the ring.

| Philosopher | Seat | Left fork (shared with) | Right fork (shared with) |
|---|---|---|---|
| B1 `0001` | 0 | 3 (B4) | 0 (B2) |
| B2 `0010` | 1 | 0 (B1) | 1 (B3) |
| B3 `0100` | 2 | 1 (B2) | 2 (B4) |
| B4 `1000` | 3 | 2 (B3) | 3 (B1) |

```
            fork 0
     B1 ------------- B2
      |               |
fork 3|               |fork 1
      |               |
     B4 ------------- B3
            fork 2
```

## From the problem to the hardware

| Classical problem | On the boards |
|---|---|
| Philosopher | A node board |
| Fork | A token that exactly one board holds at a time, passed by message |
| Thinking | Idle, both status LEDs off |
| Hungry | WAITING LED on; asking neighbors for forks |
| Eating | TOKEN LED on; running an LED sequence (the critical section) |
| The table | Doesn't exist. There is no shared memory, so the forks live on the boards |

Because there is no physical table to put a fork down on, a fork is never "free": some board always holds it, and a fork moves only when its holder sends it to a neighbor who asked for it. A board that finishes eating keeps both forks until someone asks.

## The classical rules

A hungry philosopher picks up its **left fork first**, then its right fork, and eats once it holds both:

```cpp
int firstSlot() {
  return 0; //left
}

void updateHunger() {
  if (state != HUNGRY) return;
  int first = firstSlot();
  int second = 1 - first;
  if (!holding[first]) { askFor(first); return; }
  if (!holding[second]) { askFor(second); return; }
  startMeal();
}
```

When a neighbor asks for a fork, the holder decides with `shouldGive()`:

```cpp
bool shouldGive(int k) {
  if (state == EATING) return false;   //never give up a fork mid-meal
  if (state == THINKING) return true;  //not using it, hand it over
  int first = firstSlot();
  return !(k == first || holding[first]);
}
```

A thinking board gives the fork away, and an eating board keeps it until the meal is over. A hungry board keeps its left fork, and keeps its right fork too once it holds the left. That last rule is the classical "hold what you have while you wait," and it's what makes deadlock possible. A refused request isn't forgotten: the holder remembers it (`pendingReq`) and hands the fork over as soon as it finishes eating.

## Why it deadlocks

The listener starts every board holding its **left** fork. When it sends `hungry`, every philosopher already has its first fork, so each one asks its right-hand neighbor for its second. But that neighbor is also hungry and is holding that fork as *its* left fork, so it refuses:

```
B1 waits for fork 0, held by B2
B2 waits for fork 1, held by B3
B3 waits for fork 2, held by B4
B4 waits for fork 3, held by B1
```

Each board waits on the next one, and the last waits on the first. No board can eat, so no board ever releases a fork. All four WAITING LEDs stay on, and no TOKEN LED ever lights.

A deadlock needs four conditions to hold at the same time [2], [3], and the classical rules allow all four:

| Condition | How it holds here |
|---|---|
| Mutual exclusion | A fork is held by one board at a time |
| Hold and wait | A hungry board keeps its left fork while waiting for its right |
| No preemption | A fork can't be taken from a board; the holder has to give it up |
| Circular wait | Each board waits on its right-hand neighbor, all the way around the ring |

**The deadlock depends on where the forks are, not just on the command.** Once boards have eaten, forks move: a board that eats keeps both of its forks. If any board holds both forks when everyone gets hungry, it simply eats, releases them, and the others follow, so no deadlock forms. That's why `init`, which puts every fork back in its starting place, should always come before `hungry` when demonstrating the deadlock.

## The critical section

Eating is the critical section, and each fork acts as a lock shared by two neighbors. A board can only eat while holding both of its forks, so its two neighbors, who each need one of those forks, can't eat at the same time. Boards that aren't neighbors, like B1 and B3, can eat at once. This is the main difference from the token stages in `communication/`, where only one board in the whole system could be in its critical section at a time.

## Messages

All messages are broadcast UDP. Every message after `INIT` carries the table's **epoch**, so messages left over from an earlier setup are ignored.

| Message | Sent by | Purpose |
|---|---|---|
| `HELLO:<node>` | Philosopher | Sent every second while waiting for a table, so the listener knows the board is there |
| `INIT:<epoch>:<holders>:0` | Listener | Sets the table; `holders` lists the holder of forks 0–3 |
| `IACK:<epoch>:<node>` | Philosopher | Confirms the table was set |
| `HUNGRY:<epoch>` | Listener | Every thinking philosopher becomes hungry |
| `REQ:<epoch>:<fork>:<node>` | Philosopher | Asks the neighbor holding a fork for it |
| `FORK:<epoch>:<fork>:<seq>:<to>:-` | Philosopher | Hands a fork to a neighbor |
| `FACK:<epoch>:<fork>:<seq>:<node>` | Philosopher | Confirms a fork arrived |
| `CMD:<epoch>:<seq>:<boards>:<colors>:<counts>` | Listener | LED command for the boards' next meal |
| `CACK:<epoch>:<seq>:<node>` | Philosopher | Confirms a command arrived |
| `STATE:<epoch>:<node>:<T/H/E>:<forks>:<meals>` | Philosopher | Reports state on every change, and every 2 s |

**Forks are never lost or duplicated.** A board that sends a fork stops holding it immediately, and resends it every 500 ms until the `FACK` arrives. Every transfer has a sequence number, so a resent fork that already arrived is acknowledged again but not taken twice. Fork requests that go unanswered are re-sent every 1.5 s.

## The listener

The listener runs the WiFi network and plays the role of the environment: it decides where the forks start and when the philosophers get hungry. It never holds a fork.

| Command | Effect |
|---|---|
| `hungry` | Every thinking philosopher becomes hungry at once |
| `init` | Resets the table with a new epoch; every board goes back to thinking, holding its left fork |
| `table` | Prints the last known state of every philosopher |
| `<boards> <colors> <counts>` | LED command for the next meal, e.g. `1010 101 3 2` (B2 and B4: red ×3, then blue ×2). The boards become hungry to run it |

Boards are 4-bit patterns (`0001`, `0010`, `0100`, `1000`); colors are 3-bit (red `001`, green `010`, blue `100`), with one count (1–7) per color. Without a command, a meal is a random LED sequence. Type one command per line.

The listener prints the table whenever a philosopher's state changes, and announces `DEADLOCK` when every philosopher is hungry and nothing has changed for 5 seconds. Its picture of the table is assembled from `STATE` messages, so it's always slightly behind the boards themselves. A line can even briefly show a fork on two boards, when the receiving board's report arrives before the giving board's. No single board, including the listener, has a true global view of the system.

**Boards that stop or restart.** A board the listener hasn't heard from for 6 seconds is shown as `offline`, and the listener prints `<node> is offline`. A board that restarts after being seated loses the forks it held, so those forks no longer exist anywhere. The listener detects this from the board's `HELLO` and prints `<node> restarted and lost its forks - type init to reset the table`. The table can't repair itself safely, since rebuilding the lost forks automatically could duplicate one, so it waits for `init`. Offline boards are never counted toward a deadlock.

## Hunger

`RANDOM_HUNGER` is `false`, so philosophers only get hungry when the listener tells them to (or when a command arrives). Making everyone hungry at the same moment makes the deadlock repeatable for a demonstration.

Setting `RANDOM_HUNGER` to `true` gives each philosopher a random 3–8 second thinking time instead. The deadlock still can happen, but it becomes rare, because every philosopher has to grab its left fork before any neighbor finishes a meal. That's the point about deadlock worth making in the paper: it depends on timing, which is why a system can pass casual testing and still be broken.

## Wiring

Each philosopher has five LEDs, each wired GPIO → 220–330 Ω resistor → LED anode, with the cathode to GND. The listener needs none.

| LED | B1 (ESP32) | B2–B4 (ESP8266MOD) | Meaning |
|---|---|---|---|
| Red | GPIO14 (D14) | GPIO14 (D5) | Meal blinks |
| Green | GPIO27 (D27) | GPIO12 (D6) | Meal blinks |
| Blue | GPIO26 (D26) | GPIO13 (D7) | Meal blinks |
| Token | GPIO25 (D25) | GPIO4 (D2) | Eating |
| Waiting | GPIO33 (D33) | GPIO5 (D1) | Hungry |

ESP8266 labels are for NodeMCU-style boards and differ on other layouts, so wire to the GPIO numbers.

## Serial output

The listener, after `init` and then `hungry`:

```
Everyone is hungry!
Table:  0001 HUNGRY [3] x0  0010 thinking [0] x0  0100 thinking [1] x0  1000 thinking [2] x0
Table:  0001 HUNGRY [3] x0  0010 thinking [0] x0  0100 HUNGRY [1] x0  1000 thinking [2] x0
Table:  0001 HUNGRY [3] x0  0010 HUNGRY [0] x0  0100 HUNGRY [1] x0  1000 thinking [2] x0
Table:  0001 HUNGRY [3] x0  0010 HUNGRY [0] x0  0100 HUNGRY [1] x0  1000 HUNGRY [2] x0
DEADLOCK: every philosopher is hungry, holding one fork and waiting on the other
```

Each entry shows the forks a philosopher holds in brackets and its meal count after the `x`. On B2 at the same time:

```
Hungry
Asked 0100 for fork 1
Keeping fork 0, 0001 has to wait
```

B2 holds fork 0, asks B3 for fork 1, and refuses B1's request for fork 0. Every other board prints the same thing, one seat over.

## Settings

| Constant | Value | Purpose |
|---|---|---|
| `RANDOM_HUNGER` | `false` | Random hunger timers on or off |
| `THINK_MIN_MS` / `THINK_MAX_MS` | 3000 / 8000 | Random thinking time, when enabled |
| `FORK_RETRY_MS` | 500 | Resend a fork until its `FACK` arrives |
| `REQ_RETRY_MS` | 1500 | Ask again for a fork that hasn't come |
| `STATE_REPORT_MS` | 2000 | Repeat `STATE` so a lost one is replaced |
| `HELLO_MS` | 1000 | Send `HELLO` while waiting for a table |
| `MAX_RANDOM_BLINKS` | 3 | Blinks per color in a random meal |
| `DEADLOCK_QUIET_MS` (listener) | 5000 | How long nothing must change before deadlock is reported |
| `OFFLINE_MS` (listener) | 6000 | How long a board can be silent before it's shown as offline |

## Running it

1. Flash `32-listener` first. It creates the network (`32_AP`, password `32pass88`, UDP port 4210).
2. Flash the four philosophers. They can start in any order: the listener resends `INIT` every second until all four answer, then prints `Table set: all four philosophers seated`.
3. If the boards were flashed one at a time, or any board restarted, type `init` once all four are running. Otherwise, the listener may still count boards that have since been unplugged.
4. Open the listener's Serial Monitor at 9600 baud. Type `init`, then `hungry`.
5. Type `init` to reset the table and try again.

## References

[1] E. W. Dijkstra, "Hierarchical ordering of sequential processes," *Acta Informatica*, vol. 1, no. 2, pp. 115–138, 1971.

[2] E. G. Coffman, M. Elphick, and A. Shoshani, "System deadlocks," *ACM Computing Surveys*, vol. 3, no. 2, pp. 67–78, 1971.

[3] M. Singhal and N. G. Shivaratri, *Advanced Concepts in Operating Systems: Distributed, Database, and Multiprocessor Operating Systems*. New York: McGraw-Hill, 1994.

[5] Suzuki, Ichiro, Tadao Kasami. *A distributed mutual exclusion algorithm*. ACM Transactions on Computer Systems (TOCS), Volume 3, Issue 4, Pages 344 - 349, November 1985. https://dl.acm.org/doi/abs/10.1145/6110.214406.
