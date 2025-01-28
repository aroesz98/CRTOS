#include <cstdint>
#include <cstdio>

#include <CRTOS.hpp>

void f1(void *args);
void f2(void *args);
void f3(void *args);
void f4(void *args);

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

void f1(void *args)
{
    msg m;
    CRTOS::IPC::IPCMessage* message = nullptr;

    while(1)
    {
        m.size = snprintf(m.data, 16,"%s message: %u\r\n", CRTOS::Task::GetCurrentTaskName(), rand());
        rt_queue->Send(&m, 0);
        CRTOS::Task::Delay(400);

        m.size = snprintf(m.data, 16,"%s message: %u\r\n", CRTOS::Task::GetCurrentTaskName(), rand());
        rt_queue->Send(&m, 0);
        CRTOS::Task::Delay(100);

        printf("Task: %s || Free Stack: %lu\r\n",
                CRTOS::Task::GetCurrentTaskName(),
                CRTOS::Task::GetFreeStack());

        CRTOS::Result res = CRTOS::IPC::ReceiveMessage(&th1, message, 500);
        if (res == CRTOS::Result::RESULT_SUCCESS)
        {
            if (message->data != nullptr)
            {
                char* receivedData = reinterpret_cast<char*>(message->data);
                CRTOS::Result res = CRTOS::IPC::SendMessage(&th1, &th3, 3, (void*)receivedData, message->dataSize);
                if (res != CRTOS::Result::RESULT_SUCCESS)
                {
                    printf("Task A IPC Sending Error!\r\n");
                }
            }

            CRTOS::IPC::ReleaseMessage(message);
        }
        else if (res == CRTOS::Result::RESULT_IPC_TIMEOUT)
        {
            printf("Task A IPC Receiving timeout!\r\n");
        }
    }
}

void f2(void *args)
{
    msg m;
    uint8_t buf[20];
    while(1)
    {
        CRTOS::Result t = rt_queue->Receive(&m, 300);
        if(t == CRTOS::Result::RESULT_SUCCESS)
        {

            printf("Queue Received: %s\r\n", m.data);
        }
        else
        {
            printf("Queue timeout!\r\n");
        }

        snprintf((char*)&buf[0], 20, "CircBuff: %u", rand() % 0xFFFF);
        circ.put(&buf[0], 20, 0);
        CRTOS::Task::Delay(1200);

        printf("Task: %s || Free Stack: %lu\r\n",
                CRTOS::Task::GetCurrentTaskName(),
                CRTOS::Task::GetFreeStack());

        const char messageData[] = "Hello, Receiver!\r\n";
        uint32_t dataSize = sizeof(messageData);
        CRTOS::Result res = CRTOS::IPC::SendMessage(&th2, &th1, 1, (void*)messageData, dataSize);
        if (res != CRTOS::Result::RESULT_SUCCESS)
        {
            printf("Task B IPC Sending Error!\r\n");
        }
    }
}

void f3(void *)
{
    uint8_t buf[20];
    CRTOS::IPC::IPCMessage* message = nullptr;

    while(1)
    {
        if (circ.get(&buf[0], 20, 500) != CRTOS::Result::RESULT_SUCCESS)
        {
            printf("Circ Buffer Receiving timeout!\r\n");
        }

        sem1.wait(500);

        printf("Task: %s || Free Stack: %lu\r\n",
                CRTOS::Task::GetCurrentTaskName(),
                CRTOS::Task::GetFreeStack());

        CRTOS::Result res = CRTOS::IPC::ReceiveMessage(&th3, message, 100);
        if (res == CRTOS::Result::RESULT_SUCCESS)
        {
            if (message->data != nullptr)
            {
                char* receivedData = reinterpret_cast<char*>(message->data);
                printf(receivedData);
            }

            CRTOS::IPC::ReleaseMessage(message);
        }
        else if (res == CRTOS::Result::RESULT_IPC_TIMEOUT)
        {
            printf("Task C IPC Receiving timeout!\r\n");
        }
    }
}

void f4(void *)
{
    while(1)
    {
        uint32_t exponent, mantissa;
        CRTOS::Task::GetCoreLoad(exponent, mantissa);
        printf("Core load: %lu.%lu\r\n", exponent, mantissa);
        printf("Task: %s || Free Stack: %lu\r\n",
        CRTOS::Task::GetCurrentTaskName(),
        CRTOS::Task::GetFreeStack());
        CRTOS::Task::Delay(5000);
    }
}

void myTimerCallback(void *args)
{
    printf("Timer expired!\r\n");
    printf("Free Memory: %u bytes\r\n", CRTOS::Config::getFreeMemory());
    printf("Allocated Memory: %u bytes\r\n", CRTOS::Config::getAllocatedMemory());
}

int main(void)
{
    CRTOS::Result res;
    uint32_t* mp = new uint32_t[2048];
    CRTOS::Config::initMem(mp, 8192);

    CRTOS::Config::SetCoreClock(150000000u);
    CRTOS::Config::SetTickRate(1000u);

    rt_queue = new CRTOS::Queue(20, sizeof(msg));
    circ.init();

    res = CRTOS::Task::Create(f1, "A Task", 128, NULL, 6, &th1);
    if (res != CRTOS::Result::RESULT_SUCCESS)
    {
        printf("Task1 create error!\r\n");
        while(1);
    }
    res = CRTOS::Task::Create(f2, "B Task", 135, NULL, 8, &th2);
    if (res != CRTOS::Result::RESULT_SUCCESS)
    {
        printf("Task2 create error!\r\n");
        while(1);
    }
    res = CRTOS::Task::Create(f3, "C Task", 100, NULL, 4, &th3);
    if (res != CRTOS::Result::RESULT_SUCCESS)
    {
        printf("Task3 create error!\r\n");
        while(1);
    }

    res = CRTOS::Task::Create(f4, "Load_Monitor", 100, NULL, 2, NULL);
    if (res != CRTOS::Result::RESULT_SUCCESS)
    {
        printf("Task4 create error!\r\n");
        while(1);
    }

    CRTOS::Timer::SoftwareTimer myTimer;
    res = CRTOS::Timer::Init(&myTimer, 1000, myTimerCallback, nullptr, true);
    if (res != CRTOS::Result::RESULT_SUCCESS)
    {
        printf("SW Timer init error!\r\n");
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