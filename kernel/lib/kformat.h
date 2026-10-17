/*
 * lib/kformat.h - freestanding printf-style formatter shared by the kernel and libraries
 */
#ifndef CRTOS_LIB_KFORMAT_H
#define CRTOS_LIB_KFORMAT_H

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*kformat_emit_fn)(char c, void *ctx);

/* Format @fmt, passing every output character to @emit; returns the character count */
int kformat(kformat_emit_fn emit, void *ctx, const char *fmt, va_list ap);
int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int ksnprintf(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

#ifdef __cplusplus
}
#endif

#endif
