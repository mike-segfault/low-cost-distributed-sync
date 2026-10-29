# Dining Philosophers: Problem Selection and Implementation Approach

This document explains why the Dining Philosophers problem was chosen as the classical synchronization problem for this project. It also explains the reasoning behind implementing it across a network of ESP32 and ESP8266MOD boards.

---

## Why Dining Philosophers

The roadmap called for one classical problem to represent deadlock. Dining Philosophers was chosen because:

**It is the canonical deadlock problem.** Dijkstra introduced the problem as an example of processes competing for shared resources. In that setting, a symmetric strategy, where every process acts identically, can leave the whole system stuck. Sir Tony Hoare later gave the problem its standard form: five philosophers at a round table with one fork between each pair of neighbors. Each philosopher needs both adjacent forks to eat. If every philosopher picks up their left fork at the same moment, each waits forever on their right fork. That circular wait is deadlock in its simplest form. The problem also exposes starvation, where a philosopher never gets to eat even though the system as a whole keeps making progress.

**It is extensively documented.** The problem has been studied since the early 1970s, so there is a deep body of peer-reviewed literature on it, including several well-understood solutions. This lets the project focus on representation and hardware limitations rather than on proving correctness from scratch.

**It has an established distributed form.** Chandy and Misra (1984) restated Dining Philosophers for distributed systems, where processes share no memory and communicate only by messages. In their solution, forks are passed between neighbors as messages. A request token is also exchanged so that a philosopher can ask a neighbor for a fork. This builds directly on the token-based mutual exclusion already implemented in this repo.

**It is easy to present.** Each philosopher's state (thinking, hungry, or eating) can be shown with LEDs. A deadlock is visible immediately: every board is waiting and none are eating. This makes the problem easy to demonstrate to an audience without reading logs.

---

## From Token Mutual Exclusion to Dining Philosophers

The two completed distributed synchronization stages provide the foundation:

1. **Centralized token ring.** The central board creates a single token, and boards pass it around a ring. Only the token holder may run its command sequence.
2. **Decentralized Suzuki-Kasami-style token.** Boards request the token by broadcast, and the current holder passes it on. This follows Suzuki and Kasami (1985), with project-specific modifications to the token queue.

Both stages use one global token, so only one board can be in its critical section at a time. Dining Philosophers changes this in two ways:

- **There are many tokens instead of one.** Each fork is its own token, shared by exactly two neighboring boards.
- **A board needs two tokens to proceed.** A board may run its critical section only while holding both of its forks.

This is the point where deadlock becomes possible. With one global token, circular waiting cannot occur. With one token per fork and a requirement to hold two, it can.

---

## Why the Classical Problem Cannot Be Represented Directly

The classical Dining Philosophers problem is an abstraction, and several of its assumptions have no physical equivalent on a network of low-cost microcontrollers. A faithful one-to-one representation is practically impossible.

**There are no shared forks.** In the classical problem, a fork sits on the table between two philosophers, and picking it up is a single atomic action. The boards in this project share no memory, so there is no table for a fork to sit on. A fork can only exist as a token held by one board. Acquiring it means exchanging messages with a neighbor, which takes time and can fail. A step the classical problem treats as instantaneous becomes a multi-message protocol.

**There is no global view of the table.** The classical problem lets an observer see every philosopher's state at once. On the hardware, each board knows only its own state and what its messages tell it. Messages are in flight between boards, so no board, including the central sender, can observe the whole table at a single instant.

**Timing is not arbitrary.** The classical deadlock relies on every philosopher becoming hungry and picking up their left fork at the same moment. On real hardware, boards run at different speeds and messages arrive at different times. The exact interleaving that causes deadlock is unlikely to happen on its own, even though the protocol allows it.

**The hardware can fail in ways the abstraction ignores.** Philosophers in the classical problem never vanish mid-meal, and forks are never lost. Boards can lose power or reset, and wireless messages can be dropped. If the message carrying a fork is lost, that fork disappears from the table entirely.

