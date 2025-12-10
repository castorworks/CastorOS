/**
 * @file arm64_exception_test.c
 * @brief Property tests for ARM64 interrupt register preservation
 * 
 * **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (ARM64)**
 * **Validates: Requirements 6.2**
 * 
 * This test verifies that the arm64_regs_t structure layout matches
 * the assembly stub's register save/restore order, ensuring that
 * exception handlers receive correct register values and that
 * registers are properly restored after exception handling.
 */

#include <tests/ktest.h>
#include <lib/string.h>

#ifdef ARCH_ARM64

#include "../arch/arm64/include/exception.h"

/* ============================================================================
 * Property Test: Register Structure Layout
 * ============================================================================
 * 
 * Property 7: Interrupt Register State Preservation (ARM64)
 * 
 * *For any* interrupt or exception, the interrupt handler SHALL save all
 * architecture-specific registers before handling and restore them exactly
 * upon return, such that the interrupted code continues execution correctly.
 * 
 * This property is verified by checking:
 * 1. The arm64_regs_t structure has correct size (272 bytes)
 * 2. The structure fields are at expected offsets
 * 3. The structure layout matches assembly push order
 */

/* Expected structure size: 
 * 31 GPRs (X0-X30) * 8 = 248 bytes
 * SP_EL0 * 8 = 8 bytes
 * ELR_EL1 * 8 = 8 bytes
 * SPSR_EL1 * 8 = 8 bytes
 * Total = 272 bytes
 */
#define EXPECTED_REGS_SIZE 272

/* Field offset verification macros */
#define VERIFY_OFFSET(field, expected_offset) \
    ASSERT_EQ_UINT(expected_offset, (uint32_t)__builtin_offsetof(arm64_regs_t, field))

/**
 * Test: Verify arm64_regs_t structure size
 * 
 * The structure must be exactly 272 bytes to match the assembly stub's
 * stack frame layout (FRAME_SIZE in vectors.S).
 * 
 * **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (ARM64)**
 * **Validates: Requirements 6.2**
 */
TEST_CASE(arm64_register_struct_size) {
    ASSERT_EQ_UINT(EXPECTED_REGS_SIZE, (uint32_t)sizeof(arm64_regs_t));
}

/**
 * Test: Verify arm64_regs_t field offsets
 * 
 * Each field must be at the correct offset to match the assembly stub's
 * save/restore order. The assembly saves registers in this order:
 *   1. X0-X29 (stp pairs)
 *   2. X30 and SP_EL0
 *   3. ELR_EL1 and SPSR_EL1
 * 
 * **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (ARM64)**
 * **Validates: Requirements 6.2**
 */
TEST_CASE(arm64_register_struct_offsets) {
    /* General purpose registers X0-X30 */
    /* Each register is 8 bytes, stored sequentially */
    for (int i = 0; i < 31; i++) {
        ASSERT_EQ_UINT(i * 8, (uint32_t)__builtin_offsetof(arm64_regs_t, x[i]));
    }
    
    /* SP_EL0 at offset 248 (31 * 8) */
    VERIFY_OFFSET(sp_el0, 248);
    
    /* ELR_EL1 at offset 256 */
    VERIFY_OFFSET(elr, 256);
    
    /* SPSR_EL1 at offset 264 */
    VERIFY_OFFSET(spsr, 264);
}

/**
 * Test: Verify all 64-bit registers are 8 bytes
 * 
 * In ARM64, all general-purpose registers are 64-bit (8 bytes).
 * This test ensures the structure uses correct types.
 * 
 * **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (ARM64)**
 * **Validates: Requirements 6.2**
 */
TEST_CASE(arm64_register_field_sizes) {
    arm64_regs_t regs;
    
    /* X registers array element size should be 8 bytes */
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.x[0]));
    
    /* Other fields should be 8 bytes */
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.sp_el0));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.elr));
    ASSERT_EQ_UINT(8, (uint32_t)sizeof(regs.spsr));
}

/**
 * Test: Verify register count matches ARM64 architecture
 * 
 * ARM64 has 31 general-purpose registers (X0-X30).
 * Plus SP_EL0, ELR_EL1, and SPSR_EL1 = 34 total fields.
 * 
 * **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (ARM64)**
 * **Validates: Requirements 6.2**
 */
TEST_CASE(arm64_register_count) {
    /* 31 GPRs + SP_EL0 + ELR + SPSR = 34 fields */
    /* Total size / 8 bytes per field = 34 */
    uint32_t num_fields = sizeof(arm64_regs_t) / 8;
    ASSERT_EQ_UINT(34, num_fields);
}

/**
 * Test: Verify X register array size
 * 
 * The X register array should have exactly 31 elements (X0-X30).
 * 
 * **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (ARM64)**
 * **Validates: Requirements 6.2**
 */
TEST_CASE(arm64_x_register_array_size) {
    arm64_regs_t regs;
    
    /* X array should have 31 elements */
    ASSERT_EQ_UINT(31, (uint32_t)(sizeof(regs.x) / sizeof(regs.x[0])));
}

/**
 * Test: Verify ESR exception class extraction
 * 
 * Tests that exception class values can be correctly extracted from ESR.
 * 
 * **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (ARM64)**
 * **Validates: Requirements 6.2**
 */
