/*
 * crtos/printk.h - kernel log. printk() never blocks: it appends to a ring that the
 * console driver drains from its TX interrupt.
 */
#ifndef CRTOS_PRINTK_H
#define CRTOS_PRINTK_H

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int printk(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int vprintk(const char *fmt, va_list ap);
int ksnprintf(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

/* Write raw text to the log/console (no formatting) */
void log_write(const char *s, size_t len);

/* Formatted console text without the timestamp prefix (interactive output) */
int cprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Stop the system: print synchronously and halt */
void panic(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

#define pr_err(fmt, ...)  printk("E: " fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...) printk("W: " fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...) printk(fmt, ##__VA_ARGS__)

#define BUG_ON(cond) do { if (__builtin_expect(!!(cond), 0)) panic("BUG at %s:%d: %s", __FILE__, __LINE__, #cond); } while (0)

#ifdef __cplusplus
}
#endif

#endif
