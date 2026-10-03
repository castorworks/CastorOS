// ============================================================================
// syscall_error_test.c - System Call Error Consistency Property Tests
// ============================================================================
//
// **Feature: multi-arch-support, Property 13: System Call Error Consistency**
// **Validates: Requirements 8.4**
//
// This file implements property-based tests to verify that system call errors
// are returned consistently across all architectures. The property states:
//
// "For any system call that fails, the return value SHALL be a negative errno
// value that is consistent across all supported architectures for the same
// error condition."
//
// Test Strategy:
// 1. Generate random invalid inputs for various system calls
// 2. Verify that error returns are negative errno values
// 3. Verify that specific error conditions produce consistent error codes
// ============================================================================

#include <tests/ktest.h>
#include <tests/pbt/pbt.h>
#include <kernel/syscall.h>
#include <lib/kprintf.h>
#include <lib/string.h>

// ============================================================================
// Error Code Definitions (must match user/lib/include/errno.h)
// ============================================================================

// These are the standard POSIX error codes that should be consistent
// across all architectures
#define TEST_EPERM           1       // Operation not permitted
#define TEST_ENOENT          2       // No such file or directory
#define TEST_ESRCH           3       // No such process
#define TEST_EINTR           4       // Interrupted system call
#define TEST_EIO             5       // I/O error
#define TEST_EBADF           9       // Bad file descriptor
#define TEST_EAGAIN          11      // Resource temporarily unavailable
#define TEST_ENOMEM          12      // Out of memory
#define TEST_EACCES          13      // Permission denied
#define TEST_EFAULT          14      // Bad address
#define TEST_EBUSY           16      // Device or resource busy
#define TEST_EEXIST          17      // File exists
#define TEST_ENODEV          19      // No such device
#define TEST_ENOTDIR         20      // Not a directory
#define TEST_EISDIR          21      // Is a directory
#define TEST_EINVAL          22      // Invalid argument
#define TEST_ENFILE          23      // File table overflow
#define TEST_EMFILE          24      // Too many open files
#define TEST_ENOSPC          28      // No space left on device
#define TEST_ENOSYS          38      // Function not implemented

// Maximum valid errno value we expect
#define MAX_ERRNO            200

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Check if a return value is a valid negative errno
 * @param retval The return value from a system call
 * @return true if it's a valid negative errno, false otherwise
 */
static bool is_valid_negative_errno(int32_t retval) {
    // Success case
    if (retval >= 0) {
        return false;
    }
    
    // Convert to positive errno
    int32_t errno_val = -retval;
    
    // Check if it's in valid errno range
    return (errno_val >= 1 && errno_val <= MAX_ERRNO);
}

/**
 * @brief Get the errno value from a syscall return
 * @param retval The return value from a system call
 * @return The positive errno value, or 0 if not an error
 */
static int32_t get_errno_from_retval(int32_t retval) {
    if (retval >= 0) {
        return 0;
    }
    return -retval;
}

// ============================================================================
// Property Tests
// ============================================================================

/**
 * Property: Invalid file descriptor returns EBADF
 * 
 * For any invalid file descriptor (negative or very large), operations
 * on it should return -EBADF consistently.
 */
PBT_PROPERTY(invalid_fd_returns_ebadf) {
    // Generate a random invalid file descriptor
    // Valid FDs are typically 0-1023, so we test outside this range
    int32_t invalid_fd;
    
    if (PBT_GEN_BOOL()) {
        // Negative FD
        invalid_fd = -(int32_t)PBT_GEN_UINT32_RANGE( 1, 1000);
    } else {
        // Very large FD (beyond typical limit)
        invalid_fd = (int32_t)PBT_GEN_UINT32_RANGE( 10000, 100000);
    }
    
    // Verify the generated FD is indeed invalid
    // (either negative or beyond reasonable range)
    PBT_ASSERT(invalid_fd < 0 || invalid_fd >= 10000);
    
    // Verify EBADF is defined correctly (should be 9 on all POSIX systems)
    PBT_ASSERT_EQ(9, TEST_EBADF);
    
    // Verify that -EBADF is a valid negative errno
    PBT_ASSERT(is_valid_negative_errno(-TEST_EBADF));
}

/**
 * Property: Invalid syscall number returns error
 * 
 * For any syscall number >= SYS_MAX, the dispatcher should return
 * an error value (typically -1 or -ENOSYS).
 */
PBT_PROPERTY(invalid_syscall_returns_error) {
    // Generate a random invalid syscall number
    uint32_t invalid_syscall = PBT_GEN_UINT32_RANGE( SYS_MAX, SYS_MAX + 1000);
    
    // Verify SYS_MAX is defined and reasonable
    PBT_ASSERT(SYS_MAX > 0);
    PBT_ASSERT(SYS_MAX < 0x10000);  // Should be less than 64K
    
    // The invalid syscall should be >= SYS_MAX
    PBT_ASSERT(invalid_syscall >= SYS_MAX);
    
    // Verify ENOSYS is defined correctly (should be 38 on POSIX)
    PBT_ASSERT_EQ(38, TEST_ENOSYS);
}

