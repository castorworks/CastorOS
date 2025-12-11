// ============================================================================
// pbt_patterns.h - Common Property Pattern Macros
// ============================================================================
//
// Provides macros for common property-based testing patterns:
// - Round-trip: encode(decode(x)) == x
// - Invariant: property preserved after operation
// - Idempotent: f(f(x)) == f(x)
//
// These patterns reduce boilerplate and ensure consistent property testing.
//
// **Feature: test-refactor**
// **Validates: Requirements 2.4**
//
// Usage:
//   // Define a round-trip property for serialization
//   PBT_ROUNDTRIP_PROPERTY(pte_roundtrip,
//       paddr_t, pbt_gen_paddr,
//       MAKE_PTE(input, PTE_FLAG_PRESENT), PTE_ADDR,
//       ==)
//
//   // Define an invariant property
//   PBT_INVARIANT_PROPERTY(size_invariant,
//       uint32_t, pbt_gen_uint32,
//       some_operation, get_size,
//       ==)
// ============================================================================

#ifndef _TESTS_PBT_PATTERNS_H_
#define _TESTS_PBT_PATTERNS_H_

#include <tests/pbt.h>

// ============================================================================
// Round-Trip Property Pattern
// ============================================================================
//
// Tests that encoding then decoding (or vice versa) returns the original value.
// This is essential for serialization, parsing, and address manipulation.
//
// Pattern: decode(encode(x)) == x
// ============================================================================

/**
 * @brief Define a round-trip property test
 * 
 * Tests that applying an encode function followed by a decode function
 * returns the original value.
 * 
 * @param name Property name
 * @param type Input type
 * @param generator Generator function for input type
 * @param encode Expression to encode input (use 'input' variable)
 * @param decode Expression to decode encoded value (use 'encoded' variable)
 * @param compare Comparison operator (usually ==)
 * 
 * Example:
 *   PBT_ROUNDTRIP_PROPERTY(pte_addr_roundtrip,
 *       paddr_t, pbt_gen_paddr,
 *       MAKE_PTE(input, PTE_FLAG_PRESENT), PTE_ADDR(encoded),
 *       ==)
 */
#define PBT_ROUNDTRIP_PROPERTY(name, type, generator, encode, decode, compare) \
    PBT_PROPERTY(name) { \
        type input = generator(state); \
        __typeof__(encode) encoded = (encode); \
        type decoded = (decode); \
        PBT_ASSERT(input compare decoded); \
    }

/**
 * @brief Define a round-trip property with custom equality check
 * 
 * @param name Property name
 * @param type Input type
 * @param generator Generator function
 * @param encode Encode expression
 * @param decode Decode expression
 * @param eq_func Equality function: bool eq_func(type a, type b)
 */
#define PBT_ROUNDTRIP_PROPERTY_EQ(name, type, generator, encode, decode, eq_func) \
    PBT_PROPERTY(name) { \
        type input = generator(state); \
        __typeof__(encode) encoded = (encode); \
        type decoded = (decode); \
        PBT_ASSERT(eq_func(input, decoded)); \
    }

/**
 * @brief Simpler round-trip macro for function pairs
 * 
 * @param name Property name
 * @param type Input type
 * @param generator Generator function
 * @param encode_fn Encode function: encoded_type encode_fn(type)
 * @param decode_fn Decode function: type decode_fn(encoded_type)
 */
#define PBT_ROUNDTRIP(name, type, generator, encode_fn, decode_fn) \
    PBT_PROPERTY(name) { \
        type input = generator(state); \
        type decoded = decode_fn(encode_fn(input)); \
        PBT_ASSERT_EQ(input, decoded); \
    }

// ============================================================================
// Invariant Property Pattern
// ============================================================================
//
// Tests that a property/measurement remains unchanged after an operation.
// Useful for testing that operations preserve certain characteristics.
//
// Pattern: measure(x) == measure(operation(x))
// ============================================================================

/**
 * @brief Define an invariant property test
 * 
 * Tests that a measurement/property is preserved after applying an operation.
 * 
 * @param name Property name
 * @param type Input type
 * @param generator Generator function
 * @param operation Operation to apply (expression using 'input')
 * @param measure Measurement function/expression (use 'value' variable)
 * @param compare Comparison operator
 * 
 * Example:
 *   // Test that page alignment is preserved
 *   PBT_INVARIANT_PROPERTY(alignment_preserved,
 *       paddr_t, pbt_gen_paddr,
 *       PADDR_ALIGN_DOWN(input), IS_PADDR_ALIGNED(value),
 *       ==)
 */
