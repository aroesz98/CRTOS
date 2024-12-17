#include <cstdint>
#include <cstdio>

#include <RTOS.hpp>

void f1(void *args);
void f2(void *args);
void f3(void *args);
void f4(void *args);

RTOS::Semaphore sem1(0);
RTOS::Semaphore sem2(0);
RTOS::Semaphore sem3(0);
RTOS::Semaphore sem4(0);

RTOS::Task::TaskHandle th1 = nullptr;
RTOS::Task::TaskHandle th2 = nullptr;
RTOS::Task::TaskHandle th3 = nullptr;
RTOS::Task::TaskHandle th4 = nullptr;

typedef struct
{
    char data[20];
    uint32_t size = 20u;
} msg;

RTOS::Queue* rt_queue;
RTOS::CircularBuffer circ(100);

void f1(void *args)
{
    msg m;
    while(1)
    {
        printf("Task name: %s\r\n", RTOS::Task::GetCurrentTaskName());
        m.size = snprintf(m.data, 16,"%s message: %u\r\n", RTOS::Task::GetCurrentTaskName(), rand());
        rt_queue->Send(&m, 0);
        sem1.wait(1600);
        m.size = snprintf(m.data, 16,"%s message: %u\r\n", RTOS::Task::GetCurrentTaskName(), rand());
        rt_queue->Send(&m, 0);
        sem1.wait(400);
    }
}

void f2(void *args)
{
    msg m;
    uint8_t buf[20];
    while(1)
    {
        printf("Task name: %s\r\n", RTOS::Task::GetCurrentTaskName());
        RTOS::Result t = rt_queue->Receive(&m, 300);
        if(t == RTOS::Result::RESULT_SUCCESS)
        {
            printf("Queue Received: %s\r\n", m.data);
        }
        snprintf((char*)&buf[0], 30, "CircBuff: %u", rand() % 0xFFFF);
        circ.put(&buf[0], 20, 0);
        sem2.wait(2000);
    }
}

void f3(void *args)
{
    RTOS::Task::Create(f4, "D Task", 128, NULL, 1, &th4);
    uint8_t buf[20];
    while(1)
    {
        printf("Task name: %s\r\n", RTOS::Task::GetCurrentTaskName());
        if (circ.get(&buf[0], 20, 200) == RTOS::Result::RESULT_SUCCESS)
        {
            printf("Circ Buffer Received: %s\r\n", (char*)&buf[0]);
        }
        sem3.wait(2000);
    }
}

void f4(void *args)
{
    while(1)
    {
        printf("Task name: %s\r\n", RTOS::Task::GetCurrentTaskName());
        sem4.wait(1500);
        RTOS::Task::Delete(&th4);
    }
}

void myTimerCallback(void *args)
{
    printf("Timer expired!\r\n");
    printf("Free Memory: %u bytes\r\n", RTOS::Config::getFreeMemory());
    printf("Allocated Memory: %u bytes\r\n", RTOS::Config::getAllocatedMemory());
}

/*
 * @brief   Application entry point.
 */
int main(void)
{
    uint8_t* mp = new uint8_t[16384];
    RTOS::Config::initMem(mp, 16384);

    RTOS::Config::SetCoreClock(150000000u);
    RTOS::Config::SetTickRate(1000u);

    rt_queue = new RTOS::Queue(20, sizeof(msg));
    circ.init();

    RTOS::Task::Create(f1, "A Task", 128, NULL, 2, &th1);
    RTOS::Task::Create(f2, "B Task", 128, NULL, 3, &th2);
    RTOS::Task::Create(f3, "C Task", 280, NULL, 1, &th3);

    RTOS::Timer::SoftwareTimer myTimer;
    RTOS::Timer::Init(&myTimer, 1000, myTimerCallback, nullptr, true);
    RTOS::Timer::Start(&myTimer);

    RTOS::Scheduler::Start();

    delete []mp;

    return 0;
}