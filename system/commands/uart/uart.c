/*
 * uart - talk to a serial port (/dev/ttyS*): sends the text given (with CR LF) and shows what
 * arrives until the line is quiet; without text it only listens.
 *
 *     uart [-D dev] [-b baud] [-l] [-x] [-t ms] [TEXT...]
 *
 *   -D   the port (default /dev/ttyS3)
 *   -b   line speed (8N1)
 *   -l   internal loopback: what is sent comes back (a check without wires)
 *   -x   show what arrives in hex (binary protocols)
 *   -t   how long the line must be quiet before the end (default 300 ms; listening: the
 *        whole time)
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>
#include <crtos/tty.h>

int main(int argc, char **argv)
{
    const char *dev = "/dev/ttyS3";
    uint32_t baud = 0, quiet_ms = 300;
    int loop = 0, hex = 0, i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        char o = argv[i][1];
        if (o == 'l') {
            loop = 1;
            continue;
        }
        if (o == 'x') {
            hex = 1;
            continue;
        }
        if (i + 1 >= argc || (o != 'D' && o != 'b' && o != 't')) {
            printf("usage: uart [-D dev] [-b baud] [-l] [-x] [-t ms] [TEXT...]\n");
            return 2;
        }
        const char *v = argv[++i];
        if (o == 'D')
            dev = v;
        else if (o == 'b')
            baud = (uint32_t)strtoul(v, NULL, 0);
        else
            quiet_ms = (uint32_t)strtoul(v, NULL, 0);
    }
    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        printf("uart: %s: %s\n", dev, strerror(errno));
        return 1;
    }
    if (baud && ioctl(fd, TTY_IOC_SET_SPEED, baud) < 0) {
        printf("uart: %lu baud: %s\n", (unsigned long)baud, strerror(errno));
        close(fd);
        return 1;
    }
    ioctl(fd, TTY_IOC_GET_SPEED, &baud);
    ioctl(fd, TTY_IOC_SET_LOOPBACK, loop);
    char out[512];
    size_t n = 0;
    for (; i < argc; i++)
        n += (size_t)snprintf(out + n, sizeof(out) - n, "%s%s", n ? " " : "", argv[i]);
    int talk = n > 0;
    if (talk) {
        n += (size_t)snprintf(out + n, sizeof(out) - n, "\r\n");
        write(fd, out, n);
    }
    printf("%s, %lu baud%s: %s\n", dev, (unsigned long)baud, loop ? ", loopback" : "", talk ? "sent, got:" : "listening");
    char in[256];
    size_t got = 0, same = 0;
    for (;;) {
        struct pollfd pf = { fd, POLLIN, 0 };
        if (poll(&pf, 1, (int)quiet_ms) <= 0)
            break;
        int r = (int)read(fd, in, sizeof(in));
        if (r <= 0)
            break;
        for (int k = 0; k < r; k++, got++) {
            if (got < n && in[k] == out[got])
                same++;
            if (hex)
                printf("%02x%c", (unsigned char)in[k], got % 16 == 15 ? '\n' : ' ');
        }
        if (!hex)
            fwrite(in, 1, (size_t)r, stdout);
        fflush(stdout);
    }
    ioctl(fd, TTY_IOC_SET_LOOPBACK, 0);
    close(fd);
    printf("\n(%lu byte(s) received)\n", (unsigned long)got);
    if (loop && talk) {
        int ok = got == n && same == n;
        printf("loopback: %s\n", ok ? "received = sent" : "DIFFERENT");
        return ok ? 0 : 1;
    }
    return 0;
}
