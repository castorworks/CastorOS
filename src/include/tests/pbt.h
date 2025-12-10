// ============================================================================
// pbt.h - Property-Based Testing Framework
// ============================================================================
//
// A lightweight property-based testing framework for CastorOS kernel.
// Inspired by QuickCheck, this framework allows testing properties that
// should hold for all valid inputs by generating random test cases.
//
// **Feature: multi-arch-support**
// **Validates: Requirements 11.3**
//
// Usage:
//   PBT_PROPERTY(test_name) {
//       // Generate random inputs
//       uint32_t x = pbt_gen_uint32(state);
//       uint32_t y = pbt_gen_uint32_range(state, 0, 100);
//       
//       // Test property
//       PBT_ASSERT(some_property(x, y));
//   }
//
//   void run_tests(void) {
//       PBT_RUN(test_name, 100);  // Run 100 iterations
//   }
// ============================================================================

#ifndef _TESTS_PBT_H_
#define _TESTS_PBT_H_

#include <types.h>

// ============================================================================
// Configuration
// ============================================================================

#define PBT_DEFAULT_ITERATIONS  100
#define PBT_MAX_SHRINK_ATTEMPTS 50

// ============================================================================
// Random Number Generator State
// ============================================================================

/**
 * @brief PBT random state using xorshift64 algorithm
 * 
 * This provides a fast, high-quality PRNG suitable for property testing.
 * The state should be seeded once and passed to all generators.
 */
typedef struct pbt_state {
    uint64_t seed;           // Current PRNG state
    uint32_t iteration;      // Current test iteration
    uint32_t shrink_count;   // Number of shrink attempts
    bool     failed;         // Whether current test failed
    const char *failure_msg; // Failure message
    const char *file;        // File where failure occurred
    int      line;           // Line where failure occurred
} pbt_state_t;

// ============================================================================
// Test Statistics
// ============================================================================

typedef struct pbt_stats {
    uint32_t total_properties;   // Total properties tested
    uint32_t passed_properties;  // Properties that passed
    uint32_t failed_properties;  // Properties that failed
    uint32_t total_iterations;   // Total test iterations run
} pbt_stats_t;

// ============================================================================
// Property Function Type
// ============================================================================

typedef void (*pbt_property_fn)(pbt_state_t *state);

// ============================================================================
// Core Functions
// ============================================================================

/**
 * @brief Initialize the PBT framework
 */
void pbt_init(void);

/**
 * @brief Initialize a PBT state with a seed
 * @param state State to initialize
 * @param seed Initial seed value
 */
void pbt_state_init(pbt_state_t *state, uint64_t seed);

/**
 * @brief Run a property test for a given number of iterations
 * @param name Property name for reporting
 * @param property Property function to test
 * @param iterations Number of random test cases to generate
 * @return true if all iterations passed, false otherwise
 */
bool pbt_run_property(const char *name, pbt_property_fn property, uint32_t iterations);

/**
 * @brief Get current PBT statistics
 * @return Statistics structure
 */
pbt_stats_t pbt_get_stats(void);

/**
 * @brief Print PBT summary
 */
void pbt_print_summary(void);

// ============================================================================
// Random Generators
// ============================================================================

/**
 * @brief Generate a random 64-bit unsigned integer
 */
uint64_t pbt_gen_uint64(pbt_state_t *state);

/**
 * @brief Generate a random 32-bit unsigned integer
 */
uint32_t pbt_gen_uint32(pbt_state_t *state);

/**
 * @brief Generate a random 16-bit unsigned integer
 */
uint16_t pbt_gen_uint16(pbt_state_t *state);

/**
 * @brief Generate a random 8-bit unsigned integer
 */
uint8_t pbt_gen_uint8(pbt_state_t *state);

/**
 * @brief Generate a random 32-bit signed integer
 */
int32_t pbt_gen_int32(pbt_state_t *state);

