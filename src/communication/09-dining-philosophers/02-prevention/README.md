# 02 - Deadlock Prevention

The same four philosophers, four forks, and listener as [01-classical](../01-classical/), but with three ways of **preventing** the deadlock, taken from Singhal and Shivaratri's *Advanced Concepts in Operating Systems* (sec. 3.9.3). Each method removes one of the four conditions a deadlock needs. All three start from the exact position that deadlocks the classical version, with every philosopher holding its left fork, and none of them ever deadlock.

Everything else works as in 01-classical: the table layout, the messages, fork conservation, the listener's commands, and the wiring. This README covers what's different. See the [01-classical README](../01-classical/README.md) for the rest.

## Files

| Folder | Board | Purpose |
|---|---|---|
| `32-listener` | ESP32 | Same as 01, plus it chooses the prevention method and sends it to every philosopher |
| `32-node-B1` | ESP32 | Philosopher `0001` |
| `8266-node-B2` | ESP8266MOD | Philosopher `0010` |
| `8266-node-B3` | ESP8266MOD | Philosopher `0100` |
| `8266-node-B4` | ESP8266MOD | Philosopher `1000` |

All four philosopher sketches contain all three methods. The listener decides which one runs.

## The four conditions and the three methods

A deadlock can only happen when all four of these conditions hold at once (Coffman et al.). Break any one of them, and deadlock becomes impossible.

| Condition | In the classical version | Prevented by |
|---|---|---|
| Mutual exclusion | A fork is held by one board at a time | Can't be removed: a fork really can't be shared |
| Hold and wait | A hungry board keeps one fork while waiting for the other | **All at once** |
| No preemption | A fork can't be taken from the board holding it | **Rosenkrantz priority preemption** |
| Circular wait | Each board waits on its right-hand neighbor, all the way around | **Havender resource ordering** |

All three methods change the same decision: what a **hungry** board does when a neighbor asks for one of its forks. Thinking boards always give a fork up, and eating boards never do, exactly as in the classical version.

```cpp
bool shouldGive(int k) {
  if (state == EATING) return false;
  if (state == THINKING) return true;
  switch (method) {
    case ALL_AT_ONCE:
      return true;                  //hold nothing until holding everything
    case ROSENKRANTZ:
      return neighbor[k] < me;      //give way only to a higher priority (lower ID)
    default: {                      //HAVENDER
      int first = firstSlot();
      return !(k == first || holding[first]);
    }
  }
}
```

### All at once: no hold and wait

> "The simplest way of preventing deadlock is to acquire all the needed resources before the process begins, thus eliminating the wait while hold condition."

A hungry board asks for **both** forks and keeps neither until it has both. If a neighbor asks for a fork it's holding, it gives the fork up, even while hungry. Since no board ever holds one fork while waiting for the other, the waiting can never form a circle.

**Adapted for the hardware.** The book's version assumes a process can take every resource it needs in one step. Here, the two forks are held by two different boards, so taking both at once isn't possible. The closest equivalent is never holding anything while waiting. That opens a different risk: four boards could hand forks back and forth forever, each always giving up the fork it just received. That's *livelock*, where everyone is busy but no one makes progress. To prevent it, a hungry board that gives up a fork waits a random 200–1500 ms (`BACKOFF_MIN_MS`, `BACKOFF_MAX_MS`) before asking again, so the boards fall out of step and one of them ends up with both forks.

### Rosenkrantz priority preemption: allows preemption

> "A requesting process preempts another process, which holds the needed resource only if the requesting process has higher priority."

Every board has a fixed, unique priority: the lower the board ID, the higher the priority, so B1 is highest and B4 is lowest. That mirrors the convention that an older timestamp wins. A hungry board gives a fork to a neighbor only if the neighbor has **higher** priority; otherwise it keeps the fork. Forks are still requested left first, exactly as in the classical version, so the only change is that higher-priority boards can take forks away.

A board only ever waits on a board with *higher* priority, since a lower-priority board would have handed the fork over. Following the waits from board to board, priority keeps increasing, so the chain can never loop back to where it started.

### Havender resource ordering: no circular wait

> "All of the resources are uniquely ordered and all of the processes request the resources in ascending order only, eliminating the circular wait condition. If a process already holds some resources, then it can request only those resources ranked higher in the ordering."

Forks are numbered 0–3, and every board asks for its **lower-numbered** fork first. For B2, B3, and B4 that's the left fork, the same as in the classical version. For B1, it's the right fork (fork 0 before fork 3). A hungry board keeps its lower fork, and keeps its higher fork only once it already holds the lower one.

That single change to B1 breaks the circle. In the classical version, every board waits on its right-hand neighbor; here, B1 waits on its *left* fork first, so the waits can't go all the way around.

