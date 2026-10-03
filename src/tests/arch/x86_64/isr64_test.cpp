/**
 * @file isr64_test.c
 * @brief Property tests for x86_64 interrupt register preservation
 * 
 * **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (x86_64)**
 * **Validates: Requirements 6.1**
 * 
 * This test verifies that the registers64_t structure layout matches
 * the assembly stub's register save/restore order, ensuring that
 * interrupt handlers receive correct register values and that
 * registers are properly restored after interrupt handling.
 */

#include <tests/ktest.h>
#include <lib/string.h>

#ifdef ARCH_X86_64

#include <isr64.h>

/* ============================================================================
 * Property Test: Register Structure Layout
 * ============================================================================
 * 
 * Property 7: Interrupt Register State Preservation (x86_64)
 * 
 * *For any* interrupt or exception, the interrupt handler SHALL save all
 * architecture-specific registers before handling and restore them exactly
 * upon return, such that the interrupted code continues execution correctly.
 * 
 * This property is verified by checking:
 * 1. The registers64_t structure has correct size
 * 2. The structure fields are at expected offsets
 * 3. The structure layout matches assembly push order
 */

/* Expected structure size: 
 * 15 GPRs (r15-rax) * 8 = 120 bytes
 * 2 interrupt info (int_no, err_code) * 8 = 16 bytes
 * 5 CPU frame (rip, cs, rflags, rsp, ss) * 8 = 40 bytes
 * Total = 176 bytes
 */
#define EXPECTED_REGISTERS_SIZE 176

/* Field offset verification macros */
#define VERIFY_OFFSET(field, expected_offset) \
    ASSERT_EQ_UINT(expected_offset, (uint32_t)__builtin_offsetof(registers64_t, field))

/**
 * Test: Verify registers64_t structure size
 * 
 * The structure must be exactly 176 bytes to match the assembly stub's
 * stack frame layout.
 */
TEST_CASE(isr64_register_struct_size) {
    /* **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (x86_64)** */
    /* **Validates: Requirements 6.1** */
    
    ASSERT_EQ_UINT(EXPECTED_REGISTERS_SIZE, (uint32_t)sizeof(registers64_t));
}

/**
 * Test: Verify registers64_t field offsets
 * 
 * Each field must be at the correct offset to match the assembly stub's
 * push order. The assembly pushes registers in this order:
 *   1. CPU pushes: SS, RSP, RFLAGS, CS, RIP (if privilege change)
 *   2. Stub pushes: error code (or dummy), interrupt number
 *   3. Stub pushes: RAX, RBX, RCX, RDX, RBP, RSI, RDI, R8-R15
 * 
 * Since the stack grows downward, the first pushed value is at the
 * highest address. The structure is defined to match this layout.
 */
TEST_CASE(isr64_register_struct_offsets) {
    /* **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (x86_64)** */
    /* **Validates: Requirements 6.1** */
    
    /* General purpose registers (pushed by stub, in reverse order) */
    /* Assembly pushes: rax, rbx, rcx, rdx, rbp, rsi, rdi, r8-r15 */
    /* So in memory (low to high): r15, r14, ..., r8, rdi, rsi, rbp, rdx, rcx, rbx, rax */
    VERIFY_OFFSET(r15, 0);
    VERIFY_OFFSET(r14, 8);
    VERIFY_OFFSET(r13, 16);
    VERIFY_OFFSET(r12, 24);
    VERIFY_OFFSET(r11, 32);
    VERIFY_OFFSET(r10, 40);
    VERIFY_OFFSET(r9, 48);
    VERIFY_OFFSET(r8, 56);
    VERIFY_OFFSET(rdi, 64);
    VERIFY_OFFSET(rsi, 72);
    VERIFY_OFFSET(rbp, 80);
    VERIFY_OFFSET(rdx, 88);
    VERIFY_OFFSET(rcx, 96);
    VERIFY_OFFSET(rbx, 104);
    VERIFY_OFFSET(rax, 112);
    
    /* Interrupt info (pushed by stub) */
    VERIFY_OFFSET(int_no, 120);
    VERIFY_OFFSET(err_code, 128);
    
    /* CPU-pushed interrupt frame */
    VERIFY_OFFSET(rip, 136);
    VERIFY_OFFSET(cs, 144);
    VERIFY_OFFSET(rflags, 152);
    VERIFY_OFFSET(rsp, 160);
    VERIFY_OFFSET(ss, 168);
}

/**
 * Test: Verify all 64-bit registers are 8 bytes
 * 
 * In x86_64, all general-purpose registers are 64-bit (8 bytes).
 * This test ensures the structure uses correct types.
 */
