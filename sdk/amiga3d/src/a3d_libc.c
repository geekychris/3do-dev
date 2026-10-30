/*
 * a3d_libc.c - C library functions the games use that the 3DO's libc lacks.
 */
#include <stdlib.h>

long atol(const char *s)
{
    long v = 0;
    int neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-' || *s == '+') neg = *s++ == '-';
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}
