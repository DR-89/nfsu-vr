/* Can also be built with the NDK and run on an ARM64 Android device. */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include <assert.h>
#include <fenv.h>
#include <stdio.h>

int main(void)
{
    assert(!fesetround(FE_TONEAREST));
    assert(SSE_CVT_F2I(2.5, 0) == 2);
    assert(SSE_CVT_F2I(3.5, 0) == 4);
    assert(SSE_CVT_F2I(-2.5, 0) == -2);
    assert(SSE_CVT_F2I(-2.9, 1) == -2);
    assert(SSE_CVT_F2I(2147483647.0, 0) == INT32_MAX);
    assert(SSE_CVT_F2I(2147483647.5, 0) == INT32_MIN);
    assert(SSE_CVT_F2I(2147483647.5, 1) == INT32_MAX);
    assert(SSE_CVT_F2I(-2147483648.9, 1) == INT32_MIN);
    assert(SSE_CVT_F2I(NAN, 0) == INT32_MIN);
    assert(SSE_CVT_F2I(INFINITY, 1) == INT32_MIN);
    assert(SSE_CVT_F2I(-INFINITY, 0) == INT32_MIN);
    assert(!fesetround(FE_DOWNWARD));
    assert(SSE_CVT_F2I(-2.1, 0) == -3);
    assert(SSE_CVT_F2I(-2.1, 1) == -2);
    assert(!fesetround(FE_UPWARD));
    assert(SSE_CVT_F2I(2.1, 0) == 3);
    assert(!fesetround(FE_TONEAREST));
    puts("scalar SSE conversion: PASS");
    return 0;
}
