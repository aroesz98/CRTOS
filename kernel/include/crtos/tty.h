/*
 * crtos/tty.h - the console terminal (/dev/console).
 *
 * The serial console is shared by the kernel monitor (kmon) and user space: output of both
 * goes to the UART, input goes to whoever has the focus. Ctrl-] typed in the terminal gives
 * the input to kmon; its "exit" command gives it back. init hands the console to user space
 * once the shell runs (TTY_IOC_FOCUS).
 *
 * In canonical mode (default) the kernel edits lines (echo, backspace) and read() returns
 * whole lines; Ctrl-C ends the foreground process (TTY_IOC_SET_FG) or cancels the line
 * (read() fails with EINTR), Ctrl-D at the start of a line reads as end of file.
 */
#ifndef CRTOS_TTY_H
#define CRTOS_TTY_H

#include <stdint.h>
#include <crtos/ioctl.h>

#define TTY_MODE_CANON  0x1u    /* line editing, read() returns lines */
#define TTY_MODE_ECHO   0x2u    /* echo typed characters */

struct tty_size {
    uint16_t cols, rows;
};

#define TTY_IOC_GET_MODE    _IOR('T', 1, uint32_t)
#define TTY_IOC_SET_MODE    _IO('T', 2)         /* arg: TTY_MODE_* */
#define TTY_IOC_SET_FG      _IO('T', 3)         /* arg: pid that Ctrl-C ends (0: none) */
#define TTY_IOC_FOCUS       _IO('T', 4)         /* take the console input from kmon (CAP_SYS) */
#define TTY_IOC_GET_SIZE    _IOR('T', 5, struct tty_size)
#define TTY_IOC_GET_FG      _IOR('T', 6, int32_t)       /* the foreground process (terminals) */
#define TTY_IOC_SET_SIZE    _IOW('T', 7, struct tty_size) /* terminal pipes: size of the window */
/* Serial lines (USB CDC /dev/ttyACM0): wait until a terminal is attached at the other end
 * (the host opened the port and raised DTR). read() returns 0 and poll() POLLHUP while none is. */
#define TTY_IOC_WAIT_CARRIER _IO('T', 8)
/* Serial ports (/dev/ttyS*): line speed in baud (8 data bits, no parity, 1 stop bit), and
 * the internal loopback (sent data comes back, the pins are not used: a check without wires) */
#define TTY_IOC_GET_SPEED    _IOR('T', 9, uint32_t)
#define TTY_IOC_SET_SPEED    _IO('T', 10)       /* arg: baud */
#define TTY_IOC_SET_LOOPBACK _IO('T', 11)       /* arg: 0 or 1 */

#define TTY_KEY_KMON        0x1Du   /* Ctrl-] */

#endif
