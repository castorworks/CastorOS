/**
 * @file idt64.h
 * @brief Interrupt Descriptor Table (x86_64)
 * 
 * This header defines the 64-bit IDT structures for x86_64 architecture.
 * 
 * In 64-bit mode, IDT entries are 16 bytes (instead of 8 in 32-bit mode)
 * to accommodate 64-bit handler addresses. The IDT supports:
 *   - 256 interrupt vectors (0-255)
 *   - IST (Interrupt Stack Table) for dedicated interrupt stacks
 *   - 64-bit handler addresses
 * 
 * Requirements: 3.4 - 64-bit IDT format with IST support
 */

#ifndef _ARCH_X86_64_IDT64_H_
#define _ARCH_X86_64_IDT64_H_

#include <types.h>

/* ============================================================================
 * IDT Entry Structure (16 bytes in 64-bit mode)
 * ========================================================================== */

/**
 * @brief 64-bit IDT Gate Descriptor
 * 
 * Structure of a 64-bit IDT entry (Interrupt/Trap Gate):
 *   Bits 0-15:   Offset (handler address) bits 0-15
 *   Bits 16-31:  Segment selector
 *   Bits 32-34:  IST (Interrupt Stack Table) index (0 = no IST)
 *   Bits 35-39:  Reserved (must be 0)
 *   Bits 40-43:  Gate type (0xE = Interrupt Gate, 0xF = Trap Gate)
 *   Bit 44:      0 (must be 0 for interrupt/trap gates)
 *   Bits 45-46:  DPL (Descriptor Privilege Level)
 *   Bit 47:      Present bit
 *   Bits 48-63:  Offset bits 16-31
 *   Bits 64-95:  Offset bits 32-63
 *   Bits 96-127: Reserved (must be 0)
 */
typedef struct idt64_entry {
    uint16_t offset_low;        /* Handler address bits 0-15 */
    uint16_t selector;          /* Code segment selector */
    uint8_t  ist;               /* IST index (bits 0-2), reserved (bits 3-7) */
    uint8_t  type_attr;         /* Type and attributes */
    uint16_t offset_mid;        /* Handler address bits 16-31 */
    uint32_t offset_high;       /* Handler address bits 32-63 */
    uint32_t reserved;          /* Reserved, must be 0 */
} __attribute__((packed)) idt64_entry_t;

/* ============================================================================
 * IDTR Structure (10 bytes in 64-bit mode)
 * ========================================================================== */

/**
 * @brief IDT Register structure for LIDT instruction
 */
typedef struct idt64_ptr {
    uint16_t limit;             /* IDT size - 1 */
    uint64_t base;              /* IDT base address (64-bit) */
} __attribute__((packed)) idt64_ptr_t;

/* ============================================================================
 * IDT Gate Types
 * ========================================================================== */

/* Gate type values (bits 0-3 of type_attr) */
#define IDT64_TYPE_INTERRUPT    0x0E    /* 64-bit Interrupt Gate */
#define IDT64_TYPE_TRAP         0x0F    /* 64-bit Trap Gate */

/* ============================================================================
 * IDT Attribute Flags
 * ========================================================================== */

#define IDT64_ATTR_PRESENT      0x80    /* Present bit (bit 7) */
#define IDT64_ATTR_DPL_RING0    0x00    /* Ring 0 (kernel) */
#define IDT64_ATTR_DPL_RING3    0x60    /* Ring 3 (user) */

/* Combined attributes for common gate types */
#define IDT64_GATE_INTERRUPT    (IDT64_ATTR_PRESENT | IDT64_ATTR_DPL_RING0 | IDT64_TYPE_INTERRUPT)
#define IDT64_GATE_TRAP         (IDT64_ATTR_PRESENT | IDT64_ATTR_DPL_RING0 | IDT64_TYPE_TRAP)
#define IDT64_GATE_USER_INT     (IDT64_ATTR_PRESENT | IDT64_ATTR_DPL_RING3 | IDT64_TYPE_INTERRUPT)

/* ============================================================================
 * IST (Interrupt Stack Table) Indices
 * ========================================================================== */

/**
 * IST allows specifying a dedicated stack for specific interrupts.
 * This is useful for handling critical exceptions (like double fault,
 * NMI, machine check) that might occur when the kernel stack is corrupted.
 * 
 * IST index 0 means "use the normal stack switching mechanism"
 * IST indices 1-7 refer to IST entries in the TSS
 */
#define IDT64_IST_NONE          0       /* Use normal stack switching */
#define IDT64_IST_DOUBLE_FAULT  1       /* Dedicated stack for #DF */
#define IDT64_IST_NMI           2       /* Dedicated stack for NMI */
#define IDT64_IST_DEBUG         3       /* Dedicated stack for debug exceptions */
#define IDT64_IST_MCE           4       /* Dedicated stack for Machine Check */

/* ============================================================================
 * Interrupt Vector Numbers
 * ========================================================================== */

