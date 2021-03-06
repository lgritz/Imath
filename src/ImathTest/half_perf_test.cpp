//
// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the OpenEXR Project.
//
//#define IMATH_HALF_NO_TABLES_AT_ALL
//#define IMATH_HALF_EXCEPTIONS_ENABLED
//
#ifdef _MSC_VER
#    define _CRT_RAND_S
#endif

#include <ImathConfig.h>
#include <half.h>

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#ifdef _MSC_VER
#    include <windows.h>
#else
#    include <time.h>
#endif

#include <memory>

using namespace IMATH_NAMESPACE;

#ifdef IMATH_ENABLE_HALF_LOOKUP_TABLES
static inline float table_half_cast(const half &h)
{
    return imath_half_to_float_table[h.bits()].f;
}

static inline half exptable_half_constructor(float f)
{
    half ret;
    imath_half_uif x;

    x.f = f;

    if (f == 0)
    {
        //
        // Common special case - zero.
        // Preserve the zero's sign bit.
        //

        ret.setBits( (x.i >> 16) );
    }
    else
    {
        //
        // We extract the combined sign and exponent, e, from our
        // floating-point number, f.  Then we convert e to the sign
        // and exponent of the half number via a table lookup.
        //
        // For the most common case, where a normalized half is produced,
        // the table lookup returns a non-zero value; in this case, all
        // we have to do is round f's significand to 10 bits and combine
        // the result with e.
        //
        // For all other cases (overflow, zeroes, denormalized numbers
        // resulting from underflow, infinities and NANs), the table
        // lookup returns zero, and we call a longer, non-inline function
        // to do the float-to-half conversion.
        //

        int e = (x.i >> 23) & 0x000001ff;

        e = imath_float_half_exp_table[e];

        if (e)
        {
            //
            // Simple case - round the significand, m, to 10
            // bits and combine it with the sign and exponent.
            //

            int m = x.i & 0x007fffff;
            ret.setBits (e + ((m + 0x00000fff + ((m >> 13) & 1)) >> 13));
        }
        else
        {
            //
            // Difficult case - call a function.
            //

            ret.setBits (half::long_convert (x.i));
        }
    }
    return ret;
}
#else
// provide a wrapping function for consistency/readability
static inline float table_half_cast(const half &h)
{
    return static_cast<float>( h );
}

static inline half exptable_half_constructor(float f)
{
    return half {f};
}

#endif

int64_t
get_ticks (void)
{
#ifdef _MSC_VER
    static uint64_t scale = 0;
    if (scale == 0)
    {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency (&freq);
        scale = (1000000000 / freq.QuadPart);
    }

    LARGE_INTEGER ticks;
    QueryPerformanceCounter (&ticks);
    return ticks.QuadPart * scale;
#else
    struct timespec t;
    uint64_t nsecs;

    static uint64_t start = 0;
    if (start == 0)
    {
        clock_gettime (CLOCK_MONOTONIC, &t);
        start = t.tv_sec;
    }

    clock_gettime (CLOCK_MONOTONIC, &t);
    nsecs = (t.tv_sec - start) * 1000000000;
    nsecs += t.tv_nsec;
    return nsecs;
#endif
}

void
perf_test_half_to_float (float* floats, const uint16_t* halfs, int numentries)
{
    const half* halfvals = reinterpret_cast<const half*> (halfs);

    int64_t st = get_ticks();
    for (int i = 0; i < numentries; ++i)
        floats[i] = imath_half_to_float (halfs[i]);
    int64_t et = get_ticks();

    int64_t ost = get_ticks();
    for (int i = 0; i < numentries; ++i)
        floats[i] = table_half_cast (halfvals[i]);
    int64_t oet = get_ticks();

    int64_t onanos = (oet - ost);
    int64_t nnanos = (et - st);
    fprintf (stderr,
             "half -> float Old: %10lld (%g ns) New: %10lld (%g ns) (%10lld)\n",
             (long long) onanos,
             (double) onanos / ((double) numentries),
             (long long) nnanos,
             (double) nnanos / ((double) numentries),
             ((long long) (onanos - nnanos)));
}

void
perf_test_float_to_half (uint16_t* halfs, const float* floats, int numentries)
{
    half* halfvals = reinterpret_cast<half*> (halfs);

    int64_t st = get_ticks();
    for (int i = 0; i < numentries; ++i)
        halfs[i] = imath_float_to_half (floats[i]);
    int64_t et = get_ticks();

    int64_t ost = get_ticks();
    for (int i = 0; i < numentries; ++i)
        halfvals[i] = exptable_half_constructor (floats[i]);
    int64_t oet = get_ticks();

    int64_t onanos = (oet - ost);
    int64_t nnanos = (et - st);
    fprintf (stderr,
             "float -> half Old: %10lld (%g ns) New: %10lld (%g ns) (%10lld)\n",
             (long long) onanos,
             (double) onanos / ((double) numentries),
             (long long) nnanos,
             (double) nnanos / ((double) numentries),
             ((long long) (onanos - nnanos)));
}

int
main (int argc, char* argv[])
{
    int ret        = 0;
    int numentries = 1920 * 1080 * 3;
    if (argc > 1)
    {
        numentries = atoi (argv[1]);

        if (numentries <= 0)
        {
            fprintf (stderr, "Bad entry count '%s'\n", argv[1]);
            ret = 1;
        }
    }

    if (numentries > 0)
    {
        uint16_t* halfs = new uint16_t[numentries];
        float* floats   = new float[numentries];

        if (halfs && floats)
        {
            srand (numentries);
            for (int i = 0; i < numentries; ++i)
            {
                halfs[i]  = (uint16_t) (rand());
                floats[i] = imath_half_to_float (halfs[i]);
            }
            perf_test_half_to_float (floats, halfs, numentries);

            // test float -> half with real-world values
#ifdef _MSC_VER
            unsigned int rv;
            for (int i = 0; i < numentries; ++i)
            {
                rand_s( &rv );
                floats[i] = 65504.0 * (((double) rand() / (double) UINT_MAX) * 2.0 - 1.0);
            }
#else
            for (int i = 0; i < numentries; ++i)
                floats[i] = 65504.0 * (drand48() * 2.0 - 1.0);
#endif
            perf_test_float_to_half (halfs, floats, numentries);
        }

        delete[] halfs;
        delete[] floats;
    }

    return ret;
}