**The table size is set by the hardware.** The classical formulation uses five philosophers. This network has four receiver boards, so the table has four philosophers and four forks.

These gaps are not unique to this project. Gimbert et al. (2025) show that deciding whether deadlock can be avoided in a general lock-sharing system is undecidable, even when the number of processes and locks is fixed. The problem becomes decidable only under restrictions, the first of which is that each process uses at most two locks. Dining Philosophers is the standard example of that restricted case. Restrictions are therefore not a compromise unique to low-cost hardware. They are what makes the problem tractable to reason about in the first place.

---

## Prerequisite Limitations

The goal is to keep the representation as close to the classical problem as possible. A limitation is included only if the problem cannot run correctly on the hardware without it. Each candidate limitation was tested against one question: does the classical problem already assume this, or does the hardware force it? Anything the classical problem already guarantees, or allows, was left out. Two limitations remain.

### 1. Fork conservation

Every fork is held by exactly one board at all times, from the moment the table is initialized through every transfer. A fork transfer is acknowledged and retried until confirmed, so a fork is never duplicated or lost in transit.

**Why it is required.** In the classical problem, forks are physical objects on a table. They cannot be copied or lost, and an unheld fork simply rests on the table. None of this is true on the hardware. There is no table, so a fork that no board holds does not exist. A dropped message would remove a fork from the table, and a resent message without acknowledgment could create a second copy. Either case breaks the problem: a missing fork starves both of its neighbors forever, and a duplicated fork lets two neighbors eat at once, violating mutual exclusion. Fork conservation restores the property the classical problem gets for free from physical objects.

This limitation also explains why forks start out held rather than free. Because there is no table to rest on, the central sender assigns each fork to a board before the table starts. Stage 2 and Stage 3 both rely on this initial placement as part of their algorithms, so it adds no new behavior beyond what the solutions already require.

### 2. No board failures during a run

All boards are assumed to stay powered and connected for the duration of a run.

**Why it is required.** If a board resets while holding a fork, that fork is lost and fork conservation can no longer hold. Detecting the failure and recovering the fork would require failure detection and recovery mechanisms, which turn the project into a fault-tolerance problem rather than a synchronization problem. This limitation does not change the classical problem, because the classical problem already assumes it: philosophers never leave the table mid-meal. It is listed explicitly because, unlike an abstract philosopher, a physical board can fail.

### Candidates that were not included

Several other restrictions were considered and rejected because the classical problem already covers them.

- **Two neighbors and two forks per board.** This is the definition of Dining Philosophers, not a restriction placed on it. The ring topology is the problem itself.
- **Bounded eating time.** The classical problem assumes philosophers eat for a finite time. Each board's critical section is a finite LED command sequence, so this already holds without any added rule.
- **Four philosophers instead of five.** The problem and its solutions are defined for any number of philosophers. Five is only the traditional example, so a table of four is still the classical problem.
- **Triggering hunger on every board at once.** When philosophers become hungry is outside their control in the classical problem; it is decided by the environment. A central signal that makes every board hungry at the same moment is the environment choosing one schedule the problem already allows. It does not change any philosopher's rules, so it is a way of testing the problem rather than a limitation on it. It is used in Stage 1 to make the deadlock repeatable.

---

## Mapping the Problem onto the Hardware

| Dining Philosophers | Hardware representation |
|---|---|
| Philosopher | Receiver board (ESP32 or ESP8266MOD) |
| Fork | Token shared between two neighboring boards |
| Thinking | Board idle, holding no forks it needs |
| Hungry | Board requesting forks; waiting LED on |
| Eating (critical section) | Board holding both forks and running its LED command sequence; token LED on |
| Table | Logical ring of boards defined by board ID |

The extra LEDs added during the token stages carry over directly. The waiting LED shows that a board is hungry, and the token LED shows that it is eating. The three color LEDs continue to show the command sequence being executed, so the critical section remains observable.

