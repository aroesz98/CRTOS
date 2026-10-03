/*
 * @NAME@ - a background service (made by "crtos new --service"; change it as you like).
 *
 * init starts services at boot when /sd/crtos/etc/init.cfg lists them:
 *
 *     service @NAME@ respawn /sd/crtos/sbin/@NAME@.app
 *
 * (respawn: started again when it ends). What it prints goes to the console and the kernel
 * log ("crtos kmon dmesg"). This one reports the free memory once a minute.
 */
#include <stdio.h>
#include <crtos.h>

int main(void)
{
    printf("@NAME@: started\n");
    for (;;) {
        struct crtos_sysinfo si;
        if (crtos_sys_info(&si) == 0)
            printf("@NAME@: up %lu s, %lu KB free, %lu processes\n", (unsigned long)(si.uptime_us / 1000000u),
                   (unsigned long)(si.mem_free / 1024u), (unsigned long)si.nprocs);
        crtos_sleep_ms(60000);
    }
}
