// ============================================================================
// pbt.c - Property-Based Testing Framework Implementation
// ============================================================================
//
// A lightweight property-based testing framework for CastorOS kernel.
// Uses xorshift64 PRNG for fast, high-quality random number generation.
//
// **Feature: multi-arch-support**
// **Validates: Requirements 11.3**
// ============================================================================

#include <tests/pbt.h>
#include <tests/ktest.h>
#include <lib/kprintf.h>
#include <lib/string.h>

// ============================================================================
// Global State
// ============================================================================

static pbt_stats_t g_pbt_stats;
static bool g_pbt_initialized = false;

// Default seed based on a fixed value for reproducibility
// In a real system, this could be seeded from a hardware RNG or timer
#define PBT_DEFAULT_SEED 0x123456789ABCDEF0ULL

// ============================================================================
// PRNG Implementation (xorshift64)
// ============================================================================

/**
 * @brief xorshift64 random number generator
 * 
 * This is a fast, high-quality PRNG with a period of 2^64-1.
 * Reference: Marsaglia, G. (2003). "Xorshift RNGs"
 */
static uint64_t xorshift64(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

// ============================================================================
// Core Functions
// ============================================================================

void pbt_init(void) {
    if (g_pbt_initialized) {
        return;
    }
    
    g_pbt_stats.total_properties = 0;
    g_pbt_stats.passed_properties = 0;
    g_pbt_stats.failed_properties = 0;
    g_pbt_stats.total_iterations = 0;
    
    g_pbt_initialized = true;
}

void pbt_state_init(pbt_state_t *state, uint64_t seed) {
    // Ensure seed is non-zero (xorshift requires non-zero state)
    state->seed = (seed != 0) ? seed : PBT_DEFAULT_SEED;
    state->iteration = 0;
    state->shrink_count = 0;
    state->failed = false;
    state->failure_msg = NULL;
    state->file = NULL;
    state->line = 0;
}


bool pbt_run_property(const char *name, pbt_property_fn property, uint32_t iterations) {
    if (!g_pbt_initialized) {
        pbt_init();
    }
    
    pbt_state_t state;
    uint64_t seed = PBT_DEFAULT_SEED;
    
    // Mix the property name into the seed for variety
    for (const char *p = name; *p; p++) {
        seed = seed * 31 + (uint8_t)*p;
    }
    
    pbt_state_init(&state, seed);
    
    g_pbt_stats.total_properties++;
    
    kprintf("  [ PBT  ] %s (%u iterations)\n", name, iterations);
    
    bool all_passed = true;
    uint32_t failed_iteration = 0;
    uint64_t failed_seed = 0;
    
    for (uint32_t i = 0; i < iterations; i++) {
        state.iteration = i;
        state.failed = false;
        state.failure_msg = NULL;
        
        // Save seed before this iteration for reproducibility
        uint64_t iter_seed = state.seed;
        
        // Run the property
        property(&state);
        
        g_pbt_stats.total_iterations++;
        
        if (state.failed) {
            all_passed = false;
            failed_iteration = i;
            failed_seed = iter_seed;
            break;
        }
    }
    
    if (all_passed) {
        g_pbt_stats.passed_properties++;
        kprintf("  ");
        kconsole_set_color(KCOLOR_LIGHT_GREEN, KCOLOR_BLACK);
        kprintf("[  OK  ]");
        kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
        kprintf(" %s: %u iterations passed\n", name, iterations);
        return true;
    } else {
        g_pbt_stats.failed_properties++;
        kprintf("  ");
        kconsole_set_color(KCOLOR_LIGHT_RED, KCOLOR_BLACK);
        kprintf("[ FAIL ]");
        kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
        kprintf(" %s: failed at iteration %u (seed: 0x%llx)\n", 
                name, failed_iteration, (unsigned long long)failed_seed);
        
        if (state.failure_msg) {
            kprintf("    Assertion: %s\n", state.failure_msg);
        }
        if (state.file) {
            kprintf("    Location: %s:%d\n", state.file, state.line);
        }
        
        return false;
    }
}

pbt_stats_t pbt_get_stats(void) {
    return g_pbt_stats;
}

void pbt_print_summary(void) {
    kprintf("\n");
    kconsole_set_color(KCOLOR_LIGHT_CYAN, KCOLOR_BLACK);
    kprintf("================================================================================\n");
    kprintf("Property-Based Testing Summary\n");
    kprintf("================================================================================\n");
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
    
    kprintf("Total properties:     %u\n", g_pbt_stats.total_properties);
    kprintf("Passed properties:    ");
    if (g_pbt_stats.passed_properties > 0) {
        kconsole_set_color(KCOLOR_LIGHT_GREEN, KCOLOR_BLACK);
    }
    kprintf("%u", g_pbt_stats.passed_properties);
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
    kprintf("\n");
    
    kprintf("Failed properties:    ");
    if (g_pbt_stats.failed_properties > 0) {
        kconsole_set_color(KCOLOR_LIGHT_RED, KCOLOR_BLACK);
    }
    kprintf("%u", g_pbt_stats.failed_properties);
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
    kprintf("\n");
    
    kprintf("Total iterations:     %u\n", g_pbt_stats.total_iterations);
    
    kprintf("\nResult: ");
    if (g_pbt_stats.failed_properties == 0) {
        kconsole_set_color(KCOLOR_LIGHT_GREEN, KCOLOR_BLACK);
        kprintf("ALL PROPERTIES PASSED");
    } else {
        kconsole_set_color(KCOLOR_LIGHT_RED, KCOLOR_BLACK);
        kprintf("SOME PROPERTIES FAILED");
    }
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
    kprintf("\n");
    
    kconsole_set_color(KCOLOR_LIGHT_CYAN, KCOLOR_BLACK);
    kprintf("================================================================================\n");
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
}

// ============================================================================
// Random Generators
// ============================================================================

uint64_t pbt_gen_uint64(pbt_state_t *state) {
    return xorshift64(&state->seed);
}

uint32_t pbt_gen_uint32(pbt_state_t *state) {
    return (uint32_t)xorshift64(&state->seed);
}

uint16_t pbt_gen_uint16(pbt_state_t *state) {
    return (uint16_t)xorshift64(&state->seed);
}

uint8_t pbt_gen_uint8(pbt_state_t *state) {
    return (uint8_t)xorshift64(&state->seed);
}

int32_t pbt_gen_int32(pbt_state_t *state) {
    return (int32_t)xorshift64(&state->seed);
}

bool pbt_gen_bool(pbt_state_t *state) {
    return (xorshift64(&state->seed) & 1) != 0;
}

uint32_t pbt_gen_uint32_range(pbt_state_t *state, uint32_t min, uint32_t max) {
    if (min >= max) {
        return min;
    }
    uint64_t range = (uint64_t)(max - min) + 1;
    uint64_t random = xorshift64(&state->seed);
    return min + (uint32_t)(random % range);
}

uint64_t pbt_gen_uint64_range(pbt_state_t *state, uint64_t min, uint64_t max) {
    if (min >= max) {
        return min;
    }
    uint64_t range = max - min + 1;
    uint64_t random = xorshift64(&state->seed);
    // Handle potential overflow in range calculation
    if (range == 0) {
        // Full range, just return random
        return random;
    }
    return min + (random % range);
}

int32_t pbt_gen_int32_range(pbt_state_t *state, int32_t min, int32_t max) {
    if (min >= max) {
        return min;
    }
    uint32_t range = (uint32_t)(max - min) + 1;
    uint64_t random = xorshift64(&state->seed);
    return min + (int32_t)(random % range);
}

uintptr_t pbt_gen_page_aligned(pbt_state_t *state, uintptr_t min, uintptr_t max) {
    // Align min up to page boundary
    uintptr_t aligned_min = (min + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    // Align max down to page boundary
    uintptr_t aligned_max = max & ~(PAGE_SIZE - 1);
    
    if (aligned_min > aligned_max) {
        return aligned_min;
    }
    
    // Generate random page number
    uintptr_t num_pages = (aligned_max - aligned_min) / PAGE_SIZE + 1;
    uint64_t random = xorshift64(&state->seed);
    uintptr_t page_offset = (random % num_pages) * PAGE_SIZE;
    
    return aligned_min + page_offset;
}

uint32_t pbt_gen_choice(pbt_state_t *state, uint32_t count) {
    if (count == 0) {
        return 0;
    }
    return pbt_gen_uint32_range(state, 0, count - 1);
}

void pbt_gen_bytes(pbt_state_t *state, void *buffer, size_t size) {
    uint8_t *bytes = (uint8_t *)buffer;
    
    // Generate 8 bytes at a time
    while (size >= 8) {
        uint64_t random = xorshift64(&state->seed);
        memcpy(bytes, &random, 8);
        bytes += 8;
        size -= 8;
    }
    
    // Handle remaining bytes
    if (size > 0) {
        uint64_t random = xorshift64(&state->seed);
        memcpy(bytes, &random, size);
    }
}

// ============================================================================
// Internal Failure Functions
// ============================================================================

void _pbt_fail(pbt_state_t *state, const char *msg, const char *file, int line) {
    state->failed = true;
    state->failure_msg = msg;
    state->file = file;
    state->line = line;
}

void _pbt_fail_eq(pbt_state_t *state, uint64_t expected, uint64_t actual,
                  const char *expected_str, const char *actual_str,
                  const char *file, int line) {
    state->failed = true;
    state->file = file;
    state->line = line;
    
    // Print detailed comparison
    kprintf("    Expected: %s = %llu (0x%llx)\n", 
            expected_str, (unsigned long long)expected, (unsigned long long)expected);
    kprintf("    Actual:   %s = %llu (0x%llx)\n", 
            actual_str, (unsigned long long)actual, (unsigned long long)actual);
    
    state->failure_msg = "values not equal";
}

void _pbt_fail_ne(pbt_state_t *state, uint64_t value,
                  const char *expected_str, const char *actual_str,
                  const char *file, int line) {
    state->failed = true;
    state->file = file;
    state->line = line;
    
    kprintf("    Expected: %s != %s\n", expected_str, actual_str);
    kprintf("    Both equal: %llu (0x%llx)\n", 
            (unsigned long long)value, (unsigned long long)value);
    
    state->failure_msg = "values unexpectedly equal";
}
