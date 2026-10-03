/*
 * blinky - an RTOS application: the kernel's core with this program in the firmware, without
 * the SD card, processes or driver modules ("crtos build --rtos", "crtos flash --rtos").
 *
 *   - the task "led" blinks the board's USER LED (D18, GPIO1_IO09) through the NXP SDK's
 *     GPIO driver, twice a second;
 *   - a periodic timer (every 100 ms) sends the time it ran at to a message queue;
 *   - the task "report" takes those from the queue and prints, every second, how regular the
 *     timer was and how often the LED changed;
 *   - a mutex guards the counters both tasks use.
 *
 * Its tasks: "crtos kmon ps"; its output: "crtos kmon dmesg" (or the serial console).
 */
#include <crtos/rtos.h>
#include "fsl_gpio.h"
#include "fsl_iomuxc.h"

#define LED_GPIO        GPIO1
#define LED_PIN         9u          /* USER LED, lit when the pin is low */
#define TICK_MS         100u        /* the timer's period */
#define REPORT_EVERY    10          /* samples per line of the report */

struct sample {
    uint32_t seq;
    uint64_t at_us;
};

static struct queue s_samples;
static struct sample s_sample_buf[16];
static struct timer s_tick;
static uint32_t s_seq, s_dropped;

static struct mutex s_lock = MUTEX_INIT(s_lock);
static uint32_t s_toggles;          /* (s_lock) */

/* In the timer thread "ktimer": a sample to the queue, never waiting */
static void tick(void *arg)
{
    (void)arg;
    struct sample smp = { s_seq++, time_us() };
    if (queue_send(&s_samples, &smp, NO_WAIT))
        s_dropped++;
}

static void led_task(void *arg)
{
    (void)arg;
    bool on = false;
    for (;;) {
        on = !on;
        GPIO_PinWrite(LED_GPIO, LED_PIN, on ? 0u : 1u);
        mutex_lock(&s_lock, WAIT_FOREVER);
        s_toggles++;
        mutex_unlock(&s_lock);
        task_sleep_ms(250);
    }
}

static void report_task(void *arg)
{
    (void)arg;
    uint64_t prev = 0;
    uint32_t n = 0, worst = 0;
    for (;;) {
        struct sample smp;
        if (queue_recv(&s_samples, &smp, WAIT_FOREVER))
            continue;
        if (prev) {
            uint32_t period = (uint32_t)(smp.at_us - prev);
            uint32_t off = period > TICK_MS * 1000u ? period - TICK_MS * 1000u : TICK_MS * 1000u - period;
            if (off > worst)
                worst = off;
        }
        prev = smp.at_us;
        if (++n < REPORT_EVERY)
            continue;
        mutex_lock(&s_lock, WAIT_FOREVER);
        uint32_t toggles = s_toggles;
        mutex_unlock(&s_lock);
        printk("blinky: sample %lu, timer every %u ms (off by at most %lu us), LED changed %lu times, %lu dropped\n",
               (unsigned long)smp.seq, TICK_MS, (unsigned long)worst, (unsigned long)toggles,
               (unsigned long)s_dropped);
        n = 0;
        worst = 0;
    }
}

void app_main(void)
{
    /* the LED pin as a GPIO output, off */
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_09_GPIO1_IO09, 0u);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_09_GPIO1_IO09, 0x10B0u);
    gpio_pin_config_t out = { kGPIO_DigitalOutput, 1u, kGPIO_NoIntmode };
    GPIO_PinInit(LED_GPIO, LED_PIN, &out);

    queue_init(&s_samples, s_sample_buf, sizeof(s_sample_buf[0]), sizeof(s_sample_buf) / sizeof(s_sample_buf[0]));
    if (!kthread_create("led", led_task, NULL, PRIO_NORMAL + 1, 1024) ||
        !kthread_create("report", report_task, NULL, PRIO_NORMAL, 2048)) {
        printk("blinky: cannot start the tasks\n");
        return;
    }
    timer_init(&s_tick, tick, NULL);
    timer_start(&s_tick, TICK_MS, TICK_MS);
    printk("blinky: running - the LED blinks, a report every %u ms\n", TICK_MS * REPORT_EVERY);
    /* (returning ends this task; the others go on) */
}
