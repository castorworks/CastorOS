/**
 * @file isr64.h
 * @brief Interrupt Service Routines (x86_64)
 * 
 * This header defines the 64-bit ISR structures and functions for x86_64.
 * 
 * Key differences from 32-bit:
 *   - 64-bit registers (RAX-R15)
 *   - Different stack frame layout
 *   - No PUSHA/POPA instructions (must save registers individually)
 *   - IRETQ instead of IRET
 * 
 * Requirements: 6.1 - Save/restore 64-bit register state
 */

#ifndef _ARCH_X86_64_ISR64_H_
#define _ARCH_X86_64_ISR64_H_

#include <types.h>

/**
 * @brief 64-bit interrupt register state
 * 
 * This structure represents the CPU state saved during an interrupt.
 * The layout must match exactly what the assembly stub pushes onto the stack.
 * 
 * Stack layout (from high to low address):
 *   [CPU pushed - if from Ring 3]
 *   SS, RSP (user)
 *   [CPU pushed - always]
 *   RFLAGS, CS, RIP
 *   [Error code - pushed by CPU or stub]
 *   [Interrupt number - pushed by stub]
 *   [General purpose registers - pushed by stub]
 *   R15, R14, R13, R12, R11, R10, R9, R8
 *   RDI, RSI, RBP, RDX, RCX, RBX, RAX
 */
typedef struct {
    /* General purpose registers (pushed by stub) */
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    
    /* Interrupt info (pushed by stub) */
    uint64_t int_no;
    uint64_t err_code;
    
    /* CPU-pushed interrupt frame */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;       /* User RSP (only valid if from Ring 3) */
    uint64_t ss;        /* User SS (only valid if from Ring 3) */
} __attribute__((packed)) registers64_t;

/* Alias for compatibility with generic kernel code */
typedef registers64_t registers_t;

/**
 * @brief Interrupt handler function type
 */
typedef void (*isr_handler_t)(registers_t *regs);

/**
 * @brief Initialize ISR subsystem
 * 
 * Registers all CPU exception handlers (vectors 0-31) in the IDT.
 */
void isr64_init(void);

/**
 * @brief Register an interrupt handler
 * @param n Interrupt vector number (0-255)
 * @param handler Handler function
 */
void isr64_register_handler(uint8_t n, isr_handler_t handler);

/* Wrapper for compatibility */
#define isr_init() isr64_init()
#define isr_register_handler(n, h) isr64_register_handler(n, h)

/* ============================================================================
 * CPU Exception Entry Points (defined in isr64_asm.asm)
 * ========================================================================== */

extern void isr0(void);   /* #DE - Divide Error */
extern void isr1(void);   /* #DB - Debug Exception */
extern void isr2(void);   /* NMI - Non-Maskable Interrupt */
extern void isr3(void);   /* #BP - Breakpoint */
extern void isr4(void);   /* #OF - Overflow */
extern void isr5(void);   /* #BR - Bound Range Exceeded */
extern void isr6(void);   /* #UD - Invalid Opcode */
extern void isr7(void);   /* #NM - Device Not Available */
extern void isr8(void);   /* #DF - Double Fault */
extern void isr9(void);   /* Coprocessor Segment Overrun (legacy) */
extern void isr10(void);  /* #TS - Invalid TSS */
extern void isr11(void);  /* #NP - Segment Not Present */
extern void isr12(void);  /* #SS - Stack-Segment Fault */
extern void isr13(void);  /* #GP - General Protection Fault */
extern void isr14(void);  /* #PF - Page Fault */
extern void isr15(void);  /* Reserved */
extern void isr16(void);  /* #MF - x87 FPU Error */
extern void isr17(void);  /* #AC - Alignment Check */
extern void isr18(void);  /* #MC - Machine Check */
extern void isr19(void);  /* #XM/#XF - SIMD Floating-Point */
extern void isr20(void);  /* #VE - Virtualization Exception */
extern void isr21(void);  /* #CP - Control Protection Exception */
extern void isr22(void);  /* Reserved */
extern void isr23(void);  /* Reserved */
extern void isr24(void);  /* Reserved */
extern void isr25(void);  /* Reserved */
extern void isr26(void);  /* Reserved */
extern void isr27(void);  /* Reserved */
extern void isr28(void);  /* Reserved */
extern void isr29(void);  /* Reserved */
extern void isr30(void);  /* #SX - Security Exception */
extern void isr31(void);  /* Reserved */

/* ============================================================================
 * Helper Functions
 * ========================================================================== */

/**
 * @brief Get CR2 register (page fault address)
 */
static inline uint64_t get_cr2(void) {
    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}

/**
 * @brief Page fault error code information
 */
typedef struct {
    bool present;      /* 0: Page not present, 1: Protection violation */
    bool write;        /* 0: Read access, 1: Write access */
    bool user;         /* 0: Kernel mode, 1: User mode */
    bool reserved;     /* 1: Reserved bit overwrite */
    bool instruction;  /* 1: Instruction fetch */
} page_fault_info_t;

static inline page_fault_info_t parse_page_fault_error(uint64_t err_code) {
    page_fault_info_t info;
    info.present = (err_code & 0x1) != 0;
    info.write = (err_code & 0x2) != 0;
    info.user = (err_code & 0x4) != 0;
    info.reserved = (err_code & 0x8) != 0;
    info.instruction = (err_code & 0x10) != 0;
    return info;
}

/**
 * @brief General protection fault error code information
 */
typedef struct {
    bool external;     /* 1: External event */
    uint8_t table;     /* 0: GDT, 1: IDT, 2/3: LDT */
    uint16_t index;    /* Selector index */
} gpf_info_t;

static inline gpf_info_t parse_gpf_error(uint64_t err_code) {
    gpf_info_t info;
    info.external = (err_code & 0x1) != 0;
    info.table = (err_code >> 1) & 0x3;
    info.index = (err_code >> 3) & 0x1FFF;
    return info;
}

/**
 * @brief Get interrupt count for a specific vector
 */
uint64_t isr64_get_interrupt_count(uint8_t int_no);

/**
 * @brief Get total interrupt count
 */
uint64_t isr64_get_total_interrupt_count(void);

/**
 * @brief Reset interrupt statistics
 */
void isr64_reset_interrupt_counts(void);

/**
 * @brief Print interrupt statistics
 */
void isr64_print_statistics(void);

/* Compatibility wrappers */
#define isr_get_interrupt_count(n) isr64_get_interrupt_count(n)
#define isr_get_total_interrupt_count() isr64_get_total_interrupt_count()
#define isr_reset_interrupt_counts() isr64_reset_interrupt_counts()
#define isr_print_statistics() isr64_print_statistics()

#endif /* _ARCH_X86_64_ISR64_H_ */
