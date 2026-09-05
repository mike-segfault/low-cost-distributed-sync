# Distributed Process Synchronization Represented on Low-Cost Hardware
This project examines the topic of distributed process synchronization with the attempt of representing the idea on low-cost hardware. The goal is to understand process synchronization at a lower level, while also examining and navigating the limitations of a low-cost hardware representation.

## Objectives

- Better understand process synchronization at a lower level.
- Implement communication between embedded devices.
- Explore distributed synchronization techniques.
- Investigate process synchronization and mutual exclusion.
- Demonstrate race conditions and critical section protection.
- Examine the practical limitations of low-cost embedded hardware.
- Recreate and analyze classical computer science synchronization problems on physical hardware.

## Technologies
### Languages
- C
- C++
- Arduino Framework / ArduinoIDE
### Hardware
- ESP32 microcontrollers
- ESP8226MOD microcontrollers
- Breadboards and supporting components
- USB-to-Serial interfaces for programming and monitoring
### Software Libraries
- ESP-NOW
- WiFi
- FreeRTOS

## Planned Work
### Future work includes:
- Distributed mutex implementations
- Distributed semaphore mechanisms
- Producer-consumer systems
- Dining Philosophers problem
- Cigarette Smokers problem
- Additional synchronization experiments on constrained hardware
- Evaluation of synchronization performance and scalability across multiple nodes

## Refrences
1. Gupta. *Achieving Low Cost Synchronization in a Multiprocessor System*. Future Generation Computer Systems, Vol. 6, 1990. DOI: https://doi.org/10.1016/0167-739X(90)90023-7
2. Kopetz, Hermann. *Real-Time Systems*. Springer US, 1997. Available through EBSCO: https://research.ebsco.com/plink/b79d19d5-425c-3923-882c-0a426c8165dd
3. Lipovski, G. Jack. *Single and Multi-Chip Microcontroller Interfacing: For the Motorola 6812*. Academic Press, 1999. Available through EBSCO: https://research.ebsco.com/plink/59f0289b-fa6e-3891-9082-e73ef07bce15
4. Munir, Arslan, et al. *Modeling and Optimization of Parallel and Distributed Embedded Systems*. John Wiley & Sons, 2016. https://ebookcentral.proquest.com/lib/clarion-ebooks/detail.action?docID=4305723
5. Patil, Suhas S. *Limitations and Capabilities of Dijkstra's Semaphore Primitives for Coordination among Processes*. Group Memo 57, February 1971. https://wiki.eecs.yorku.ca/course_archive/2014-15/W/6490A/_media/public:patil.pdf
6. ROHINI College of Engineering and Technology. *Distributed Embedded Systems*. https://www.rcet.org.in/uploads/academics/rohini_85806983944.pdf
7. Silberschatz, Avi, Peter Baer Galvin, and Greg Gagne. *Operating System Concepts*. Chapters 6-8. John Wiley & Sons, 2018.
8. Thompson, Michael and Daniel Bennett. *Cigarette Smokers Problem*. April 2026. https://mirkwood.cs.edinboro.edu/~bennett/class/cmsc4000/spring2026/notes/smokers.html
