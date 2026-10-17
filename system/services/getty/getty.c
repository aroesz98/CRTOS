/*
 * getty - the shell on a serial line (layer 2): /dev/ttyACM0, the board as a USB serial port
 * of a computer (drivers/usb with dr_mode = "peripheral" on J9).
 *
 * Waits until a terminal program opens the port on the computer (DTR: TTY_IOC_WAIT_CARRIER),
 * then runs the shell over two terminal pipes (PIPE_TTY) like the graphical terminal: in
 * canonical mode (the default) this program edits the line (echo, backspace, Ctrl-U) and
 * sends it whole on Enter; in raw mode every byte goes straight through, Ctrl-C too (the
 * shell edits its command line so). In canonical mode Ctrl-C ends the foreground process the
 * shell registered. Output gets CR LF line ends. Closing the terminal ends the shell; a shell
 * that ends (exit) is started again while the terminal stays open.
 *
 *     getty [device]           (default /dev/ttyACM0)
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>
#include <crtos/tty.h>

#define SHELL       "/sd/crtos/bin/sh.app"
#define LINE_MAX    256
#define RETRY_MS    5000    /* looking for the device again */

static const char *s_dev = "/dev/ttyACM0";
static int s_tty = -1;                  /* the line */
static int s_in = -1, s_out = -1;       /* our ends of the shell's input and output */
static int s_pid = -1;
static char s_line[LINE_MAX];           /* being typed (canonical mode) */
static int s_len;
static int s_esc;                       /* 1: ESC seen, 2: inside ESC [ ... */
static char s_prev;

/* To the line, with CR LF line ends */
static void put(const char *p, int n)
{
    char buf[256];
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (p[i] == '\n')
            buf[k++] = '\r';
        buf[k++] = p[i];
        if (k >= (int)sizeof(buf) - 1) {
            write(s_tty, buf, (size_t)k);
            k = 0;
        }
    }
    if (k)
        write(s_tty, buf, (size_t)k);
}

static void say(const char *s)
{
    put(s, (int)strlen(s));
}

/* ---- the shell --------------------------------------------------------------------------------- */

static uint32_t own_caps(void)
{
    struct crtos_procinfo pi;
    for (int i = 0; crtos_proc_info(i, &pi) == 0; i++)
        if (pi.pid == getpid())
            return pi.caps;
    return 0;
}

static int start_shell(void)
{
    int in[2], out[2];
    if (crtos_pipe(in, PIPE_TTY))
        return -1;
    if (crtos_pipe(out, PIPE_TTY)) {
        close(in[0]);
        close(in[1]);
        return -1;
    }
    const char *argv[] = { "sh", NULL };
    struct crtos_spawn sp;
    memset(&sp, 0, sizeof(sp));
    sp.path = SHELL;
    sp.argv = argv;
    sp.stdio[0] = in[0];
    sp.stdio[1] = out[1];
    sp.stdio[2] = out[1];
    sp.caps = own_caps();
    s_pid = crtos_spawn(&sp);
    close(in[0]); /* the shell has its own copies */
    close(out[1]);
    if (s_pid < 0) {
        close(in[1]);
        close(out[0]);
        return -1;
    }
    s_in = in[1];
    s_out = out[0];
    fcntl(s_out, F_SETFL, O_NONBLOCK);
    s_len = 0;
    s_esc = 0;
    return 0;
}

static void stop_shell(void)
{
    if (s_pid > 0) {
        int32_t fg = 0;
        ioctl(s_in, TTY_IOC_GET_FG, &fg);
        if (fg > 0)
            crtos_kill(fg, 0);
        crtos_kill(s_pid, 0);
        int status;
        crtos_wait(s_pid, &status, 1000);
    }
    if (s_in >= 0)
        close(s_in);
    if (s_out >= 0)
        close(s_out);
    s_in = s_out = s_pid = -1;
}

/* The shell's output to the line; returns true once it has ended (every writer closed) */
static bool shell_output(void)
{
    char buf[512];
    for (;;) {
        int n = (int)read(s_out, buf, sizeof(buf));
        if (n > 0) {
            put(buf, n);
            continue;
        }
        return n == 0;
    }
}

