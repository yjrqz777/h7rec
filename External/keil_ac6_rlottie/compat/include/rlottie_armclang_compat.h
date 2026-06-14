#ifndef AC6_RLOTTIE_ARMCLANG_COMPAT_H
#define AC6_RLOTTIE_ARMCLANG_COMPAT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
#include <condition_variable>
#include <functional>
#endif

#ifndef __has_include
#define __has_include(x) 0
#endif

#ifndef RLOTTIE_BUILD
#define RLOTTIE_BUILD
#endif

#ifndef RLOTTIE_DISABLE_DYNAMIC_LOADING
#define RLOTTIE_DISABLE_DYNAMIC_LOADING 1
#endif

#if defined(__ARMCC_VERSION) && !defined(RLOTTIE_AC6_HAVE_STRDUP)
#define RLOTTIE_AC6_HAVE_STRDUP 1
static inline char *strdup(const char *s)
{
    if (!s) {
        return NULL;
    }

    size_t len = strlen(s) + 1U;
    char *copy = (char *)malloc(len);
    if (copy) {
        memcpy(copy, s, len);
    }
    return copy;
}
#endif

#endif
