# Project Roadmap

## Completed

- [x] ESP-NOW sender implementation
- [x] ESP-NOW receiver implementation
- [x] FreeRTOS mutex demonstration
- [x] Race condition demonstration
- [x] Context switch instrumentation
- [x] Broadcasting communication between boards
- [x] Multi-node communication testing
- [x] 3-bit sequences for activating lights off broadcasting/received message
- [x] 4-bit sequence for machine ID

### Distributed Synchronization
- [x] Centralized token ring (central creates token, boards pass in ring)
- [x] Decentralized Suzuki-Kasami-style token

---

## In Progress

- [ ] Methodical Stepthrough (pseudocode probably)
- [ ] Libraries
- [ ] Synchronization performance measurements
- [ ] Selecting deadlock problem to represent

---

## Planned
- [ ] Utilize GitDiagram to visualize repo structure

---

### Classical Synchronization Problems
- [ ] Dining Philosophers mapped onto boards (forks as tokens between neighbors)