#define PBT_INVARIANT_PROPERTY(name, type, generator, operation, measure, compare) \
    PBT_PROPERTY(name) { \
        type input = generator(state); \
        type value = input; \
        __typeof__(measure) before = (measure); \
        value = (operation); \
        __typeof__(measure) after = (measure); \
        PBT_ASSERT(before compare after); \
    }

/**
 * @brief Simpler invariant macro for function-based measurement
 * 
 * @param name Property name
 * @param type Input type
 * @param generator Generator function
 * @param operation_fn Operation function: type operation_fn(type)
 * @param measure_fn Measurement function: measure_type measure_fn(type)
 */
#define PBT_INVARIANT(name, type, generator, operation_fn, measure_fn) \
    PBT_PROPERTY(name) { \
        type input = generator(state); \
        __typeof__(measure_fn(input)) before = measure_fn(input); \
        type result = operation_fn(input); \
        __typeof__(measure_fn(result)) after = measure_fn(result); \
        PBT_ASSERT_EQ(before, after); \
    }

// ============================================================================
// Idempotent Property Pattern
// ============================================================================
//
// Tests that applying an operation twice gives the same result as once.
// Useful for normalization, canonicalization, and cleanup operations.
//
// Pattern: f(x) == f(f(x))
// ============================================================================

/**
 * @brief Define an idempotent property test
 * 
 * Tests that applying an operation twice yields the same result as once.
 * 
 * @param name Property name
 * @param type Input/output type
 * @param generator Generator function
 * @param operation Operation expression (use 'input' variable)
 * @param compare Comparison operator
 * 
 * Example:
 *   // Test that page alignment is idempotent
 *   PBT_IDEMPOTENT_PROPERTY(align_idempotent,
 *       paddr_t, pbt_gen_paddr,
 *       PADDR_ALIGN_DOWN(input),
 *       ==)
 */
#define PBT_IDEMPOTENT_PROPERTY(name, type, generator, operation, compare) \
    PBT_PROPERTY(name) { \
        type input = generator(state); \
        type once = (operation); \
        input = once; \
        type twice = (operation); \
        PBT_ASSERT(once compare twice); \
    }

/**
 * @brief Simpler idempotent macro for functions
 * 
 * @param name Property name
 * @param type Input/output type
 * @param generator Generator function
 * @param operation_fn Operation function: type operation_fn(type)
 */
#define PBT_IDEMPOTENT(name, type, generator, operation_fn) \
    PBT_PROPERTY(name) { \
        type input = generator(state); \
        type once = operation_fn(input); \
        type twice = operation_fn(once); \
        PBT_ASSERT_EQ(once, twice); \
    }

/**
 * @brief Idempotent property with custom equality
 * 
 * @param name Property name
 * @param type Input/output type
 * @param generator Generator function
 * @param operation_fn Operation function
 * @param eq_func Equality function: bool eq_func(type a, type b)
 */
#define PBT_IDEMPOTENT_EQ(name, type, generator, operation_fn, eq_func) \
    PBT_PROPERTY(name) { \
        type input = generator(state); \
        type once = operation_fn(input); \
        type twice = operation_fn(once); \
        PBT_ASSERT(eq_func(once, twice)); \
    }

// ============================================================================
// Commutative Property Pattern
// ============================================================================
//
// Tests that order of operands doesn't matter.
//
// Pattern: f(a, b) == f(b, a)
// ============================================================================

/**
 * @brief Define a commutative property test
 * 
 * @param name Property name
 * @param type Operand type
 * @param generator Generator function
 * @param operation Binary operation expression (use 'a' and 'b' variables)
 * @param compare Comparison operator
 */
#define PBT_COMMUTATIVE_PROPERTY(name, type, generator, operation, compare) \
    PBT_PROPERTY(name) { \
        type a = generator(state); \
        type b = generator(state); \
        __typeof__(operation) result_ab = (operation); \
        type temp = a; a = b; b = temp; \
        __typeof__(operation) result_ba = (operation); \
        PBT_ASSERT(result_ab compare result_ba); \
    }

/**
 * @brief Simpler commutative macro for functions
 * 
 * @param name Property name
 * @param type Operand type
 * @param generator Generator function
 * @param operation_fn Binary function: result_type operation_fn(type, type)
 */
