#include <cstdint>
#include <cstdio>

#include <CRTOS.hpp>

void f1(void *args);
void f2(void *args);
void f3(void *args);

CRTOS::Semaphore sem1(0);

CRTOS::Task::TaskHandle th1 = nullptr;
CRTOS::Task::TaskHandle th2 = nullptr;
CRTOS::Task::TaskHandle th3 = nullptr;

typedef struct
{
    char data[16];
    uint32_t size = 16u;
} msg;

CRTOS::Queue* rt_queue;
CRTOS::CircularBuffer circ(100);

void f1(void *)
{
    msg m;
    memset(&m, 0, 20);

    while(1)
    {
        m.size = snprintf(m.data, 16,"%s message\r\n", CRTOS::Task::GetCurrentTaskName());
        rt_queue->Send(&m);
        CRTOS::Task::Delay(100);

        m.size = snprintf(m.data, 16,"%s message\r\n", CRTOS::Task::GetCurrentTaskName());
        rt_queue->Send(&m);
        CRTOS::Task::Delay(100);

        printf("Task: %s || Free Stack: %lu\r\n",
                CRTOS::Task::GetCurrentTaskName(),
                CRTOS::Task::GetFreeStack());

    }
}

void f2(void *)
{
    msg m;
    uint8_t buf[20];
    memset(&buf[0], 0, 20);
    memset(&m, 0, 20);

    while(1)
    {
        CRTOS::Result t = rt_queue->Receive(&m, 300);
        if(t == CRTOS::Result::RESULT_SUCCESS)
        {
            printf("Queue Received: %s\r\n", m.data);
        }

        snprintf((char*)&buf[0], 20, "CircBuff: %u\r\n", rand() % 0xFFFF);
        circ.Send(&buf[0], 200);
        CRTOS::Task::Delay(100);

        printf("Task: %s || Free Stack: %lu\r\n",
                CRTOS::Task::GetCurrentTaskName(),
                CRTOS::Task::GetFreeStack());
    }
}

void f3(void *)
{
    uint8_t buf[20];
    memset(&buf[0], 0, 20);
    CRTOS::CRC32::Init();

    while(1)
    {
        if (circ.Receive(&buf[0], 20, 500) == CRTOS::Result::RESULT_SUCCESS)
        {
            printf((char*)&buf[0]);
        }

        sem1.Wait(100);

        printf("Task: %s || Free Stack: %lu\r\n",
                CRTOS::Task::GetCurrentTaskName(),
                CRTOS::Task::GetFreeStack());

    }
}

void CoreLoadTask(void *)
{
    while(1)
    {
        uint32_t exponent, mantissa;
        CRTOS::Task::GetCoreLoad(exponent, mantissa);
        printf("Core load: %lu.%lu\r\n", exponent, mantissa);
        CRTOS::Task::Delay(5000);
    }
}

void MemInfo(void *)
{
    printf("Free Memory: %lu bytes\r\n", CRTOS::Config::GetFreeMemory());
    printf("Allocated Memory: %lu bytes\r\n", CRTOS::Config::GetAllocatedMemory());
}

int main(void)
{
    CRTOS::Result res;
    uint32_t* mp = new uint32_t[2048];

    CRTOS::Config::InitMem(mp, 8192);

    CRTOS::Config::SetCoreClock(150000000u);
    CRTOS::Config::SetTickRate(1000u);

    rt_queue = new CRTOS::Queue(20, sizeof(msg));
    circ.Init();

    res = CRTOS::Task::Create(f1, "A Task", 128, NULL, 6, &th1);
    if (res != CRTOS::Result::RESULT_SUCCESS)
    {
        printf("Task1 create error!\r\n");
        while(1);
    }
    res = CRTOS::Task::Create(f2, "B Task", 160, NULL, 8, &th2);
    if (res != CRTOS::Result::RESULT_SUCCESS)
    {
        printf("Task2 create error!\r\n");
        while(1);
    }
    res = CRTOS::Task::Create(f3, "C Task", 120, NULL, 4, &th3);
    if (res != CRTOS::Result::RESULT_SUCCESS)
    {
        printf("Task3 create error!\r\n");
        while(1);
    }

    res = CRTOS::Task::Create(CoreLoadTask, "CoreLoadMonitor", 120, NULL, 2, NULL);
    if (res != CRTOS::Result::RESULT_SUCCESS)
    {
        printf("Task4 create error!\r\n");
        while(1);
    }

    CRTOS::Timer::SoftwareTimer myTimer;
    res = CRTOS::Timer::Init(&myTimer, 5000, MemInfo, nullptr, true);
    if (res != CRTOS::Result::RESULT_SUCCESS)
    {
        printf("Task init error!\r\n");
        while(1);
    }
    res = CRTOS::Timer::Start(&myTimer);
    if (res != CRTOS::Result::RESULT_SUCCESS)
    {
        printf("Timer start error!\r\n");
        while(1);
    }

    CRTOS::Scheduler::Start();

    delete []mp;

    return 0;
}