/**
 * @brief Generate a random boolean
 */
bool pbt_gen_bool(pbt_state_t *state);

/**
 * @brief Generate a random uint32 in range [min, max] (inclusive)
 */
uint32_t pbt_gen_uint32_range(pbt_state_t *state, uint32_t min, uint32_t max);

/**
 * @brief Generate a random uint64 in range [min, max] (inclusive)
 */
uint64_t pbt_gen_uint64_range(pbt_state_t *state, uint64_t min, uint64_t max);

/**
 * @brief Generate a random int32 in range [min, max] (inclusive)
 */
int32_t pbt_gen_int32_range(pbt_state_t *state, int32_t min, int32_t max);

/**
 * @brief Generate a page-aligned address
 */
uintptr_t pbt_gen_page_aligned(pbt_state_t *state, uintptr_t min, uintptr_t max);

/**
 * @brief Generate a random element from an array
 * @param state PBT state
 * @param array Array of elements
 * @param count Number of elements in array
 * @return Index of selected element
 */
uint32_t pbt_gen_choice(pbt_state_t *state, uint32_t count);

/**
 * @brief Fill a buffer with random bytes
 */
void pbt_gen_bytes(pbt_state_t *state, void *buffer, size_t size);

// ============================================================================
// Assertion Macros
// ============================================================================

/**
 * @brief Assert a property condition
 * 
 * If the condition is false, marks the current test as failed and records
 * the failure location.
 */
#define PBT_ASSERT(condition) \
    do { \
        if (!(condition)) { \
            _pbt_fail(state, #condition, __FILE__, __LINE__); \
            return; \
        } \
    } while (0)

/**
 * @brief Assert with custom message
 */
#define PBT_ASSERT_MSG(condition, msg) \
    do { \
        if (!(condition)) { \
            _pbt_fail(state, msg, __FILE__, __LINE__); \
            return; \
        } \
    } while (0)

/**
 * @brief Assert two values are equal
 */
#define PBT_ASSERT_EQ(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            _pbt_fail_eq(state, (uint64_t)(expected), (uint64_t)(actual), \
                        #expected, #actual, __FILE__, __LINE__); \
            return; \
        } \
    } while (0)

/**
 * @brief Assert two values are not equal
 */
#define PBT_ASSERT_NE(expected, actual) \
    do { \
        if ((expected) == (actual)) { \
            _pbt_fail_ne(state, (uint64_t)(expected), #expected, #actual, \
                        __FILE__, __LINE__); \
            return; \
        } \
    } while (0)

// ============================================================================
// Property Definition Macros
// ============================================================================

/**
 * @brief Define a property test function
 * 
 * Usage:
 *   PBT_PROPERTY(my_property) {
 *       uint32_t x = pbt_gen_uint32(state);
 *       PBT_ASSERT(x == x);
 *   }
 */
#define PBT_PROPERTY(name) \
    static void pbt_prop_##name(pbt_state_t *state)

/**
 * @brief Run a property test
 * 
 * Usage:
 *   PBT_RUN(my_property, 100);  // Run 100 iterations
 */
#define PBT_RUN(name, iterations) \
    pbt_run_property(#name, pbt_prop_##name, iterations)

/**
 * @brief Run a property test with default iterations
 */
#define PBT_RUN_DEFAULT(name) \
    PBT_RUN(name, PBT_DEFAULT_ITERATIONS)

// ============================================================================
// Internal Functions (do not call directly)
// ============================================================================

void _pbt_fail(pbt_state_t *state, const char *msg, const char *file, int line);
void _pbt_fail_eq(pbt_state_t *state, uint64_t expected, uint64_t actual,
                  const char *expected_str, const char *actual_str,
                  const char *file, int line);
void _pbt_fail_ne(pbt_state_t *state, uint64_t value,
                  const char *expected_str, const char *actual_str,
                  const char *file, int line);

#endif // _TESTS_PBT_H_
