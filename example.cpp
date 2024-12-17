#include <cstdint>
#include <cstdio>

#include <CRTOS.hpp>

void f1(void *args);
void f2(void *args);
void f3(void *args);
void f4(void *args);

CRTOS::Semaphore sem1(0);
CRTOS::Semaphore sem2(0);
CRTOS::Semaphore sem3(0);
CRTOS::Semaphore sem4(0);

CRTOS::Task::TaskHandle th1 = nullptr;
CRTOS::Task::TaskHandle th2 = nullptr;
CRTOS::Task::TaskHandle th3 = nullptr;
CRTOS::Task::TaskHandle th4 = nullptr;

typedef struct
{
    char data[20];
    uint32_t size = 20u;
} msg;

CRTOS::Queue* rt_queue;
CRTOS::CircularBuffer circ(100);

void f1(void *args)
{
    msg m;
    while(1)
    {
        printf("Task name: %s\r\n", CRTOS::Task::GetCurrentTaskName());
        m.size = snprintf(m.data, 16,"%s message: %u\r\n", CRTOS::Task::GetCurrentTaskName(), rand());
        rt_queue->Send(&m, 0);
        sem1.wait(1600);
        m.size = snprintf(m.data, 16,"%s message: %u\r\n", CRTOS::Task::GetCurrentTaskName(), rand());
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
        printf("Task name: %s\r\n", CRTOS::Task::GetCurrentTaskName());
        CRTOS::Result t = rt_queue->Receive(&m, 300);
        if(t == CRTOS::Result::RESULT_SUCCESS)
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
    CRTOS::Task::Create(f4, "D Task", 128, NULL, 1, &th4);
    uint8_t buf[20];
    while(1)
    {
        printf("Task name: %s\r\n", CRTOS::Task::GetCurrentTaskName());
        if (circ.get(&buf[0], 20, 200) == CRTOS::Result::RESULT_SUCCESS)
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
        printf("Task name: %s\r\n", CRTOS::Task::GetCurrentTaskName());
        sem4.wait(1500);
        CRTOS::Task::Delete(&th4);
    }
}

void myTimerCallback(void *args)
{
    printf("Timer expired!\r\n");
    printf("Free Memory: %u bytes\r\n", CRTOS::Config::getFreeMemory());
    printf("Allocated Memory: %u bytes\r\n", CRTOS::Config::getAllocatedMemory());
}

/*
 * @brief   Application entry point.
 */
int main(void)
{
    uint8_t* mp = new uint8_t[16384];
    CRTOS::Config::initMem(mp, 16384);

    CRTOS::Config::SetCoreClock(150000000u);
    CRTOS::Config::SetTickRate(1000u);

    rt_queue = new CRTOS::Queue(20, sizeof(msg));
    circ.init();

    CRTOS::Task::Create(f1, "A Task", 128, NULL, 2, &th1);
    CRTOS::Task::Create(f2, "B Task", 128, NULL, 3, &th2);
    CRTOS::Task::Create(f3, "C Task", 280, NULL, 1, &th3);

    CRTOS::Timer::SoftwareTimer myTimer;
    CRTOS::Timer::Init(&myTimer, 1000, myTimerCallback, nullptr, true);
    CRTOS::Timer::Start(&myTimer);

    CRTOS::Scheduler::Start();

    delete []mp;

    return 0;
}