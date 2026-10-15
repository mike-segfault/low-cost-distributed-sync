# FreeRTOS: Race Condition

By Dr. Dan Bennett

A single ESP32 runs ten FreeRTOS tasks that all increment one shared counter **without protecting it**. Each task adds 1,000,000, so the counter should finish at 10,000,000, but it almost always comes out lower. This is the classic race condition, shown on one machine, and it's the problem every later synchronization stage in the project is built to prevent.

Unlike the `communication` stages, everything here happens inside one board. There's no network: the "processes" are FreeRTOS tasks that share memory directly.

## Files

| Folder | Board | Purpose |
|---|---|---|
| `NoCriticalSection` | ESP32 | Ten tasks race to increment an unprotected shared counter |

## How it works

`setup()` creates ten tasks, named `TASK_A` through `TASK_J`, all running `incrementTask()`. They are created with `tskNO_AFFINITY`, so the scheduler can run them on either of the ESP32's two cores. That means tasks can be interrupted by each other on the same core *and* run truly in parallel on different cores.

Each task runs 1,000 outer loops. In each one, it increments `sharedCounter` 1,000 times, then sleeps for a random 1–4 ticks so the tasks interleave unpredictably:

```cpp
for (int i = 0; i < OUTER_LOOPS; ++i) {
  for (int j = 0; j < INNER_LOOPS; ++j) {
    ++sharedCounter;   // not protected
  }
  vTaskDelay(random(1, 5));
}
```

## Why the count comes out low

`++sharedCounter` looks like a single step, but the processor carries it out as three: **read** the value from memory, **add** 1, and **write** it back. The commented-out lines in the sketch spell this out:

```cpp
//a = sharedCounter;
//a = a + 1;
//sharedCounter = a;
```

If two tasks read the same value before either writes it back, both write the same result, and one increment is lost:

| Step | Task A | Task B | `sharedCounter` |
|---|---|---|---|
| 1 | reads 500 | | 500 |
| 2 | | reads 500 | 500 |
| 3 | writes 501 | | 501 |
| 4 | | writes 501 | 501 (should be 502) |

The increment is the **critical section**: code that touches shared data and must not run in more than one task at a time. This sketch deliberately leaves it unprotected, so lost updates pile up across ten million increments, and the total is different on every run.

The `#pragma GCC optimize ("O0")` around `incrementTask()` turns off compiler optimization for that function. Without it, the compiler could keep the counter in a register and write it back only occasionally, which changes how often the race happens. With optimizations off, every increment really is a separate read, add, and write to memory.

## Where the mutex is (and isn't) used

The sketch does create a mutex (`counterMutex`), but only uses it at the very end. Each task takes the mutex to safely add 1 to `finished` and print that it's done. That single protected step is the contrast: the `finished` count is always correct, while the unprotected `sharedCounter` is not. Wrapping the increment itself in `xSemaphoreTake()` and `xSemaphoreGive()` would make the counter correct too, at the cost of tasks waiting on each other.

After finishing, each task idles forever (`vTaskDelay(1)` in a loop). `loop()` checks once a second, and once all ten tasks are done it prints the results.

## Context switch counter

The sketch defines `traceTASK_SWITCHED_IN()`, a FreeRTOS trace hook that runs whenever a task is switched in, to count context switches in `context_switch_count`, and prints the total at the end. The macro is defined before the FreeRTOS headers are included, as the hook requires.

With the Arduino ESP32 core, FreeRTOS comes precompiled, so a trace macro defined in a sketch may never reach the kernel code that calls it. If the output shows `Context Switches: 0`, that is the likely reason, not an absence of switching. Counting switches reliably would require rebuilding FreeRTOS with the hook, for example through ESP-IDF.

## Serial output

```
--- ESP32 Mutex Demonstration Start ---
A is running on core: 0
B is running on core: 1
...
A All Finished
...
J All Finished
All tasks Done!
 counter = <less than 10000000>
Should be 10000000
ERRORS!!!

Context Switches: <count>
```

The final counter is different every run. Getting exactly 10,000,000 is possible but unlikely.

## Running it

Flash `NoCriticalSection` to an ESP32 and open the Serial Monitor at 9600 baud. No wiring is needed. The tasks take several seconds to finish, after which the results print once.
