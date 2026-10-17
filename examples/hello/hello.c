/*
 * hello - smallest CRTOS program: arguments, stdio, heap, exit code.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>

int main(int argc, char **argv)
{
    printf("hello from pid %d (%s), %d argument(s):", getpid(), argv[0], argc - 1);
    for (int i = 1; i < argc; i++)
        printf(" '%s'", argv[i]);
    printf("\n");
    char *p = malloc(100000);
    if (p) {
        memset(p, 0x5A, 100000);
        printf("malloc(100000) = %p, arena %p + %lu KB\n", (void *)p, __crtos_startup->arena,
               (unsigned long)(__crtos_startup->arena_size / 1024u));
        free(p);
    }
    printf("float: %.3f\n", 3.14159 * argc);
    return argc > 1 ? atoi(argv[1]) : 0;
}