```cpp
int firstSlot() {
  if (method == HAVENDER) return (forkId[0] < forkId[1]) ? 0 : 1;   //lower-numbered fork
  return 0;                                                          //left, as in classical
}
```

## Who breaks the cycle

After `init`, every board holds its left fork, the position that deadlocks the classical version. Here's how each method gets out of it when `hungry` is sent:

| Method | What happens | Who eats first |
|---|---|---|
| All at once | Every board gives away the fork it's asked for, then backs off for a random time. The boards fall out of step until one holds both forks | Varies from run to run |
| Rosenkrantz | B2 gives fork 0 to B1, because B1 has higher priority. B1 keeps fork 3 from B4, because B4 has lower priority. B1 now holds both | B1 (highest priority) |
| Havender | B1 holds fork 3, but its first fork is 0, which it doesn't have. So when B4 asks for fork 3, B1 gives it up. B4 now holds both | B4 |

After the first meal, the forks it released let the next board eat, and so on.

## Choosing the method

The method is set on the **listener**, not on each board, and sent to the philosophers in the `INIT` message:

```
INIT:<epoch>:<fork holders>:<method>
```

That way all four philosophers always use the same method. Boards running different methods would behave unpredictably, and the comparison would mean nothing.

| Method | `PREVENTION_METHOD` in the listener | Serial command |
|---|---|---|
| All at once | `'A'` | `method all` |
| Rosenkrantz | `'R'` | `method rosenkrantz` |
| Havender | `'H'` (default) | `method havender` |

`PREVENTION_METHOD` sets the method at startup. The `method` command switches it while running and resets the table with a new epoch, so methods can be compared without reflashing anything. Each philosopher prints the method it received when the table is set:

```
Table initialized (epoch 880422), holding forks 3 | method: Havender resource ordering (no circular wait)
```

## Trade-offs

Preventing deadlock doesn't make every method equally good.

| Method | Cost |
|---|---|
| All at once | Wasted effort: forks bounce between boards, and boards sit out random backoffs. Livelock is avoided by chance, not guaranteed |
| Rosenkrantz | Unfairness: B4 has the lowest priority and gives way to everyone, so under heavy contention it waits the longest. The original method avoids this with timestamps a process keeps across retries, so a waiting process eventually becomes the oldest. The fixed IDs used here don't do that |
| Havender | Asymmetry: B1 follows a different rule than everyone else. Deadlock is impossible, but fairness isn't guaranteed |

## Hunger

`RANDOM_HUNGER` is `true` here, unlike in 01-classical: each philosopher gets hungry after a random 3–8 seconds of thinking, so the table keeps running on its own. The listener's `hungry` command still makes everyone hungry at once, which is the hardest case for any prevention method.

## Showing it

1. Power up the listener and all four philosophers, and wait for `Table set: all four philosophers seated`.
2. Type `method havender`, which resets the table into the deadlock position.
3. Type `hungry`. Instead of deadlocking, the boards eat one after another.
4. Repeat with `method rosenkrantz` and `method all`, and compare who eats first and how long the boards wait.

The listener's deadlock check is still running. With any of these methods, it should never print `DEADLOCK`.

## Settings

Beyond those listed in 01-classical:

| Constant | Value | Purpose |
|---|---|---|
| `PREVENTION_METHOD` (listener) | `'H'` | Method used at startup |
| `RANDOM_HUNGER` | `true` | Random hunger timers on |
| `BACKOFF_MIN_MS` / `BACKOFF_MAX_MS` | 200 / 1500 | All at once: random wait after giving up a fork |

## Testing

In simulation, each method was started from the deadlock position and run for five minutes at 0%, 10%, and 20% packet loss. No method ever deadlocked, and no fork was ever lost or duplicated. Every philosopher kept eating, and the longest any board stayed hungry was 7.8 s (Havender), 8.4 s (all at once), and 9.2 s (Rosenkrantz). Results on the real boards may differ, since the boards don't all run at the same speed.

## References

[1] M. Singhal and N. G. Shivaratri, *Advanced Concepts in Operating Systems: Distributed, Database, and Multiprocessor Operating Systems*. New York: McGraw-Hill, 1994, sec. 3.9.3.

[2] J. W. Havender, "Avoiding deadlock in multitasking systems," *IBM Systems Journal*, vol. 7, no. 2, pp. 74–84, 1968.

[3] D. J. Rosenkrantz, R. E. Stearns, and P. M. Lewis II, "System level concurrency control for distributed database systems," *ACM Transactions on Database Systems*, vol. 3, no. 2, pp. 178–198, 1978.

[4] E. G. Coffman, M. Elphick, and A. Shoshani, "System deadlocks," *ACM Computing Surveys*, vol. 3, no. 2, pp. 67–78, 1971.