#define PBT_COMMUTATIVE(name, type, generator, operation_fn) \
    PBT_PROPERTY(name) { \
        type a = generator(state); \
        type b = generator(state); \
        PBT_ASSERT_EQ(operation_fn(a, b), operation_fn(b, a)); \
    }

// ============================================================================
// Associative Property Pattern
// ============================================================================
//
// Tests that grouping doesn't matter.
//
// Pattern: f(f(a, b), c) == f(a, f(b, c))
// ============================================================================

/**
 * @brief Define an associative property test
 * 
 * @param name Property name
 * @param type Operand type
 * @param generator Generator function
 * @param operation_fn Binary function: type operation_fn(type, type)
 */
#define PBT_ASSOCIATIVE(name, type, generator, operation_fn) \
    PBT_PROPERTY(name) { \
        type a = generator(state); \
        type b = generator(state); \
        type c = generator(state); \
        type left = operation_fn(operation_fn(a, b), c); \
        type right = operation_fn(a, operation_fn(b, c)); \
        PBT_ASSERT_EQ(left, right); \
    }

// ============================================================================
// Monotonic Property Pattern
// ============================================================================
//
// Tests that an operation preserves ordering.
//
// Pattern: a <= b implies f(a) <= f(b)
// ============================================================================

/**
 * @brief Define a monotonic (order-preserving) property test
 * 
 * @param name Property name
 * @param type Input type
 * @param generator Generator function
 * @param operation_fn Operation function
 * @param order_compare Ordering comparison (e.g., <=)
 */
#define PBT_MONOTONIC(name, type, generator, operation_fn, order_compare) \
    PBT_PROPERTY(name) { \
        type a = generator(state); \
        type b = generator(state); \
        if (a order_compare b) { \
            PBT_ASSERT(operation_fn(a) order_compare operation_fn(b)); \
        } else { \
            PBT_ASSERT(operation_fn(b) order_compare operation_fn(a)); \
        } \
    }

// ============================================================================
// Inverse Property Pattern
// ============================================================================
//
// Tests that two operations are inverses of each other.
//
// Pattern: g(f(x)) == x AND f(g(y)) == y
// ============================================================================

/**
 * @brief Define an inverse property test (both directions)
 * 
 * @param name Property name
 * @param type_a First type
 * @param type_b Second type
 * @param gen_a Generator for type_a
 * @param gen_b Generator for type_b
 * @param f_fn Function from type_a to type_b
 * @param g_fn Function from type_b to type_a
 */
#define PBT_INVERSE(name, type_a, type_b, gen_a, gen_b, f_fn, g_fn) \
    PBT_PROPERTY(name##_forward) { \
        type_a input = gen_a(state); \
        type_a result = g_fn(f_fn(input)); \
        PBT_ASSERT_EQ(input, result); \
    } \
    PBT_PROPERTY(name##_backward) { \
        type_b input = gen_b(state); \
        type_b result = f_fn(g_fn(input)); \
        PBT_ASSERT_EQ(input, result); \
    }

// ============================================================================
// Bounds Property Pattern
// ============================================================================
//
// Tests that output is within expected bounds.
//
// Pattern: lower <= f(x) <= upper
// ============================================================================

/**
 * @brief Define a bounds property test
 * 
 * @param name Property name
 * @param input_type Input type
 * @param output_type Output type
 * @param generator Generator function
 * @param operation Operation expression (use 'input' variable)
 * @param lower Lower bound expression
 * @param upper Upper bound expression
 */
#define PBT_BOUNDS_PROPERTY(name, input_type, output_type, generator, operation, lower, upper) \
    PBT_PROPERTY(name) { \
        input_type input = generator(state); \
        output_type result = (operation); \
        output_type lo = (lower); \
        output_type hi = (upper); \
        PBT_ASSERT(result >= lo); \
        PBT_ASSERT(result <= hi); \
    }

// ============================================================================
// Helper Macros for Property Annotations
// ============================================================================

/**
 * @brief Annotate a property with feature and requirement references
 * 
 * Use this at the start of a property definition to document what it validates.
 * 
 * Example:
 *   PBT_PROPERTY(my_property) {
 *       PBT_VALIDATES("test-refactor", 1, "PMM Allocation Alignment", "3.1");
 *       // ... property implementation
 *   }
 */
#define PBT_VALIDATES(feature, prop_num, prop_name, req) \
    /* **Feature: feature, Property prop_num: prop_name** */ \
    /* **Validates: Requirements req** */ \
    (void)0

#endif // _TESTS_PBT_PATTERNS_H_