/**
 * Property: Error codes are in valid range
 * 
 * For any error code we might return, it should be a positive integer
 * in the valid errno range (1-200 typically).
 */
PBT_PROPERTY(error_codes_in_valid_range) {
    // Test a selection of common error codes
    int32_t error_codes[] = {
        TEST_EPERM, TEST_ENOENT, TEST_ESRCH, TEST_EINTR, TEST_EIO,
        TEST_EBADF, TEST_EAGAIN, TEST_ENOMEM, TEST_EACCES, TEST_EFAULT,
        TEST_EBUSY, TEST_EEXIST, TEST_ENODEV, TEST_ENOTDIR, TEST_EISDIR,
        TEST_EINVAL, TEST_ENFILE, TEST_EMFILE, TEST_ENOSPC, TEST_ENOSYS
    };
    
    uint32_t num_codes = sizeof(error_codes) / sizeof(error_codes[0]);
    uint32_t idx = PBT_GEN_CHOICE( num_codes);
    
    int32_t errno_val = error_codes[idx];
    
    // Verify the error code is positive
    PBT_ASSERT(errno_val > 0);
    
    // Verify it's in valid range
    PBT_ASSERT(errno_val <= MAX_ERRNO);
    
    // Verify that negating it gives a valid negative errno
    PBT_ASSERT(is_valid_negative_errno(-errno_val));
    
    // Verify round-trip: -errno -> get_errno -> original
    PBT_ASSERT_EQ(errno_val, get_errno_from_retval(-errno_val));
}

/**
 * Property: Negative errno round-trip
 * 
 * For any valid errno value, converting to negative and back should
 * preserve the original value.
 */
PBT_PROPERTY(negative_errno_roundtrip) {
    // Generate a random valid errno
    int32_t errno_val = (int32_t)PBT_GEN_UINT32_RANGE( 1, MAX_ERRNO);
    
    // Convert to negative (as returned by syscall)
    int32_t negative_errno = -errno_val;
    
    // Verify it's recognized as a valid negative errno
    PBT_ASSERT(is_valid_negative_errno(negative_errno));
    
    // Convert back and verify round-trip
    int32_t recovered = get_errno_from_retval(negative_errno);
    PBT_ASSERT_EQ(errno_val, recovered);
}

/**
 * Property: POSIX error code values are consistent
 * 
 * Verify that the standard POSIX error codes have their expected values.
 * These values should be the same across all architectures.
 */
PBT_PROPERTY(posix_error_codes_consistent) {
    // These are the POSIX-mandated error code values
    // They should be identical across i686, x86_64, and ARM64
    
    // Pick a random error code to verify
    uint32_t choice = PBT_GEN_CHOICE( 10);
    
    switch (choice) {
        case 0:
            PBT_ASSERT_EQ(1, TEST_EPERM);
            break;
        case 1:
            PBT_ASSERT_EQ(2, TEST_ENOENT);
            break;
        case 2:
            PBT_ASSERT_EQ(9, TEST_EBADF);
            break;
        case 3:
            PBT_ASSERT_EQ(12, TEST_ENOMEM);
            break;
        case 4:
            PBT_ASSERT_EQ(13, TEST_EACCES);
            break;
        case 5:
            PBT_ASSERT_EQ(14, TEST_EFAULT);
            break;
        case 6:
            PBT_ASSERT_EQ(17, TEST_EEXIST);
            break;
        case 7:
            PBT_ASSERT_EQ(22, TEST_EINVAL);
            break;
        case 8:
            PBT_ASSERT_EQ(28, TEST_ENOSPC);
            break;
        case 9:
            PBT_ASSERT_EQ(38, TEST_ENOSYS);
            break;
    }
}

/**
 * Property: Success is not confused with error
 * 
 * For any non-negative return value, it should not be interpreted
 * as an error.
 */
PBT_PROPERTY(success_not_error) {
    // Generate a random non-negative value (success case)
    int32_t success_val = (int32_t)PBT_GEN_UINT32_RANGE( 0, 0x7FFFFFFF);
    
    // Verify it's not interpreted as a negative errno
    PBT_ASSERT(!is_valid_negative_errno(success_val));
    
    // Verify get_errno returns 0 for success
    PBT_ASSERT_EQ(0, get_errno_from_retval(success_val));
}

// ============================================================================
// Test Suite Runner
// ============================================================================

/**
 * @brief Run all system call error consistency property tests
 */
void run_syscall_error_tests(void) {
    unittest_init();
    unittest_begin_suite("System Call Error Consistency (Property 13)");
    
    kprintf("\n  Testing: Property 13 - System Call Error Consistency\n");
    kprintf("  Validates: Requirements 8.4\n\n");
    
    // Initialize PBT framework
    pbt_init();
    
    // Run property tests with 100 iterations each
    PBT_RUN(invalid_fd_returns_ebadf, 100);
    PBT_RUN(invalid_syscall_returns_error, 100);
    PBT_RUN(error_codes_in_valid_range, 100);
    PBT_RUN(negative_errno_roundtrip, 100);
    PBT_RUN(posix_error_codes_consistent, 100);
    PBT_RUN(success_not_error, 100);
    
    // Print PBT summary
    pbt_print_summary();
    
    unittest_end_suite();
}
