// Minimal check macros shared by the host tests (each test file is its own program).
#ifndef HOST_TESTS_CHECK_H
#define HOST_TESTS_CHECK_H

#include <cmath>
#include <cstdio>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond) do { \
    if (cond) g_pass++; \
    else { g_fail++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_NEAR(a, b, tol) do { \
    const double _a = (a), _b = (b); \
    if (std::fabs(_a - _b) <= (tol)) g_pass++; \
    else { g_fail++; std::printf("FAIL %s:%d  %s = %.4f, expected %.4f\n", __FILE__, __LINE__, #a, _a, _b); } \
} while (0)

/** Print the totals; use as `return check_summary("name");` at the end of main. */
static int check_summary(const char* name) {
    std::printf("%s: %d passed, %d failed\n", name, g_pass, g_fail);
    return g_fail ? 1 : 0;
}

#endif