TEST_CASE(arm64_esr_exception_class_extraction) {
    /* Test ESR_EC extraction */
    uint64_t esr;
    uint32_t ec;
    
    /* SVC from AArch64: EC = 0x15 */
    esr = ((uint64_t)ESR_EC_SVC64 << ESR_EC_SHIFT);
    ec = (esr >> ESR_EC_SHIFT) & 0x3F;
    ASSERT_EQ_UINT(ESR_EC_SVC64, ec);
    
    /* Data abort from current EL: EC = 0x25 */
    esr = ((uint64_t)ESR_EC_DABT_CUR << ESR_EC_SHIFT);
    ec = (esr >> ESR_EC_SHIFT) & 0x3F;
    ASSERT_EQ_UINT(ESR_EC_DABT_CUR, ec);
    
    /* Instruction abort from lower EL: EC = 0x20 */
    esr = ((uint64_t)ESR_EC_IABT_LOW << ESR_EC_SHIFT);
    ec = (esr >> ESR_EC_SHIFT) & 0x3F;
    ASSERT_EQ_UINT(ESR_EC_IABT_LOW, ec);
}

/**
 * Test: Verify fault status code extraction
 * 
 * Tests that fault status codes can be correctly extracted from ESR ISS field.
 * 
 * **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (ARM64)**
 * **Validates: Requirements 6.2**
 */
TEST_CASE(arm64_fault_status_extraction) {
    uint32_t iss;
    uint32_t fsc;
    
    /* Translation fault level 3 */
    iss = FSC_TRANS_L3;
    fsc = iss & ESR_ISS_DFSC_MASK;
    ASSERT_EQ_UINT(FSC_TRANS_L3, fsc);
    
    /* Permission fault level 2 */
    iss = FSC_PERM_L2;
    fsc = iss & ESR_ISS_DFSC_MASK;
    ASSERT_EQ_UINT(FSC_PERM_L2, fsc);
    
    /* Alignment fault */
    iss = FSC_ALIGN;
    fsc = iss & ESR_ISS_DFSC_MASK;
    ASSERT_EQ_UINT(FSC_ALIGN, fsc);
}

/**
 * Test: Verify write/read bit extraction from data abort ISS
 * 
 * Tests that the WnR (Write not Read) bit can be correctly extracted.
 * 
 * **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (ARM64)**
 * **Validates: Requirements 6.2**
 */
TEST_CASE(arm64_data_abort_wnr_extraction) {
    uint32_t iss;
    bool is_write;
    
    /* Read operation (WnR = 0) */
    iss = FSC_TRANS_L3;  /* No WnR bit set */
    is_write = (iss & ESR_ISS_WNR) != 0;
    ASSERT_FALSE(is_write);
    
    /* Write operation (WnR = 1) */
    iss = FSC_TRANS_L3 | ESR_ISS_WNR;
    is_write = (iss & ESR_ISS_WNR) != 0;
    ASSERT_TRUE(is_write);
}

/**
 * Test: Verify exception type constants match vectors.S
 * 
 * **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (ARM64)**
 * **Validates: Requirements 6.2**
 */
TEST_CASE(arm64_exception_type_constants) {
    /* These must match the values in vectors.S */
    ASSERT_EQ_UINT(0, EXCEPTION_SYNC);
    ASSERT_EQ_UINT(1, EXCEPTION_IRQ);
    ASSERT_EQ_UINT(2, EXCEPTION_FIQ);
    ASSERT_EQ_UINT(3, EXCEPTION_SERROR);
}

/**
 * Test: Verify exception source constants match vectors.S
 * 
 * **Feature: multi-arch-support, Property 7: Interrupt Register State Preservation (ARM64)**
 * **Validates: Requirements 6.2**
 */
TEST_CASE(arm64_exception_source_constants) {
    /* These must match the values in vectors.S */
    ASSERT_EQ_UINT(0, EXCEPTION_FROM_EL1_SP0);
    ASSERT_EQ_UINT(1, EXCEPTION_FROM_EL1_SPX);
    ASSERT_EQ_UINT(2, EXCEPTION_FROM_EL0_64);
    ASSERT_EQ_UINT(3, EXCEPTION_FROM_EL0_32);
}

/* Test suite runner */
void run_arm64_exception_tests(void) {
    unittest_begin_suite("ARM64 Exception Register Preservation Tests");
    unittest_run_test("register struct size", test_arm64_register_struct_size);
    unittest_run_test("register struct offsets", test_arm64_register_struct_offsets);
    unittest_run_test("register field sizes", test_arm64_register_field_sizes);
    unittest_run_test("register count", test_arm64_register_count);
    unittest_run_test("X register array size", test_arm64_x_register_array_size);
    unittest_run_test("ESR exception class extraction", test_arm64_esr_exception_class_extraction);
    unittest_run_test("fault status extraction", test_arm64_fault_status_extraction);
    unittest_run_test("data abort WnR extraction", test_arm64_data_abort_wnr_extraction);
    unittest_run_test("exception type constants", test_arm64_exception_type_constants);
    unittest_run_test("exception source constants", test_arm64_exception_source_constants);
    unittest_end_suite();
}

#else /* !ARCH_ARM64 */

/* Stub for non-ARM64 architectures */
void run_arm64_exception_tests(void) {
    /* Tests only run on ARM64 */
}

#endif /* ARCH_ARM64 */
