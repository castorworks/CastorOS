/**
 * @file idt64.c
 * @brief Interrupt Descriptor Table Implementation (x86_64)
 * 
 * This file implements the 64-bit IDT for x86_64 architecture.
 * 
 * Key differences from 32-bit IDT:
 *   - Each entry is 16 bytes (vs 8 bytes in 32-bit mode)
 *   - Handler addresses are 64-bit
 *   - IST (Interrupt Stack Table) support for dedicated interrupt stacks
 *   - No task gates (hardware task switching not supported in long mode)
 * 
 * The IDT is used for:
 *   - CPU exceptions (vectors 0-31)
 *   - Hardware interrupts (vectors 32-255, typically 32-47 for legacy IRQs)
 *   - Software interrupts (e.g., system calls)
 * 
 * Requirements: 3.4 - 64-bit IDT format with IST support
 */

#include "idt64.h"
#include "gdt64.h"
#include <types.h>
#include <lib/string.h>
#include <lib/klog.h>

/* ============================================================================
 * IDT Table
 * ============================================================================
 * The IDT contains 256 entries, each 16 bytes in 64-bit mode.
 * Total size: 256 * 16 = 4096 bytes (exactly one page)
 */

/* IDT entries (256 vectors * 16 bytes = 4KB) */
static idt64_entry_t idt64_entries[256] __attribute__((aligned(16)));

/* IDT pointer for LIDT instruction */
static idt64_ptr_t idt64_pointer;

/* ============================================================================
 * Public API Implementation
 * ========================================================================== */

/**
 * @brief Set an IDT gate entry
 * @param vector Interrupt vector number (0-255)
 * @param handler Handler function address
 * @param selector Code segment selector
 * @param ist IST index (0 for none, 1-7 for IST entries)
 * @param type_attr Type and attribute flags
 */
void idt64_set_gate(uint8_t vector, uint64_t handler, uint16_t selector,
                    uint8_t ist, uint8_t type_attr) {
    idt64_entries[vector].offset_low = (uint16_t)(handler & 0xFFFF);
    idt64_entries[vector].selector = selector;
    idt64_entries[vector].ist = ist & 0x07;  /* Only bits 0-2 are used for IST */
    idt64_entries[vector].type_attr = type_attr;
    idt64_entries[vector].offset_mid = (uint16_t)((handler >> 16) & 0xFFFF);
    idt64_entries[vector].offset_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    idt64_entries[vector].reserved = 0;
}

/**
 * @brief Set an interrupt gate (interrupts disabled during handler)
 * @param vector Interrupt vector number
 * @param handler Handler function address
 * 
 * Interrupt gates automatically clear IF (interrupt flag) when entered,
 * preventing nested interrupts unless explicitly re-enabled.
 */
void idt64_set_interrupt_gate(uint8_t vector, uint64_t handler) {
    idt64_set_gate(vector, handler, GDT64_KERNEL_CODE_SEGMENT,
                   IDT64_IST_NONE, IDT64_GATE_INTERRUPT);
}

/**
 * @brief Set an interrupt gate with IST
 * @param vector Interrupt vector number
 * @param handler Handler function address
 * @param ist IST index (1-7)
 * 
 * Using IST ensures the handler runs on a known-good stack,
 * which is critical for handling exceptions that might occur
 * when the current stack is corrupted (e.g., double fault).
 */
void idt64_set_interrupt_gate_ist(uint8_t vector, uint64_t handler, uint8_t ist) {
    idt64_set_gate(vector, handler, GDT64_KERNEL_CODE_SEGMENT,
                   ist, IDT64_GATE_INTERRUPT);
}

/**
 * @brief Set a trap gate (interrupts remain enabled during handler)
 * @param vector Interrupt vector number
 * @param handler Handler function address
 * 
 * Trap gates do NOT clear IF, so interrupts can occur during
 * the handler execution. Used for exceptions like breakpoints
 * where nested interrupts are acceptable.
 */
void idt64_set_trap_gate(uint8_t vector, uint64_t handler) {
    idt64_set_gate(vector, handler, GDT64_KERNEL_CODE_SEGMENT,
                   IDT64_IST_NONE, IDT64_GATE_TRAP);
}

/**
 * @brief Set a user-callable interrupt gate (DPL=3)
 * @param vector Interrupt vector number
 * @param handler Handler function address
 * 
 * User interrupt gates can be triggered from Ring 3 (user mode).
 * This is used for system calls via INT instruction.
 */
void idt64_set_user_interrupt_gate(uint8_t vector, uint64_t handler) {
    idt64_set_gate(vector, handler, GDT64_KERNEL_CODE_SEGMENT,
                   IDT64_IST_NONE, IDT64_GATE_USER_INT);
}

/**
 * @brief Initialize the IDT
 * 
 * Sets up the IDT pointer and clears all entries.
 * Individual interrupt handlers must be registered separately
 * (typically in isr64.c and irq64.c).
 */
void idt64_init(void) {
    LOG_INFO_MSG("Initializing x86_64 IDT...\n");
    
    /* Set up IDT pointer */
    idt64_pointer.limit = sizeof(idt64_entries) - 1;
    idt64_pointer.base = (uint64_t)&idt64_entries;
    
    /* Clear all IDT entries */
    memset(&idt64_entries, 0, sizeof(idt64_entries));
    
    /* Note: Interrupt handlers will be registered by isr64_init() and irq64_init()
     * which should be called after idt64_init() */
    
    /* Load the IDT */
    idt64_flush((uint64_t)&idt64_pointer);
    
    LOG_INFO_MSG("x86_64 IDT initialized successfully\n");
    LOG_DEBUG_MSG("  IDT base: 0x%llx\n", (unsigned long long)idt64_pointer.base);
    LOG_DEBUG_MSG("  IDT limit: %u bytes (%u entries)\n", 
                  (unsigned int)(idt64_pointer.limit + 1), 
                  (unsigned int)((idt64_pointer.limit + 1) / sizeof(idt64_entry_t)));
}
