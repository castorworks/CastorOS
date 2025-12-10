/**
 * @file userlib_syscall_test.h
 * @brief User Library System Call Instruction Property Tests
 * 
 * **Feature: multi-arch-support, Property 16: User Library System Call Instruction Correctness**
 * **Validates: Requirements 10.2**
 */

#ifndef _USERLIB_SYSCALL_TEST_H_
#define _USERLIB_SYSCALL_TEST_H_

/**
 * @brief Run all user library syscall property tests
 * 
 * Tests verify:
 * - syscall_arg_t type has correct size for architecture
 * - Syscall dispatcher receives arguments correctly
 * - Syscall numbers are portable across architectures
 * - Architecture-specific syscall entry is configured
 * - Pointer size matches architecture word size
 */
void run_userlib_syscall_tests(void);

#endif // _USERLIB_SYSCALL_TEST_H_
