# FreeRTOS: Mutex

By Dr. Dan Bennett

A single ESP32 runs four FreeRTOS tasks that all access one shared counter, but unlike the race condition example, every access is protected by a **mutex**. Only one task can enter the critical section at a time, so the counter is updated safely and consistently regardless of how the scheduler interleaves the tasks.

This sketch demonstrates the classic solution to the race condition problem. Rather than allowing multiple tasks to modify shared data simultaneously, each task must acquire a mutex before entering the critical section and release it when finished.

Unlike the `communication` stages, everything here happens inside one board. The tasks share memory directly, and synchronization is provided entirely by FreeRTOS.

## Files

| Folder | Board | Purpose |
|---|---|---|
| `ESP32-Semaphore` | ESP32 | Four tasks safely share a counter using a FreeRTOS mutex |

## How it works

`setup()` creates a mutex with `xSemaphoreCreateMutex()` and then launches four tasks, `TASK_A` through `TASK_D`. Each task receives a unique letter identifier and runs the same function, `incrementTask()`.

The tasks are created with `tskNO_AFFINITY`, meaning the FreeRTOS scheduler can run them on either of the ESP32's two cores. Multiple tasks may be ready to run at the same time, but only one can own the mutex.

Each task repeatedly attempts to acquire the mutex:

```cpp
if (xSemaphoreTake(counterMutex, portMAX_DELAY) == pdTRUE) {
```

Using `portMAX_DELAY` causes a task to wait indefinitely until the mutex becomes available. Once a task acquires the mutex, it enters the critical section and gains exclusive access to the shared counter.

Inside the protected region, the task:

1. Prints the current counter value.
2. Increments the counter.
3. Holds the mutex briefly.
4. Prints the updated value.
5. Releases the mutex.

```cpp
Serial.print("[Task ");
Serial.print(letter);
Serial.print("] Lock acquired. Counter before: ");
Serial.println(sharedCounter);

sharedCounter++;

vTaskDelay(pdMS_TO_TICKS(100));

Serial.print("\t[Task ");
Serial.print(letter);
Serial.print("] Counter after: ");
Serial.println(sharedCounter);

xSemaphoreGive(counterMutex);
```

After releasing the mutex, the task sleeps for a random length of time before competing for the lock again. This staggered delay makes the lock acquisition order appear unpredictable in the Serial Monitor.

## Why the counter stays correct

Incrementing a variable is not actually a single processor operation. Conceptually, it consists of three steps:

```cpp
//a = sharedCounter;
//a = a + 1;
//sharedCounter = a;
```

In the race condition example, two tasks can perform these steps simultaneously and overwrite each other's updates.

With a mutex, only one task is allowed into the critical section at a time:

| Step | Task A | Task B | Mutex |
|---|---|---|---|
| 1 | Acquires lock | Waiting | Owned by A |
| 2 | Reads and updates counter | Waiting | Owned by A |
| 3 | Releases lock | Waiting | Free |
| 4 | | Acquires lock | Owned by B |
| 5 | | Reads and updates counter | Owned by B |

Because the second task cannot access the shared variable until the first has finished, no updates are lost. Every increment is accounted for, and the counter always increases sequentially.

## The critical section

The critical section is the portion of code that accesses the shared resource:

```cpp
sharedCounter++;
```

In this example, the surrounding mutex operations enforce **mutual exclusion**, guaranteeing that only one task executes the critical section at any moment.

```cpp
xSemaphoreTake(counterMutex, portMAX_DELAY);

/* critical section */

xSemaphoreGive(counterMutex);
`*`

This pattern is one of the most*common synchronization techniques *n FreeRTOS and operating systems i* general.

## Contention and sched*ling

Although four tasks are runn*ng concurrently, they spend much o* their time waiting.

When a task *alls:

```cpp
xSemaphoreTake(count*rMutex, portMAX_DELAY);
```

it ei*her:

- acquires the mutex immedia*ely if it is available, or
- block* until the current owner releases *t.

The `vTaskDelay()` call after *eleasing the mutex intentionally c*eates gaps between attempts:

```c*p
vTaskDelay(pdMS_TO_TICKS(delay))*
```

The random delay causes diff*rent tasks to acquire the lock at different times, making the execution order visible in the Serial output. Which task acquires the mutex next depends entirely on timing and scheduler decisions.

## Dynamic task parameters

Each task receives a letter (`A`, `B`, `C`, or `D`) through dynamically allocated memory:

```cpp
char *data = static_cast<char*>(pvPortMalloc(sizeof(char)));
*data = PNames[i];
```

The pointer is passed as the task parameter when the task is created:

```cpp
xTaskCreatePinnedToCore(
    incrementTask,
    taskName,
    2048,
    data,
    1,
    NULL,
    tskNO_AFFINITY
);
```

Inside `incrementTask()`, the parameter is recovered and used when printing task activity:

```cpp
char *letterPtr = (char *) pvParameters;
char letter{*letterPtr};
```

This allows all tasks to execute the same code while still identifying themselves in the Serial output.

## Serial output

```text
--- ESP32 Mutex Demonstration Start ---
[Task A] Lock acquired. Counter before: 0
    [Task A] Will Delay 6321 Counter after: 1

[Task C] Lock acquired. Counter before: 1
    [Task C] Will Delay 2174 Counter after: 2

[Task B] Lock acquired. Counter before: 2
    [Task B] Will Delay 8417 Counter after: 3

[Task D] Lock acquired. Counter before: 3
    [Task D] Will Delay 1268 Counter after: 4

...
```

The order varies from run to run, but one thing remains constant: the counter increases by exactly one each time a task enters the critical section.

You should never see duplicated counter values or skipped increments because the mutex prevents concurrent access.

## Running it

Flash `ESP32-Semaphore.ino` from the `02-mutex` directory to an ESP32 and open the Serial Monitor at 9600 baud.

No external wiring is required. Once running, the Serial Monitor will show tasks repeatedly competing for the mutex, incrementing the shared counter, and releasing the lock. The output continues indefinitely, demonstrating safe shared-resource access under FreeRTOS.