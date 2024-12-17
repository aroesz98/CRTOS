# CRTOS
Custom Real-Time Operating System for ARM Cortex-M

# Technical Documentation of RTOS Implementation

## Table of Contents
1. **Functional Overview**
2. **Task Scheduler Functionality**
3. **Algorithms Used**
4. **Usage Examples**

---

### 1. Functional Overview

This CRTOS implementation provides several core functionalities:

- **RTOS Configuration:**
  - `SetCoreClock(uint32_t clock)`: Sets the core clock frequency.
  - `SetTickRate(uint32_t ticks)`: Sets the system tick rate.
  - `initMem(void *pool, uint32_t size)`: Initializes the memory pool for RTOS.
  - `getFreeMemory(void)`: Returns the amount of free memory.
  - `getAllocatedMemory(void)`: Returns the amount of allocated memory.

- **Semaphores:**
  - `Semaphore(uint32_t initialValue)`: Constructor initializing a semaphore with a specified initial value.
  - `wait(uint32_t timeoutTicks)`: Waits for a semaphore with a specified timeout.
  - `signal(void)`: Signals a semaphore.

- **Mutexes:**
  - `Mutex(void)`: Constructor initializing a mutex.
  - `lock(void)`: Locks the mutex.
  - `unlock(void)`: Unlocks the mutex.

- **Queues:**
  - `Queue(uint32_t maxsize, uint32_t element_size)`: Constructor initializing a queue with a specified max size and element size.
  - `Send(void* item, uint32_t timeout)`: Sends an item to the queue.
  - `Receive(void* item, uint32_t timeout)`: Receives an item from the queue.

- **Circular Buffers:**
  - `CircularBuffer(uint32_t buffer_size)`: Constructor initializing a circular buffer with a specified size.
  - `init(void)`: Initializes the circular buffer.
  - `put(const uint8_t* data, uint32_t size, uint32_t timeout_ms)`: Puts data into the circular buffer.
  - `get(uint8_t* data, uint32_t size, uint32_t timeout_ms)`: Gets data from the circular buffer.

- **Tasks:**
  - `Create(void (*function)(void *), const char * const name, uint32_t stackDepth, void *args, uint32_t prio, TaskHandle *handle)`: Creates a new task.
  - `Delete(void)`: Deletes the currently executing task.
  - `Delete(TaskHandle *handle)`: Deletes a task specified by the handle.
  - `GetCurrentTaskName(void)`: Returns the name of the currently executing task.

- **Timer Management:**
  - `Init(SoftwareTimer *timer, uint32_t timeoutTicks, void (*callback)(void*), void *callbackArgs, bool autoReload)`: Initializes a software timer.
  - `Start(SoftwareTimer *timer)`: Starts a software timer.
  - `Stop(SoftwareTimer *timer)`: Stops a software timer.

---

### 2. Task Scheduler Functionality

The CRTOS Scheduler is responsible for managing the execution of tasks based on their priorities. Key elements of the scheduler's operation are:

- **Initialization**: The scheduler is initialized by setting the clock frequency and memory allocation.
- **Context Switching**: The implementation provides context switching between tasks using the PendSV interrupt.
- **Task Management**: The scheduler manages task execution order based on their priorities.
- **System Interrupt Handling**: The scheduler responds to system interrupts such as SysTick and PendSV for context switching.

The context switching algorithm involves:
1. Saving the context of the currently executing task.
2. Selecting the next task with the highest priority.
3. Restoring the context of the selected task.

---

### 3. Algorithms Used

#### a. Bubble Sort in SortListByPriority
The bubble sort algorithm is used to sort the task list by priority:
- **Description**: Neighboring list elements are compared and swapped if they are in the wrong order.
- **Complexity**: O(n^2) in the worst case.

#### b. FIFO in Queue Operations
The algorithms used in the queue operations are based on the FIFO (First In, First Out) principle:
- **Send**: Adds a new item to the end of the queue.
- **Receive**: Retrieves an item from the beginning of the queue.
- **Complexity**: O(1) for both operations due to the circular buffer implementation.

#### c. Circular Buffer Algorithm
The circular buffer algorithm allows efficient data management in a limited buffer:
- **put**: Adds data to the buffer by updating the head pointer.
- **get**: Retrieves data from the buffer by updating the tail pointer.
- **Complexity**: O(1) for both operations.

---

### 4. Usage Examples

#### Semaphore Example
```cpp
CRTOS::Semaphore mySemaphore(1);

void exampleTask(void *) {
    if (mySemaphore.wait(1000) == CRTOS::Result::RESULT_SUCCESS) {
        // Critical section
        mySemaphore.signal();
    }
}
```

#### Queue Example
```cpp
CRTOS::Queue myQueue(10, sizeof(int));

void producerTask(void *) {
    int item = 42;
    myQueue.Send(&item, 1000);
}

void consumerTask(void *) {
    int item;
    myQueue.Receive(&item, 1000);
}
```

#### Circular Buffer Example
```cpp
CRTOS::CircularBuffer myBuffer(128);

void bufferTask(void *) {
    uint8_t data[] = {1, 2, 3, 4, 5};
    myBuffer.put(data, sizeof(data), 1000);

    uint8_t retrievedData[5];
    myBuffer.get(retrievedData, sizeof(retrievedData), 1000);
}
```

#### Task Creation and Deletion Example
```cpp
void myTask(void *) {
    // Task code here
}

void main() {
    CRTOS::TaskHandle taskHandle;
    CRTOS::Task::Create(myTask, "MyTask", 1024, nullptr, 5, &taskHandle);

    // Delete the task later
    CRTOS::Task::Delete(&taskHandle);
}
```

---
