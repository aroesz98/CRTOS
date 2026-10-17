/*
 * @NAME@ - a command-line program (made by "crtos new --console"; change it as you like).
 *
 * Standard C works: printf, files (fopen("/sd/crtos/...")), malloc, time, POSIX threads.
 * CRTOS's own functions (processes, IPC, system information) are in <crtos.h>.
 * Run it from the computer with "crtos run @NAME@ one two" or in the shell on the board.
 */
#include <stdio.h>
#include <crtos.h>

int main(int argc, char **argv)
{
    printf("Hello from @NAME@!\n");
    for (int i = 1; i < argc; i++)
        printf("  argument %d: %s\n", i, argv[i]);

    struct crtos_sysinfo si;
    if (crtos_sys_info(&si) == 0)
        printf("The board has been running for %lu s, %lu KB of memory is free.\n",
               (unsigned long)(si.uptime_us / 1000000u), (unsigned long)(si.mem_free / 1024u));
    return 0;   /* the exit code: "crtos run" returns it */
}
