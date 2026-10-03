/*
 * crt0.c - program entry. The kernel starts the main thread here with the startup block
 * (arguments, environment, constructor tables) in R0.
 */
#include <stdlib.h>
#include <crtos.h>

extern int main(int argc, char **argv, char **envp);
extern char **environ;

struct crtos_startup *__crtos_startup;

/* The full newlib (programs that run in place) calls these around its own init/fini arrays,
 * which crti.o/crtn.o would give; here the kernel's startup block does that job */
__attribute__((weak)) void _init(void)
{
}

__attribute__((weak)) void _fini(void)
{
}

static void run_fini(void)
{
    struct crtos_startup *s = __crtos_startup;
    for (uint32_t i = s->fini_count; i > 0; i--)
        s->fini_array[i - 1]();
}

void _start(struct crtos_startup *s) __attribute__((noreturn, used));
void _start(struct crtos_startup *s)
{
    __crtos_startup = s;
    environ = s->envp;
    if (s->fini_count)
        atexit(run_fini);
    for (uint32_t i = 0; i < s->init_count; i++)
        s->init_array[i]();
    exit(main(s->argc, s->argv, s->envp));
}
