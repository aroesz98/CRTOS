/*
 * tls_crtos.c - what Mbed TLS needs from the system (see crtos_mbedtls_config.h): entropy from the
 * hardware random number generator behind /dev/random, and a millisecond clock.
 */
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <crtos.h>

#include "mbedtls/build_info.h"
#include "mbedtls/entropy.h"
#include "mbedtls/platform_time.h"

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen);

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    static int fd = -1;
    (void)data;
    *olen = 0;
    if (fd < 0)
    {
        fd = open("/dev/random", O_RDONLY);
        if (fd < 0)
            return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
    while (*olen < len)
    {
        ssize_t n = read(fd, output + *olen, len - *olen);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
        *olen += (size_t)n;
    }
    return 0;
}

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t)(crtos_time_us() / 1000u);
}
