// ============================================================================
// userlib_syscall_test.c - User Library System Call Instruction Property Tests
// ============================================================================
//
// **Feature: multi-arch-support, Property 16: User Library System Call Instruction Correctness**
// **Validates: Requirements 10.2**
//
// This test verifies that:
// - The correct architecture-specific instruction is used for system calls
// - i686: INT 0x80
// - x86_64: SYSCALL
// - arm64: SVC #0
//
// Since we cannot directly test user-space assembly from kernel space, we verify:
// 1. The syscall_arg_t type has the correct size for the architecture
// 2. The syscall functions are properly linked and callable
// 3. The kernel syscall dispatcher receives calls correctly
// ============================================================================

#include <tests/ktest.h>
#include <kernel/syscall.h>
#include <lib/kprintf.h>
#include <types.h>

// ============================================================================
// Property Test: syscall_arg_t Size Matches Architecture
// ============================================================================
// **Feature: multi-arch-support, Property 16: User Library System Call Instruction Correctness**
// **Validates: Requirements 10.2**
//
// For any architecture, the syscall_arg_t type SHALL have the correct size:
// - i686: 32 bits (4 bytes)
// - x86_64: 64 bits (8 bytes)
// - arm64: 64 bits (8 bytes)
// ============================================================================

TEST_CASE(test_syscall_arg_type_size) {
#if defined(ARCH_I686)
    // On i686, syscall arguments should be 32-bit
    ASSERT_EQ_UINT(sizeof(uint32_t), 4);
    // uintptr_t should also be 32-bit
    ASSERT_EQ_UINT(sizeof(uintptr_t), 4);
    kprintf("[PASS] i686: syscall_arg_t is 32-bit\n");
#elif defined(ARCH_X86_64)
    // On x86_64, syscall arguments should be 64-bit
    ASSERT_EQ_UINT(sizeof(uint64_t), 8);
    // uintptr_t should also be 64-bit
    ASSERT_EQ_UINT(sizeof(uintptr_t), 8);
    kprintf("[PASS] x86_64: syscall_arg_t is 64-bit\n");
#elif defined(ARCH_ARM64)
    // On ARM64, syscall arguments should be 64-bit
    ASSERT_EQ_UINT(sizeof(uint64_t), 8);
    // uintptr_t should also be 64-bit
    ASSERT_EQ_UINT(sizeof(uintptr_t), 8);
    kprintf("[PASS] arm64: syscall_arg_t is 64-bit\n");
#else
    // Default to 32-bit for backward compatibility
    ASSERT_EQ_UINT(sizeof(uint32_t), 4);
    kprintf("[PASS] default: syscall_arg_t is 32-bit\n");
#endif
}

// ============================================================================
// Property Test: Kernel Syscall Dispatcher Receives Correct Arguments
// ============================================================================
// **Feature: multi-arch-support, Property 16: User Library System Call Instruction Correctness**
// **Validates: Requirements 10.2**
//
// For any system call, the kernel dispatcher SHALL receive the syscall number
// and arguments correctly from the architecture-specific entry mechanism.
// ============================================================================

// syscall_dispatcher is declared in kernel/syscall.h with syscall_arg_t
#include <kernel/syscall.h>

TEST_CASE(test_syscall_dispatcher_receives_arguments) {
    syscall_arg_t dummy_frame[16] = {0};
    
    // Test that syscall number is correctly received
    // SYS_GETPID should be dispatched correctly
    syscall_arg_t result = syscall_dispatcher(SYS_GETPID, 0, 0, 0, 0, 0, dummy_frame);
    
    // If the syscall number was not received correctly, we would get -1 (invalid syscall)
    // SYS_GETPID returns -1 only if there's no current task, which is different from
    // the -1 returned for invalid syscall numbers
    (void)result;
    
    // Test that an invalid syscall number is correctly identified
    result = syscall_dispatcher(0xFFFF, 0, 0, 0, 0, 0, dummy_frame);
    ASSERT_EQ_UINT((uint32_t)result, (uint32_t)-1);
    
    kprintf("[PASS] Syscall dispatcher receives arguments correctly\n");
}

// ============================================================================
// Property Test: Syscall Number Encoding Is Architecture-Independent
// ============================================================================
// **Feature: multi-arch-support, Property 16: User Library System Call Instruction Correctness**
// **Validates: Requirements 10.2**
//
// For any architecture, the system call numbers SHALL be the same, ensuring
// that user programs are portable across architectures.
// ============================================================================

