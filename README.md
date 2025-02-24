# CRTOS
Custom Real-Time Operating System for ARM Cortex-M

# CRTOS: Technical Documentation

## Table of Contents
1. [Overview](#overview)
2. [Functional Description](#functional-description)
3. [Algorithm Description](#algorithm-description)
4. [Usage Examples](#usage-examples)

## Overview
CRTOS is a real-time operating system designed for embedded systems. This documentation covers the core functionalities, algorithms, and usage examples of CRTOS. The system provides features such as task management, mutexes, semaphores, timers, circular buffers, queues, and CRC32 calculation. 

## Functional Description

### Namespaces and Enums
- **Namespace `CRTOS`:** Contains the core functionalities of CRTOS.
- **Namespace `Config`:** Configuration settings such as core clock and tick rate.
- **Namespace `Task`:** Task management features.
- **Namespace `Scheduler`:** Scheduler control.
- **Namespace `Timer`:** Software timer functionalities.
- **Namespace `CRC32`:** CRC32 calculation.
- **Enum `Result`:** Standard result codes for CRTOS operations.
- **Enum `TaskState`:** Represents the state of a task.

### Classes
- **Class `Semaphore`:** Binary semaphore for synchronization.
- **Class `Mutex`:** Mutual exclusion mechanism.
- **Class `Queue`:** Implements a fixed-size queue.
- **Class `CircularBuffer`:** Implements a circular buffer.

## Algorithm Description

### Task Management
Tasks are represented by the `TaskControlBlock` structure. Task switching relies on saving and restoring the context of tasks using assembly code. Tasks can be created, deleted, delayed, paused, and resumed.

#### Task Switching
1. **Context Saving:** The current task context is saved by pushing its registers onto its stack.
2. **Context Loading:** The next task's context is restored by popping its registers from its stack.
3. **PendSV_Handler:** This handler performs the context switch and updates the system tick.

### Mutex
Mutexes are used to ensure mutual exclusion. The `Mutex` class uses atomic operations for locking and unlocking.

#### Locking Algorithm
1. **Interrupt Masking:** Disables interrupts to protect the critical section.
2. **Spin-lock:** Uses a loop to wait until the lock is available.
3. **Memory Barriers:** Ensures proper ordering of operations.

### Semaphore
Semaphores provide signaling mechanisms. The `Semaphore` class supports wait and signal operations.

#### Wait Algorithm
1. **Critical Section:** Disables interrupts and checks the semaphore value.
2. **Timeout Management:** If the semaphore is not available, it enters a blocked state and waits for the timeout.
3. **Signal Handling:** Signals the semaphore and releases the waiting task if the semaphore value becomes positive.

### Queue
Queues provide a fixed-size first-in, first-out (FIFO) buffer. The `Queue` class supports sending and receiving items.

#### Send Algorithm
1. **Critical Section:** Disables interrupts and checks if the queue is full.
2. **Insertion:** Inserts the item at the rear of the queue and updates the rear index.
3. **Semaphore Signal:** Signals the semaphore to unblock waiting tasks.

### Circular Buffer
Circular buffers provide a fixed-size buffer with wrap-around behavior. The `CircularBuffer` class supports sending and receiving data.

#### Send Algorithm
1. **Critical Section:** Disables interrupts and checks if there is enough space in the buffer.
2. **Wrap-around:** Copies data to the buffer, handling wrap-around if necessary.
3. **Semaphore Signal:** Signals the semaphore to unblock waiting tasks.

### CRC32 Calculation
The `CRC32` class provides methods to initialize, calculate, and deinitialize the CRC32 table.

#### Calculation Algorithm
1. **Table Initialization:** Generates a lookup table for CRC32 calculation using a polynomial.
2. **CRC Update:** Updates the CRC value for each byte of data using the lookup table.
3. **Final XOR:** Performs a final XOR operation on the CRC value.

## Usage Examples

### Creating and Running Tasks
```cpp
void MyTaskFunction(void *params) {
    while (true) {
        // Task code
    }
}

int main(void) {
    CRTOS::Config::SetCoreClock(150000000); // Set core clock to 150 MHz
    CRTOS::Config::SetTickRate(1000); // Set tick rate to 1000 ticks/second
    
    void* memoryPool = malloc(1024 * 1024); // Allocate memory pool
    CRTOS::Config::InitMem(memoryPool, 1024 * 1024); // Initialize memory pool
    
    CRTOS::Task::TaskHandle taskHandle;
    CRTOS::Task::Create(MyTaskFunction, "MyTask", 256, nullptr, 1, &taskHandle); // Create task
    
    CRTOS::Scheduler::Start(); // Start scheduler
    
    return 0;
}
```

### Using Mutex
```cpp
void Task1(void *params) {
    CRTOS::Mutex myMutex;
    myMutex.Lock();
    // Critical section
    myMutex.Unlock();
}

void Task2(void *params) {
    CRTOS::Mutex myMutex;
    myMutex.Lock();
    // Critical section
    myMutex.Unlock();
}
```

### Using Semaphore
```cpp
void TaskProducer(void *params) {
    CRTOS::Semaphore mySemaphore;
    while (true) {
        // Produce item
        mySemaphore.Signal();
    }
}

void TaskConsumer(void *params) {
    CRTOS::Semaphore mySemaphore;
    while (true) {
        mySemaphore.Wait(100); // Wait for item
        // Consume item
    }
}
```

### Using Queue
```cpp
void TaskProducer(void *params) {
    CRTOS::Queue myQueue(10, sizeof(int));
    int item = 42;
    myQueue.Send(&item);
}

void TaskConsumer(void *params) {
    CRTOS::Queue myQueue(10, sizeof(int));
    int item;
    myQueue.Receive(&item, 100);
}
```

### Using Circular Buffer
```cpp
void TaskProducer(void *params) {
    CRTOS::CircularBuffer myBuffer(10);
    myBuffer.Init();
    uint8_t data[] = {1, 2, 3, 4, 5};
    myBuffer.Send(data, 5);
}

void TaskConsumer(void *params) {
    CRTOS::CircularBuffer myBuffer(10);
    uint8_t data[5];
    myBuffer.Receive(data, 5, 100);
}
```

### Calculating CRC32
```cpp
void CalculateCRC(void) {
    uint8_t data[] = {1, 2, 3, 4, 5};
    uint32_t crc;
    CRTOS::CRC32::Init();
    CRTOS::CRC32::Calculate(data, sizeof(data), crc);
    CRTOS::CRC32::Deinit();
}
```

---

