/*
 * fnmatch.c - fnmatch(), which newlib declares but does not have here. Its own object file, so
 * a program with another fnmatch (GCC's and binutils' libiberty) links without a clash.
 *
 * Patterns: * ? [abc] [a-z] [!a] and a backslash (the next character as it is, unless
 * FNM_NOESCAPE). Flags: FNM_NOESCAPE, FNM_PATHNAME, FNM_PERIOD, FNM_CASEFOLD, FNM_LEADING_DIR.
 */
#include <ctype.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <string.h>

/* newlib's header shows these only with _GNU_SOURCE, which would also change basename() */
#ifndef FNM_CASEFOLD
#define FNM_CASEFOLD 0x10
#endif
#ifndef FNM_LEADING_DIR
#define FNM_LEADING_DIR 0x08
#endif

static int fold(int c, int flags)
{
    return (flags & FNM_CASEFOLD) ? tolower(c) : c;
}

/* [...] at @p against @c: the end of the bracket (after ]) with *ok, or NULL if it is no bracket */
static const char *bracket(const char *p, int c, int flags, bool *ok)
{
    bool neg = *p == '!' || *p == '^';
    if (neg)
        p++;
    bool in = false;
    const char *start = p;
    while (*p && (*p != ']' || p == start)) {
        int lo = (unsigned char)*p;
        if (lo == '\\' && !(flags & FNM_NOESCAPE) && p[1])
            lo = (unsigned char)*++p;
        int hi = lo;
        if (p[1] == '-' && p[2] && p[2] != ']') {
            hi = (unsigned char)p[2];
            if (hi == '\\' && !(flags & FNM_NOESCAPE) && p[3])
                hi = (unsigned char)*(p += 3);
            else
                p += 2;
        }
        if (fold(c, flags) >= fold(lo, flags) && fold(c, flags) <= fold(hi, flags))
            in = true;
        p++;
    }
    if (*p != ']')
        return NULL;
    *ok = in != neg;
    return p + 1;
}

static int match(const char *pat, const char *s, const char *start, int flags)
{
    for (;;) {
        int c = (unsigned char)*s;
        switch (*pat) {
        case 0:
            return !c || ((flags & FNM_LEADING_DIR) && c == '/') ? 0 : FNM_NOMATCH;
        case '?':
            if (!c || ((flags & FNM_PATHNAME) && c == '/'))
                return FNM_NOMATCH;
            if ((flags & FNM_PERIOD) && c == '.' && (s == start || ((flags & FNM_PATHNAME) && s[-1] == '/')))
                return FNM_NOMATCH;
            break;
        case '*':
            while (pat[1] == '*')
                pat++;
            if ((flags & FNM_PERIOD) && c == '.' && (s == start || ((flags & FNM_PATHNAME) && s[-1] == '/')))
                return FNM_NOMATCH;
            if (!pat[1])
                return (flags & FNM_PATHNAME) && !(flags & FNM_LEADING_DIR) && strchr(s, '/') ? FNM_NOMATCH : 0;
            for (;; s++) {
                if (!match(pat + 1, s, start, flags & ~FNM_PERIOD))
                    return 0;
                if (!*s || ((flags & FNM_PATHNAME) && *s == '/'))
                    return FNM_NOMATCH;
            }
        case '[': {
            if (!c || ((flags & FNM_PATHNAME) && c == '/'))
                return FNM_NOMATCH;
            bool ok;
            const char *end = bracket(pat + 1, c, flags, &ok);
            if (end) {
                if (!ok)
                    return FNM_NOMATCH;
                pat = end;
                s++;
                continue;
            }
            if (c != '[') /* no closing ]: a plain [ */
                return FNM_NOMATCH;
            break;
        }
        case '\\':
            if (!(flags & FNM_NOESCAPE) && pat[1])
                pat++;
            /* fall through */
        default:
            if (fold(c, flags) != fold((unsigned char)*pat, flags))
                return FNM_NOMATCH;
        }
        pat++;
        s++;
    }
}

int fnmatch(const char *pattern, const char *string, int flags)
{
    return match(pattern, string, string, flags);
}
