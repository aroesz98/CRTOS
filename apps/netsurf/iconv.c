/*
 * iconv.c - iconv() for NetSurf (newlib is built without it): UTF-8, US-ASCII and the
 * ISO-8859-n / Windows-125x single-byte charsets, with libparserutils' tables and charset
 * aliases (latin2, cp1250, ...). NetSurf needs it for forms sent in a page's charset and the
 * front end's CP1252 clipboard. "//TRANSLIT" (or "//IGNORE") after the target name writes
 * '?' for a character the target cannot hold; without it that is EILSEQ, which NetSurf
 * answers with an HTML character reference.
 */
#include <errno.h>
#include <iconv.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <parserutils/charset/mibenum.h>

#include "8859_tables.h"
#include "ext8_tables.h"

#define NONE 0xFFFFu

enum
{
    CS_UTF8,
    CS_ASCII,
    CS_UPPER96,
    CS_UPPER128
};

struct charset
{
    const char *name;
    int kind;
    const uint32_t *table; /* 0xA0-0xFF (96) or 0x80-0xFF (128) */
};

static const struct charset s_charsets[] = {
    {"UTF-8", CS_UTF8, NULL},
    {"US-ASCII", CS_ASCII, NULL},
    {"ISO-8859-1", CS_UPPER96, t1},
    {"ISO-8859-2", CS_UPPER96, t2},
    {"ISO-8859-3", CS_UPPER96, t3},
    {"ISO-8859-4", CS_UPPER96, t4},
    {"ISO-8859-5", CS_UPPER96, t5},
    {"ISO-8859-6", CS_UPPER96, t6},
    {"ISO-8859-7", CS_UPPER96, t7},
    {"ISO-8859-8", CS_UPPER96, t8},
    {"ISO-8859-9", CS_UPPER96, t9},
    {"ISO-8859-10", CS_UPPER96, t10},
    {"ISO-8859-11", CS_UPPER96, t11},
    {"ISO-8859-13", CS_UPPER96, t13},
    {"ISO-8859-14", CS_UPPER96, t14},
    {"ISO-8859-15", CS_UPPER96, t15},
    {"ISO-8859-16", CS_UPPER96, t16},
    {"Windows-1250", CS_UPPER128, w1250},
    {"Windows-1251", CS_UPPER128, w1251},
    {"Windows-1252", CS_UPPER128, w1252},
    {"Windows-1253", CS_UPPER128, w1253},
    {"Windows-1254", CS_UPPER128, w1254},
    {"Windows-1255", CS_UPPER128, w1255},
    {"Windows-1256", CS_UPPER128, w1256},
    {"Windows-1257", CS_UPPER128, w1257},
    {"Windows-1258", CS_UPPER128, w1258},
};

#define N_CHARSETS (sizeof(s_charsets) / sizeof(s_charsets[0]))

struct conv
{
    const struct charset *from, *to;
    bool translit;
};

/* A charset by any of its names ("latin2", "cp1250", ...) */
static const struct charset *find(const char *name, size_t len)
{
    uint16_t mib = parserutils_charset_mibenum_from_name(name, len);
    if (!mib)
        return NULL;
    for (size_t i = 0; i < N_CHARSETS; i++)
        if (parserutils_charset_mibenum_from_name(s_charsets[i].name, strlen(s_charsets[i].name)) == mib)
            return &s_charsets[i];
    return NULL;
}

iconv_t iconv_open(const char *tocode, const char *fromcode)
{
    const char *suffix = strstr(tocode, "//");
    size_t tolen = suffix ? (size_t)(suffix - tocode) : strlen(tocode);
    const char *fsuffix = strstr(fromcode, "//");
    size_t fromlen = fsuffix ? (size_t)(fsuffix - fromcode) : strlen(fromcode);
    const struct charset *to = find(tocode, tolen), *from = find(fromcode, fromlen);
    if (!to || !from)
    {
        errno = EINVAL;
        return (iconv_t)-1;
    }
    struct conv *c = malloc(sizeof(*c));
    if (!c)
    {
        errno = ENOMEM;
        return (iconv_t)-1;
    }
    c->from = from;
    c->to = to;
    c->translit = suffix && (!strncmp(suffix, "//TRANSLIT", 10) || !strncmp(suffix, "//IGNORE", 8));
    return (iconv_t)c;
}

int iconv_close(iconv_t cd)
{
    free((void *)cd);
    return 0;
}