TEST_CASE(isr64_register_field_sizes) {
    /* **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (x86_64)** */
    /* **Validates: Requirements 6.1** */
    
    registers64_t regs;
    
    /* All fields should be 8 bytes (uint64_t) */
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.r15));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.r14));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.r13));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.r12));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.r11));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.r10));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.r9));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.r8));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.rdi));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.rsi));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.rbp));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.rdx));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.rcx));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.rbx));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.rax));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.int_no));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.err_code));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.rip));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.cs));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.rflags));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.rsp));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.ss));
}

/**
 * Test: Verify register count matches x86_64 architecture
 * 
 * x86_64 has 16 general-purpose registers (RAX-R15).
 * We save 15 of them (RSP is handled separately by CPU).
 */
TEST_CASE(isr64_register_count) {
    /* **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (x86_64)** */
    /* **Validates: Requirements 6.1** */
    
    /* 15 GPRs saved by stub (RSP is in CPU frame) */
    /* Plus 2 interrupt info fields */
    /* Plus 5 CPU frame fields */
    /* Total: 22 fields */
    
    /* Calculate number of 8-byte fields */
    uint32_t num_fields = sizeof(registers64_t) / 8;
    ASSERT_EQ_UINT(22, num_fields);
}

/**
 * Test: Verify page fault info parsing
 * 
 * Tests that page fault error codes are correctly parsed.
 */
TEST_CASE(isr64_page_fault_parsing) {
    /* **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (x86_64)** */
    /* **Validates: Requirements 6.1** */
    
    /* Test various error code combinations */
    page_fault_info_t info;
    
    /* Error code 0: Page not present, read, kernel mode */
    info = parse_page_fault_error(0x0);
    ASSERT_FALSE(info.present);
    ASSERT_FALSE(info.write);
    ASSERT_FALSE(info.user);
    ASSERT_FALSE(info.reserved);
    ASSERT_FALSE(info.instruction);
    
    /* Error code 1: Page present (protection violation), read, kernel mode */
    info = parse_page_fault_error(0x1);
    ASSERT_TRUE(info.present);
    ASSERT_FALSE(info.write);
    ASSERT_FALSE(info.user);
    
    /* Error code 2: Page not present, write, kernel mode */
    info = parse_page_fault_error(0x2);
    ASSERT_FALSE(info.present);
    ASSERT_TRUE(info.write);
    ASSERT_FALSE(info.user);
    
    /* Error code 7: Page present, write, user mode */
    info = parse_page_fault_error(0x7);
    ASSERT_TRUE(info.present);
    ASSERT_TRUE(info.write);
    ASSERT_TRUE(info.user);
    
    /* Error code with reserved bit */
    info = parse_page_fault_error(0x8);
    ASSERT_TRUE(info.reserved);
    
    /* Error code with instruction fetch */
    info = parse_page_fault_error(0x10);
    ASSERT_TRUE(info.instruction);
}

/**
 * Test: Verify GPF info parsing
 * 
 * Tests that general protection fault error codes are correctly parsed.
 */
TEST_CASE(isr64_gpf_parsing) {
    /* **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (x86_64)** */
    /* **Validates: Requirements 6.1** */
    
    gpf_info_t info;
    
    /* Error code 0: Internal, GDT, index 0 */
    info = parse_gpf_error(0x0);
    ASSERT_FALSE(info.external);
    ASSERT_EQ_UINT(0, info.table);
    ASSERT_EQ_UINT(0, info.index);
    
    /* Error code 1: External, GDT, index 0 */
    info = parse_gpf_error(0x1);
    ASSERT_TRUE(info.external);
    ASSERT_EQ_UINT(0, info.table);
    
    /* Error code 2: Internal, IDT, index 0 */
    info = parse_gpf_error(0x2);
    ASSERT_FALSE(info.external);
    ASSERT_EQ_UINT(1, info.table);
    
    /* Error code with selector index */
    /* Selector 0x10 (index 2, GDT) -> error code = (2 << 3) | 0 = 0x10 */
    info = parse_gpf_error(0x10);
    ASSERT_EQ_UINT(2, info.index);
    ASSERT_EQ_UINT(0, info.table);
}

/* Test suite runner */
void run_isr64_tests(void) {
    unittest_begin_suite("x86_64 ISR Register Preservation Tests");
    unittest_run_test("register struct size", test_isr64_register_struct_size);
    unittest_run_test("register struct offsets", test_isr64_register_struct_offsets);
    unittest_run_test("register field sizes", test_isr64_register_field_sizes);
    unittest_run_test("register count", test_isr64_register_count);
    unittest_run_test("page fault parsing", test_isr64_page_fault_parsing);
    unittest_run_test("GPF parsing", test_isr64_gpf_parsing);
    unittest_end_suite();
}

#else /* !ARCH_X86_64 */

/* Stub for non-x86_64 architectures */
void run_isr64_tests(void) {
    /* Tests only run on x86_64 */
}

#endif /* ARCH_X86_64 */
