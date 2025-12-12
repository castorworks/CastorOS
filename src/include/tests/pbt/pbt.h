// ============================================================================
// pbt.h - Property-Based Testing Framework
// ============================================================================
//
// A lightweight property-based testing framework for CastorOS kernel.
//
// **Feature: multi-arch-support**
// **Validates: Requirements 11.3**
// ============================================================================

#ifndef _TESTS_PBT_PBT_H_
#define _TESTS_PBT_PBT_H_

#include <types.h>
#include <lib/kprintf.h>

// ============================================================================
// Configuration
// ============================================================================

#ifndef PBT_DEFAULT_ITERATIONS
#define PBT_DEFAULT_ITERATIONS 100
#endif

#ifndef PBT_SHRINK_ENABLED
#define PBT_SHRINK_ENABLED 1
#endif

#ifndef PBT_MAX_SHRINK_ATTEMPTS
#define PBT_MAX_SHRINK_ATTEMPTS 100
#endif

// ============================================================================
// Types
// ============================================================================

typedef struct pbt_state {
    uint64_t seed;
    uint64_t initial_seed;
    uint32_t iteration;
    uint32_t shrink_count;
    bool failed;
    bool is_shrinking;
    const char *failure_msg;
    const char *file;
    int line;
    uint64_t counterexample_values[8];
    uint32_t counterexample_count;
} pbt_state_t;

typedef struct pbt_stats {
    uint32_t total_properties;
    uint32_t passed_properties;
    uint32_t failed_properties;
    uint32_t total_iterations;
} pbt_stats_t;

typedef void (*pbt_property_fn)(pbt_state_t *state);


// ============================================================================
// Core Functions
// ============================================================================

void pbt_init(void);
void pbt_state_init(pbt_state_t *state, uint64_t seed);
bool pbt_run_property(const char *name, pbt_property_fn property, uint32_t iterations);
pbt_stats_t pbt_get_stats(void);
void pbt_print_summary(void);

// ============================================================================
// Random Generators
// ============================================================================

uint64_t pbt_gen_uint64(pbt_state_t *state);
uint32_t pbt_gen_uint32(pbt_state_t *state);
uint16_t pbt_gen_uint16(pbt_state_t *state);
uint8_t pbt_gen_uint8(pbt_state_t *state);
int32_t pbt_gen_int32(pbt_state_t *state);
bool pbt_gen_bool(pbt_state_t *state);
uint32_t pbt_gen_uint32_range(pbt_state_t *state, uint32_t min, uint32_t max);
uint64_t pbt_gen_uint64_range(pbt_state_t *state, uint64_t min, uint64_t max);
int32_t pbt_gen_int32_range(pbt_state_t *state, int32_t min, int32_t max);
uintptr_t pbt_gen_page_aligned(pbt_state_t *state, uintptr_t min, uintptr_t max);
uint32_t pbt_gen_choice(pbt_state_t *state, uint32_t count);
void pbt_gen_bytes(pbt_state_t *state, void *buffer, size_t size);

// ============================================================================
// Counterexample Tracking
// ============================================================================

void pbt_record_value(pbt_state_t *state, uint64_t value);
void pbt_print_failure_diagnostics(pbt_state_t *state, const char *name);

// ============================================================================
// Internal Failure Functions
// ============================================================================

void _pbt_fail(pbt_state_t *state, const char *msg, const char *file, int line);
void _pbt_fail_eq(pbt_state_t *state, uint64_t expected, uint64_t actual,
                  const char *expected_str, const char *actual_str,
                  const char *file, int line);
void _pbt_fail_ne(pbt_state_t *state, uint64_t value,
                  const char *expected_str, const char *actual_str,
                  const char *file, int line);

// ============================================================================
// Macros
// ============================================================================

#define PBT_PROPERTY(name) \
    static void pbt_property_##name(pbt_state_t *_pbt_state)

#define PBT_RUN(name, iterations) \
    pbt_run_property(#name, pbt_property_##name, iterations)

#define PBT_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            _pbt_fail(_pbt_state, #cond, __FILE__, __LINE__); \
            return; \
        } \
    } while (0)

#define PBT_ASSERT_EQ(expected, actual) \
    do { \
        uint64_t _e = (uint64_t)(expected); \
        uint64_t _a = (uint64_t)(actual); \
        if (_e != _a) { \
            _pbt_fail_eq(_pbt_state, _e, _a, #expected, #actual, __FILE__, __LINE__); \
            return; \
        } \
    } while (0)

#define PBT_ASSERT_NE(expected, actual) \
    do { \
        uint64_t _e = (uint64_t)(expected); \
        uint64_t _a = (uint64_t)(actual); \
        if (_e == _a) { \
            _pbt_fail_ne(_pbt_state, _e, #expected, #actual, __FILE__, __LINE__); \
            return; \
        } \
    } while (0)

#define PBT_ASSERT_TRUE(cond) PBT_ASSERT(cond)
#define PBT_ASSERT_FALSE(cond) PBT_ASSERT(!(cond))

#define PBT_GEN_UINT32() pbt_gen_uint32(_pbt_state)
#define PBT_GEN_UINT64() pbt_gen_uint64(_pbt_state)
#define PBT_GEN_UINT32_RANGE(min, max) pbt_gen_uint32_range(_pbt_state, min, max)
#define PBT_GEN_UINT64_RANGE(min, max) pbt_gen_uint64_range(_pbt_state, min, max)
#define PBT_GEN_INT32_RANGE(min, max) pbt_gen_int32_range(_pbt_state, min, max)
#define PBT_GEN_BOOL() pbt_gen_bool(_pbt_state)
#define PBT_GEN_CHOICE(count) pbt_gen_choice(_pbt_state, count)
#define PBT_GEN_PAGE_ALIGNED(min, max) pbt_gen_page_aligned(_pbt_state, min, max)

#define PBT_RECORD(value) pbt_record_value(_pbt_state, (uint64_t)(value))

#endif // _TESTS_PBT_PBT_H_