/* One character: its code point and length; 0 bytes: incomplete, -1: invalid */
static int decode(const struct charset *cs, const uint8_t *s, size_t n, uint32_t *cp)
{
    uint8_t b = s[0];
    if (cs->kind != CS_UTF8)
    {
        if (b < 0x80)
        {
            *cp = b;
        }
        else if (cs->kind == CS_ASCII)
        {
            return -1;
        }
        else if (cs->kind == CS_UPPER96)
        {
            *cp = b < 0xA0 ? b : cs->table[b - 0xA0];
        }
        else
        {
            *cp = cs->table[b - 0x80];
        }
        return *cp == NONE ? -1 : 1;
    }
    int len;
    uint32_t v, min;
    if (b < 0x80)
    {
        *cp = b;
        return 1;
    }
    else if ((b & 0xE0) == 0xC0)
    {
        len = 2, v = b & 0x1Fu, min = 0x80;
    }
    else if ((b & 0xF0) == 0xE0)
    {
        len = 3, v = b & 0x0Fu, min = 0x800;
    }
    else if ((b & 0xF8) == 0xF0)
    {
        len = 4, v = b & 0x07u, min = 0x10000;
    }
    else
    {
        return -1;
    }
    for (int i = 1; i < len; i++)
    {
        if ((size_t)i >= n)
            return 0;
        if ((s[i] & 0xC0) != 0x80)
            return -1;
        v = (v << 6) | (s[i] & 0x3Fu);
    }
    if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF))
        return -1;
    *cp = v;
    return len;
}

/* One character into @out (4 bytes room); its length, or -1 if the charset lacks it */
static int encode(const struct charset *cs, uint32_t cp, uint8_t *out)
{
    if (cs->kind == CS_UTF8)
    {
        if (cp < 0x80)
        {
            out[0] = (uint8_t)cp;
            return 1;
        }
        else if (cp < 0x800)
        {
            out[0] = (uint8_t)(0xC0 | (cp >> 6));
            out[1] = (uint8_t)(0x80 | (cp & 0x3F));
            return 2;
        }
        else if (cp < 0x10000)
        {
            out[0] = (uint8_t)(0xE0 | (cp >> 12));
            out[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
            out[2] = (uint8_t)(0x80 | (cp & 0x3F));
            return 3;
        }
        out[0] = (uint8_t)(0xF0 | (cp >> 18));
        out[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (uint8_t)(0x80 | (cp & 0x3F));
        return 4;
    }
    if (cp < 0x80)
    {
        out[0] = (uint8_t)cp;
        return 1;
    }
    if (cs->kind == CS_ASCII)
        return -1;
    if (cs->kind == CS_UPPER96 && cp < 0xA0)
    {
        out[0] = (uint8_t)cp; /* C1 controls map to themselves */
        return 1;
    }
    unsigned first = cs->kind == CS_UPPER96 ? 0xA0 : 0x80, n = 256 - first;
    for (unsigned i = 0; i < n; i++)
    {
        if (cs->table[i] == cp)
        {
            out[0] = (uint8_t)(first + i);
            return 1;
        }
    }
    return -1;
}

size_t iconv(iconv_t cd, char **__restrict inbuf, size_t *__restrict inleft, char **__restrict outbuf,
             size_t *__restrict outleft)
{
    struct conv *c = (struct conv *)cd;
    if (!inbuf || !*inbuf)
        return 0; /* stateless: nothing to reset */
    size_t replaced = 0;
    while (*inleft)
    {
        uint32_t cp;
        int n = decode(c->from, (const uint8_t *)*inbuf, *inleft, &cp);
        if (n <= 0)
        {
            errno = n == 0 ? EINVAL : EILSEQ;
            return (size_t)-1;
        }
        uint8_t tmp[4];
        int m = encode(c->to, cp, tmp);
        if (m < 0)
        {
            if (!c->translit)
            {
                errno = EILSEQ;
                return (size_t)-1;
            }
            tmp[0] = '?';
            m = 1;
            replaced++;
        }
        if ((size_t)m > *outleft)
        {
            errno = E2BIG;
            return (size_t)-1;
        }
        memcpy(*outbuf, tmp, (size_t)m);
        *outbuf += m;
        *outleft -= (size_t)m;
        *inbuf += n;
        *inleft -= (size_t)n;
    }
    return replaced;
}
