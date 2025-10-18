#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>

#define NES_UNUSED(x) (void)(x)

#define NES_ASSERT(cond)                                                          \
    do {                                                                          \
        if (!(cond)) {                                                            \
            fprintf(stderr, "[NES] Assertion failed: %s (%s:%d)\n", #cond,        \
                    __FILE__, __LINE__);                                          \
        }                                                                         \
    } while (0)

#endif /* UTIL_H */