The central ESP32 sender does not act as a philosopher. Its role is to initialize the table, meaning it assigns the initial fork placement, and to dispatch commands. Once the table is initialized, fork exchanges happen directly between neighboring boards.

---

## Implementation Plan

The implementation is staged so that the problem is demonstrated before it is solved.

### Stage 1: Naive solution (demonstrate deadlock)

Each board requests its left fork, then its right fork, and holds whatever it has acquired while waiting. To make this repeatable, the central sender triggers hunger on every board at once. This is a schedule the classical problem already allows (see Candidates that were not included). Each board holds one fork and waits on the other. Every waiting LED stays on, and no board ever eats. This stage exists to make deadlock visible on real hardware.

### Stage 2: Resource ordering (Dijkstra)

Forks are numbered, and each board always requests its lower-numbered fork first. This breaks the circular wait, so deadlock cannot occur. Board IDs already provide a natural ordering, so this requires little change to the existing addressing scheme. Resource ordering prevents deadlock, but it does not guarantee fairness between boards.

### Stage 3: Chandy-Misra (hygienic solution)

Each fork is marked clean or dirty. A fork becomes dirty after it is used to eat. A board that holds a dirty fork must give it up when a neighbor requests it, and it cleans the fork before sending it. A board holding a clean fork may keep it until it has eaten. This rule guarantees that every hungry philosopher eventually eats, preventing both deadlock and starvation. It also fits the existing message-passing design, because fork transfers and fork requests are both ordinary messages between boards.

---

## Hardware Considerations

### Heterogeneous boards

The network mixes ESP32 and ESP8266MOD boards, and the difference matters for this problem.

- **The ESP32** is dual-core and runs FreeRTOS, so tasks can be preempted and synchronized with RTOS primitives such as mutexes.
- **The ESP8266**, under the Arduino core, has a single core and runs a cooperative main loop without FreeRTOS tasks.

Because the boards share no memory and do not share a common RTOS, synchronization between them cannot rely on mutexes or semaphores. Message passing is the only coordination mechanism available across the whole network. This is the defining condition of distributed synchronization, and it is the condition the Chandy-Misra solution was designed for.

The boards also differ in processing speed. A faster board may request and release forks more often than a slower neighbor. This asymmetry makes the fairness difference between Stage 2 and Stage 3 observable on real hardware.

### Communication

Fork transfers reuse the existing communication layer: broadcast and addressed messages with acknowledgment and retry. Because a fork is a token, losing a fork message would effectively remove that fork from the table. The ACK and retry behavior from earlier stages is therefore required for correctness, not just reliability.

### Board addressing

The original 3-bit one-hot board addressing supports three boards (`001`, `010`, `100`). With a fourth receiver board in the network, addressing moves to the 4-bit machine ID sequence listed on the roadmap. Board IDs double as the ordering used for fork numbering in Stage 2 and for initial fork placement in Stage 3.

---

## References

1. Patil, Suhas S. *Limitations and Capabilities of Dijkstra's Semaphore Primitives for Coordination among Processes*. Group Memo 57, February 1971. https://wiki.eecs.yorku.ca/course_archive/2014-15/W/6490A/_media/public:patil.pdf.

2. Hugo, Gimbert, et al. *Distributed Controller Synthesis for Deadlock Avoidance.* arXiv.org, 2025. ProQuest, https://go.openathens.net/redirector/pennwest.edu?url=https://www.proquest.com/working-papers/distributed-controller-synthesis-deadlock/docview/3302629657/se-2, doi:https://doi.org/10.46298/lmcs-21(3:24)2025.

3. Krzysztof R. Apt, and Hoare, Tony, "Concurrent Algorithms," in *Edsger Wybe Dijkstra: His Life, Work, and Legacy*, K. R. Apt and T. Hoare, Eds. ACM Books, 2022, sec. 4.2.5.
