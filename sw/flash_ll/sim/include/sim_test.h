// Very small test harness macros
#ifndef SIM_TEST_H
#define SIM_TEST_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int sim_test_failures = 0;

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "ASSERT_TRUE failed: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
        sim_test_failures++; \
    } \
} while(0)

#define ASSERT_EQ_U32(a,b) do { \
    uint32_t _av = (uint32_t)(a); \
    uint32_t _bv = (uint32_t)(b); \
    if (_av != _bv) { \
        fprintf(stderr, "ASSERT_EQ_U32 failed: %s(%u) != %s(%u) at %s:%d\n", #a, _av, #b, _bv, __FILE__, __LINE__); \
        sim_test_failures++; \
    } \
} while(0)

#define ASSERT_EQ_U8(a,b) do { \
    uint8_t _av = (uint8_t)(a); \
    uint8_t _bv = (uint8_t)(b); \
    if (_av != _bv) { \
        fprintf(stderr, "ASSERT_EQ_U8 failed: %s(%u) != %s(%u) at %s:%d\n", #a, _av, #b, _bv, __FILE__, __LINE__); \
        sim_test_failures++; \
    } \
} while(0)

#define ASSERT_MEMEQ(a,b,len) do { \
    if (memcmp((a),(b),(len)) != 0) { \
        fprintf(stderr, "ASSERT_MEMEQ failed at %s:%d (len=%zu)\n", __FILE__, __LINE__, (size_t)(len)); \
        sim_test_failures++; \
    } \
} while(0)

#define TEST_CASE(name) static void name(void)

#define RUN_TEST(name) do { \
    int _prev_fail = sim_test_failures; \
    fprintf(stdout, "[ RUN      ] %s\n", #name); \
    name(); \
    int _delta = sim_test_failures - _prev_fail; \
    if (_delta == 0) { \
        fprintf(stdout, "[       OK ] %s\n", #name); \
    } else { \
        fprintf(stdout, "[  FAILED  ] %s (%d failure%s)\n", #name, _delta, _delta==1?"":"s"); \
    } \
} while(0)

#endif // SIM_TEST_H