/* ---- typed input ----------------------------------------------------------------------------------- */

static uint32_t tty_mode(void)
{
    uint32_t m = TTY_MODE_CANON | TTY_MODE_ECHO;
    ioctl(s_in, TTY_IOC_GET_MODE, &m);
    return m;
}

static void send(const char *p, int n)
{
    if (s_in >= 0 && n > 0)
        write(s_in, p, (size_t)n);
}

static void interrupt(void)
{
    int32_t fg = 0;
    ioctl(s_in, TTY_IOC_GET_FG, &fg);
    say("^C\n");
    s_len = 0;
    if (fg > 0)
        crtos_kill(fg, -4); /* -EINTR: "interrupted" */
    else
        send("\n", 1); /* a fresh prompt */
}

static void input(const char *p, int n)
{
    uint32_t mode = tty_mode();
    for (int i = 0; i < n; i++) {
        char c = p[i];
        char prev = s_prev;
        s_prev = c;
        if (c == 3 && (mode & TTY_MODE_CANON)) {
            interrupt();
            continue;
        }
        if (!(mode & TTY_MODE_CANON)) { /* raw: straight through */
            send(&c, 1);
            if (mode & TTY_MODE_ECHO)
                put(&c, 1);
            continue;
        }
        if (s_esc) { /* ESC [ ... final byte: cursor keys and the like are not edited */
            if (s_esc == 1 && c == '[')
                s_esc = 2;
            else if (s_esc == 1 || (c >= 0x40 && c <= 0x7e))
                s_esc = 0;
            continue;
        }
        switch (c) {
        case '\n':
            if (prev == '\r') /* CR LF: the CR ended the line already */
                break;
            /* fall through */
        case '\r':
            s_line[s_len++] = '\n';
            say("\n");
            send(s_line, s_len);
            s_len = 0;
            break;
        case 8:
        case 127:
            if (s_len) {
                s_len--;
                say("\b \b");
            }
            break;
        case 21: /* Ctrl-U */
            while (s_len) {
                s_len--;
                say("\b \b");
            }
            break;
        case 27:
            s_esc = 1;
            break;
        default:
            if ((unsigned char)c >= 32 && s_len < LINE_MAX - 1) {
                s_line[s_len++] = c;
                if (mode & TTY_MODE_ECHO)
                    put(&c, 1);
            }
            break;
        }
    }
}

/* ---- the line ------------------------------------------------------------------------------------- */

/* Shells for as long as the device is there */
static void serve(void)
{
    for (;;) {
        if (ioctl(s_tty, TTY_IOC_WAIT_CARRIER, 0) < 0) {
            printf("getty: %s: %s\n", s_dev, strerror(errno));
            return;
        }
        if (start_shell()) {
            say("getty: cannot start the shell\n");
            crtos_sleep_ms(RETRY_MS);
            continue;
        }
        bool hangup = false, ended = false;
        while (!hangup && !ended) {
            struct pollfd pf[2] = { { s_tty, POLLIN, 0 }, { s_out, POLLIN, 0 } };
            if (poll(pf, 2, -1) < 0)
                continue;
            if (pf[1].revents & (POLLIN | POLLHUP))
                ended = shell_output();
            if (pf[0].revents & POLLIN) {
                char buf[128];
                int n = (int)read(s_tty, buf, sizeof(buf));
                if (n > 0)
                    input(buf, n);
                else if (n == 0)
                    hangup = true; /* the terminal closed */
            }
            if (pf[0].revents & (POLLHUP | POLLERR))
                hangup = true;
        }
        stop_shell();
        if (ended) {
            say("\n[the shell ended - a new one starts]\n");
            crtos_sleep_ms(500);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc > 1)
        s_dev = argv[1];
    chdir("/sd/crtos");
    bool told = false;
    for (;;) {
        s_tty = open(s_dev, O_RDWR);
        if (s_tty < 0) { /* not (yet) there: e.g. the USB port is a host, or its driver is loading */
            if (!told)
                printf("getty: waiting for %s\n", s_dev);
            told = true;
            crtos_sleep_ms(RETRY_MS);
            continue;
        }
        told = false;
        serve();
        close(s_tty);
        crtos_sleep_ms(RETRY_MS);
    }
}
