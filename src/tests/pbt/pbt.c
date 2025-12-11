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
    state->initial_seed = state->seed;
    state->iteration = 0;
    state->shrink_count = 0;
    state->failed = false;
    state->is_shrinking = false;
    state->failure_msg = NULL;
    state->file = NULL;
    state->line = 0;
    state->counterexample_count = 0;
    for (int i = 0; i < 8; i++) {
        state->counterexample_values[i] = 0;
    }
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
    pbt_state_t failed_state;  // Save state for detailed reporting
    memset(&failed_state, 0, sizeof(failed_state));
    
    for (uint32_t i = 0; i < iterations; i++) {
        state.iteration = i;
        state.failed = false;
        state.failure_msg = NULL;
        state.counterexample_count = 0;
        
        // Save seed before this iteration for reproducibility
        uint64_t iter_seed = state.seed;
        state.initial_seed = iter_seed;
        
        // Run the property
        property(&state);
        
        g_pbt_stats.total_iterations++;
        
        if (state.failed) {
            all_passed = false;
            failed_state = state;  // Copy state for reporting
            
#if PBT_SHRINK_ENABLED
            // Attempt to shrink the counterexample
            uint32_t shrink_attempts = 0;
            uint64_t shrink_seed = iter_seed;
            
            while (shrink_attempts < PBT_MAX_SHRINK_ATTEMPTS) {
                // Try a "smaller" seed by reducing it
                uint64_t try_seed = shrink_seed / 2;
                if (try_seed == 0 || try_seed == shrink_seed) {
                    break;
                }
                
                pbt_state_t shrink_state;
                pbt_state_init(&shrink_state, try_seed);
                shrink_state.iteration = i;
                shrink_state.is_shrinking = true;
                shrink_state.shrink_count = shrink_attempts + 1;
                
                property(&shrink_state);
                
                if (shrink_state.failed) {
                    // Found a smaller failing case
                    shrink_seed = try_seed;
                    failed_state = shrink_state;
                }
                
                shrink_attempts++;
            }
            
            failed_state.shrink_count = shrink_attempts;
#endif
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
        
        // Print enhanced failure diagnostics
        pbt_print_failure_diagnostics(&failed_state, name);
        
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
// Counterexample Tracking
// ============================================================================

void pbt_record_value(pbt_state_t *state, uint64_t value) {
    if (state->counterexample_count < 8) {
        state->counterexample_values[state->counterexample_count++] = value;
    }
}

// ============================================================================
// Enhanced Failure Reporting
// ============================================================================

void pbt_print_failure_diagnostics(pbt_state_t *state, const char *name) {
    kprintf("\n");
    kconsole_set_color(KCOLOR_LIGHT_RED, KCOLOR_BLACK);
    kprintf("================================================================================\n");
    kprintf("PROPERTY TEST FAILURE DIAGNOSTICS\n");
    kprintf("================================================================================\n");
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
    
    kprintf("Property:     %s\n", name);
    kprintf("Iteration:    %u\n", state->iteration);
    kprintf("Seed:         0x%llx\n", (unsigned long long)state->initial_seed);
    
    if (state->shrink_count > 0) {
        kprintf("Shrink attempts: %u\n", state->shrink_count);
        kprintf("Shrunk seed:  0x%llx (use this seed to reproduce minimal case)\n", 
                (unsigned long long)state->initial_seed);
    }
    
    if (state->file) {
        kprintf("Location:     %s:%d\n", state->file, state->line);
    }
    
    if (state->failure_msg) {
        kprintf("Assertion:    %s\n", state->failure_msg);
    }
    
    // Print recorded counterexample values
    if (state->counterexample_count > 0) {
        kprintf("\nCounterexample values:\n");
        for (uint32_t i = 0; i < state->counterexample_count; i++) {
            kprintf("  [%u]: %llu (0x%llx)\n", i, 
                    (unsigned long long)state->counterexample_values[i],
                    (unsigned long long)state->counterexample_values[i]);
        }
    }
    
    // Print reproduction hint
    kprintf("\nTo reproduce this failure:\n");
    kprintf("  1. Use seed 0x%llx in pbt_state_init()\n", 
            (unsigned long long)state->initial_seed);
    kprintf("  2. Run iteration %u\n", state->iteration);
    
    kconsole_set_color(KCOLOR_LIGHT_RED, KCOLOR_BLACK);
    kprintf("================================================================================\n");
    kconsole_set_color(KCOLOR_WHITE, KCOLOR_BLACK);
    kprintf("\n");
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