/* CPU Exceptions (0-31) */
#define IDT64_VECTOR_DIVIDE_ERROR       0   /* #DE - Divide Error */
#define IDT64_VECTOR_DEBUG              1   /* #DB - Debug Exception */
#define IDT64_VECTOR_NMI                2   /* NMI - Non-Maskable Interrupt */
#define IDT64_VECTOR_BREAKPOINT         3   /* #BP - Breakpoint */
#define IDT64_VECTOR_OVERFLOW           4   /* #OF - Overflow */
#define IDT64_VECTOR_BOUND_RANGE        5   /* #BR - Bound Range Exceeded */
#define IDT64_VECTOR_INVALID_OPCODE     6   /* #UD - Invalid Opcode */
#define IDT64_VECTOR_DEVICE_NOT_AVAIL   7   /* #NM - Device Not Available */
#define IDT64_VECTOR_DOUBLE_FAULT       8   /* #DF - Double Fault */
#define IDT64_VECTOR_COPROC_SEGMENT     9   /* Coprocessor Segment Overrun (legacy) */
#define IDT64_VECTOR_INVALID_TSS        10  /* #TS - Invalid TSS */
#define IDT64_VECTOR_SEGMENT_NOT_PRES   11  /* #NP - Segment Not Present */
#define IDT64_VECTOR_STACK_SEGMENT      12  /* #SS - Stack-Segment Fault */
#define IDT64_VECTOR_GENERAL_PROTECT    13  /* #GP - General Protection Fault */
#define IDT64_VECTOR_PAGE_FAULT         14  /* #PF - Page Fault */
#define IDT64_VECTOR_RESERVED_15        15  /* Reserved */
#define IDT64_VECTOR_X87_FPU            16  /* #MF - x87 FPU Error */
#define IDT64_VECTOR_ALIGNMENT_CHECK    17  /* #AC - Alignment Check */
#define IDT64_VECTOR_MACHINE_CHECK      18  /* #MC - Machine Check */
#define IDT64_VECTOR_SIMD_FP            19  /* #XM/#XF - SIMD Floating-Point */
#define IDT64_VECTOR_VIRTUALIZATION     20  /* #VE - Virtualization Exception */
#define IDT64_VECTOR_CONTROL_PROTECT    21  /* #CP - Control Protection Exception */
/* Vectors 22-31 are reserved */

/* Hardware IRQs (remapped to 32-47 typically) */
#define IDT64_VECTOR_IRQ_BASE           32
#define IDT64_VECTOR_IRQ0               32  /* Timer */
#define IDT64_VECTOR_IRQ1               33  /* Keyboard */
#define IDT64_VECTOR_IRQ2               34  /* Cascade */
#define IDT64_VECTOR_IRQ3               35  /* COM2 */
#define IDT64_VECTOR_IRQ4               36  /* COM1 */
#define IDT64_VECTOR_IRQ5               37  /* LPT2 */
#define IDT64_VECTOR_IRQ6               38  /* Floppy */
#define IDT64_VECTOR_IRQ7               39  /* LPT1 / Spurious */
#define IDT64_VECTOR_IRQ8               40  /* RTC */
#define IDT64_VECTOR_IRQ9               41  /* Free */
#define IDT64_VECTOR_IRQ10              42  /* Free */
#define IDT64_VECTOR_IRQ11              43  /* Free */
#define IDT64_VECTOR_IRQ12              44  /* PS/2 Mouse */
#define IDT64_VECTOR_IRQ13              45  /* FPU */
#define IDT64_VECTOR_IRQ14              46  /* Primary ATA */
#define IDT64_VECTOR_IRQ15              47  /* Secondary ATA */

/* System call vector */
#define IDT64_VECTOR_SYSCALL            0x80

/* ============================================================================
 * API Functions
 * ========================================================================== */

/**
 * @brief Initialize the IDT
 * 
 * Sets up the IDT pointer and clears all entries.
 * Individual interrupt handlers must be registered separately.
 */
void idt64_init(void);

/**
 * @brief Set an IDT gate entry
 * @param vector Interrupt vector number (0-255)
 * @param handler Handler function address
 * @param selector Code segment selector
 * @param ist IST index (0 for none, 1-7 for IST entries)
 * @param type_attr Type and attribute flags
 */
void idt64_set_gate(uint8_t vector, uint64_t handler, uint16_t selector,
                    uint8_t ist, uint8_t type_attr);

/**
 * @brief Set an interrupt gate (interrupts disabled during handler)
 * @param vector Interrupt vector number
 * @param handler Handler function address
 */
void idt64_set_interrupt_gate(uint8_t vector, uint64_t handler);

/**
 * @brief Set an interrupt gate with IST
 * @param vector Interrupt vector number
 * @param handler Handler function address
 * @param ist IST index (1-7)
 */
void idt64_set_interrupt_gate_ist(uint8_t vector, uint64_t handler, uint8_t ist);

/**
 * @brief Set a trap gate (interrupts remain enabled during handler)
 * @param vector Interrupt vector number
 * @param handler Handler function address
 */
void idt64_set_trap_gate(uint8_t vector, uint64_t handler);

/**
 * @brief Set a user-callable interrupt gate (DPL=3)
 * @param vector Interrupt vector number
 * @param handler Handler function address
 */
void idt64_set_user_interrupt_gate(uint8_t vector, uint64_t handler);

/* ============================================================================
 * Assembly Functions (defined in idt64_asm.asm)
 * ========================================================================== */

/**
 * @brief Load IDT using LIDT instruction
 * @param idt_ptr Pointer to IDT pointer structure
 */
extern void idt64_flush(uint64_t idt_ptr);

#endif /* _ARCH_X86_64_IDT64_H_ */