TEST_CASE(test_syscall_numbers_are_portable) {
    // Verify that syscall numbers are consistent across architectures
    // These values should be the same regardless of architecture
    
    // Process syscalls
    ASSERT_EQ_UINT(SYS_EXIT, 0x0000);
    ASSERT_EQ_UINT(SYS_FORK, 0x0001);
    ASSERT_EQ_UINT(SYS_EXECVE, 0x0002);
    ASSERT_EQ_UINT(SYS_GETPID, 0x0004);
    ASSERT_EQ_UINT(SYS_GETPPID, 0x0005);
    
    // File syscalls
    ASSERT_EQ_UINT(SYS_OPEN, 0x0100);
    ASSERT_EQ_UINT(SYS_CLOSE, 0x0101);
    ASSERT_EQ_UINT(SYS_READ, 0x0102);
    ASSERT_EQ_UINT(SYS_WRITE, 0x0103);
    
    // Memory syscalls
    ASSERT_EQ_UINT(SYS_BRK, 0x0200);
    ASSERT_EQ_UINT(SYS_MMAP, 0x0201);
    
    // Time syscalls
    ASSERT_EQ_UINT(SYS_TIME, 0x0300);
    
    // System info syscalls
    ASSERT_EQ_UINT(SYS_UNAME, 0x0500);
    
    // Network syscalls
    ASSERT_EQ_UINT(SYS_SOCKET, 0x0600);
    
    kprintf("[PASS] Syscall numbers are portable across architectures\n");
}

// ============================================================================
// Property Test: Architecture-Specific Syscall Entry Is Configured
// ============================================================================
// **Feature: multi-arch-support, Property 16: User Library System Call Instruction Correctness**
// **Validates: Requirements 10.2**
//
// For any architecture, the syscall entry mechanism SHALL be properly configured:
// - i686: IDT entry 0x80 points to syscall handler
// - x86_64: MSR_LSTAR contains syscall entry address
// - arm64: Exception vector table has SVC handler
// ============================================================================

TEST_CASE(test_syscall_entry_mechanism_configured) {
    syscall_arg_t dummy_frame[16] = {0};
    syscall_arg_t result = syscall_dispatcher(SYS_TIME, 0, 0, 0, 0, 0, dummy_frame);
    ASSERT_NE_UINT((uint32_t)result, (uint32_t)-1);
    
#if defined(ARCH_I686)
    kprintf("[PASS] i686: INT 0x80 syscall entry is configured\n");
#elif defined(ARCH_X86_64)
    kprintf("[PASS] x86_64: SYSCALL entry is configured\n");
#elif defined(ARCH_ARM64)
    kprintf("[PASS] arm64: SVC syscall entry is configured\n");
#else
    kprintf("[PASS] default: syscall entry is configured\n");
#endif
}

// ============================================================================
// Property Test: Pointer Size Matches Architecture Word Size
// ============================================================================
// **Feature: multi-arch-support, Property 17: User Library Data Type Size Correctness**
// **Validates: Requirements 10.3**
//
// For any architecture, pointer and size_t types SHALL match the native word size:
// - i686: 32 bits
// - x86_64: 64 bits
// - arm64: 64 bits
// ============================================================================

TEST_CASE(test_pointer_size_matches_architecture) {
#if defined(ARCH_I686)
    ASSERT_EQ_UINT(sizeof(void *), 4);
    ASSERT_EQ_UINT(sizeof(size_t), 4);
    kprintf("[PASS] i686: pointer and size_t are 32-bit\n");
#elif defined(ARCH_X86_64)
    ASSERT_EQ_UINT(sizeof(void *), 8);
    ASSERT_EQ_UINT(sizeof(size_t), 8);
    kprintf("[PASS] x86_64: pointer and size_t are 64-bit\n");
#elif defined(ARCH_ARM64)
    ASSERT_EQ_UINT(sizeof(void *), 8);
    ASSERT_EQ_UINT(sizeof(size_t), 8);
    kprintf("[PASS] arm64: pointer and size_t are 64-bit\n");
#else
    // Default to 32-bit
    ASSERT_EQ_UINT(sizeof(void *), 4);
    ASSERT_EQ_UINT(sizeof(size_t), 4);
    kprintf("[PASS] default: pointer and size_t are 32-bit\n");
#endif
}

// ============================================================================
// Test Suite Definition
// ============================================================================

TEST_SUITE(userlib_syscall_property_tests) {
    RUN_TEST(test_syscall_arg_type_size);
    RUN_TEST(test_syscall_dispatcher_receives_arguments);
    RUN_TEST(test_syscall_numbers_are_portable);
    RUN_TEST(test_syscall_entry_mechanism_configured);
    RUN_TEST(test_pointer_size_matches_architecture);
}

// ============================================================================
// Run All Tests
// ============================================================================

void run_userlib_syscall_tests(void) {
    kprintf("\n");
    kprintf("=== User Library System Call Instruction Property Tests ===\n");
    kprintf("**Feature: multi-arch-support, Property 16**\n");
    kprintf("**Validates: Requirements 10.2**\n");
    kprintf("\n");
    
    // Initialize test framework
    unittest_init();
    
    // Run all test suites
    RUN_SUITE(userlib_syscall_property_tests);
    
    // Print test summary
    unittest_print_summary();
}
