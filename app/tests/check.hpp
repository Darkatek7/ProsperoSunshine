// SPDX-License-Identifier: GPL-3.0-only
// The app tests' checks, shared by their files (app/tests/run.sh links them into one program).
#pragma once

#include <cstdio>

inline int failures = 0;
#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)
// CHECK, and the test function stops when it fails: for a pointer the next lines dereference.
#define REQUIRE(cond)                                                                              \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            ++failures;                                                                            \
            return;                                                                                \
        }                                                                                          \
    } while (0)
