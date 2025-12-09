/**
 * @file isr64.c
 * @brief Interrupt Service Routines Implementation (x86_64)
 * 
 * This file implements the 64-bit ISR handlers for x86_64 architecture.
 * 
 * Requirements: 6.1 - Handle 64-bit exceptions with proper register save/restore
 */

#include "isr64.h"
#include "idt64.h"
#include "gdt64.h"
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>

/* Interrupt handler function array */
static isr_handler_t interrupt_handlers[256] = {0};

/* Interrupt statistics counters */
static uint64_t interrupt_counts[256] = {0};

/* CPU exception names */
static const char* exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Security Exception",
    "Reserved"
};

/**
 * @brief Register an interrupt handler
 */
void isr64_register_handler(uint8_t n, isr_handler_t handler) {
    interrupt_handlers[n] = handler;
    LOG_DEBUG_MSG("Registered ISR handler for interrupt %u\n", n);
}

/**
 * @brief Common interrupt handler (called from assembly)
 */
void isr64_handler(registers_t *regs) {
    /* Update statistics */
    interrupt_counts[regs->int_no]++;

    /* Call registered handler if present */
    if (interrupt_handlers[regs->int_no] != 0) {
        isr_handler_t handler = interrupt_handlers[regs->int_no];
        handler(regs);
    } else {
        /* Unhandled exception - display error info */
        LOG_ERROR_MSG("Unhandled exception: %s (%llu)\n", 
                     exception_messages[regs->int_no], regs->int_no);
        
        /* Check interrupt source (Ring 0 or Ring 3) */
        bool from_usermode = (regs->cs & 0x3) == 3;
        
        kprintf("\n================================= KERNEL PANIC =================================\n");
        kprintf("Exception: %s\n", exception_messages[regs->int_no]);
        kprintf("Interrupt number: %llu\n", regs->int_no);
        kprintf("Error code: 0x%llx\n", regs->err_code);
        kprintf("Mode: %s\n", from_usermode ? "User (Ring 3)" : "Kernel (Ring 0)");
        kprintf("\nRegisters:\n");
        kprintf("  RAX=0x%016llx  RBX=0x%016llx\n", regs->rax, regs->rbx);
        kprintf("  RCX=0x%016llx  RDX=0x%016llx\n", regs->rcx, regs->rdx);
        kprintf("  RSI=0x%016llx  RDI=0x%016llx\n", regs->rsi, regs->rdi);
        kprintf("  RBP=0x%016llx  RSP=0x%016llx\n", regs->rbp, regs->rsp);
        kprintf("  R8 =0x%016llx  R9 =0x%016llx\n", regs->r8, regs->r9);
        kprintf("  R10=0x%016llx  R11=0x%016llx\n", regs->r10, regs->r11);
        kprintf("  R12=0x%016llx  R13=0x%016llx\n", regs->r12, regs->r13);
        kprintf("  R14=0x%016llx  R15=0x%016llx\n", regs->r14, regs->r15);
        kprintf("  RIP=0x%016llx  RFLAGS=0x%016llx\n", regs->rip, regs->rflags);
        kprintf("  CS=0x%04llx\n", regs->cs);
        
        if (from_usermode) {
            kprintf("  User RSP=0x%016llx  User SS=0x%04llx\n", regs->rsp, regs->ss);
        }
        
        kprintf("================================================================================\n\n");
        
        /* Halt the system */
        __asm__ volatile("cli; hlt");
        for(;;);
    }
}

/* Forward declaration for VMM functions */
extern bool vmm_handle_kernel_page_fault(uint64_t fault_addr);
extern bool vmm_handle_cow_page_fault(uint64_t fault_addr, uint64_t err_code);

/**
 * @brief Page fault handler (exception #14)
 */
static void page_fault_handler(registers_t *regs) {
    uint64_t faulting_address = get_cr2();
    
    /* Try to handle kernel page fault (sync page directory) */
    if (vmm_handle_kernel_page_fault(faulting_address)) {
        return;
    }
    
    /* Try to handle COW write protection fault */
    if (vmm_handle_cow_page_fault(faulting_address, regs->err_code)) {
        return;
    }

    page_fault_info_t pf_info = parse_page_fault_error(regs->err_code);
    
    bool from_usermode = (regs->cs & 0x3) == 3;
    
    LOG_ERROR_MSG("Page fault at 0x%llx (error: 0x%llx)\n", 
                 faulting_address, regs->err_code);
    
    kprintf("\n================================== PAGE FAULT ==================================\n");
    kprintf("Faulting address: 0x%016llx\n", faulting_address);
    kprintf("Error code: 0x%llx\n", regs->err_code);
    kprintf("\nCause:\n");
    kprintf("  %s\n", pf_info.present ? "Page protection violation" : "Page not present");
    kprintf("  %s operation\n", pf_info.write ? "Write" : "Read");
    kprintf("  %s mode\n", pf_info.user ? "User" : "Kernel");
    if (pf_info.reserved) {
        kprintf("  Reserved bit overwrite\n");
    }
    if (pf_info.instruction) {
        kprintf("  Instruction fetch\n");
    }
    
    kprintf("\nRegisters:\n");
    kprintf("  RIP=0x%016llx  RSP=0x%016llx  RBP=0x%016llx\n", 
            regs->rip, regs->rsp, regs->rbp);
    kprintf("  CS=0x%04llx\n", regs->cs);
    
    if (from_usermode) {
        kprintf("  User RSP=0x%016llx  User SS=0x%04llx\n", regs->rsp, regs->ss);
    }
    
    kprintf("  RFLAGS=0x%016llx\n", regs->rflags);
    kprintf("================================================================================\n\n");
    
    /* Halt the system */
    __asm__ volatile("cli; hlt");
    for(;;);
}

/**
 * @brief General protection fault handler (exception #13)
 */
static void general_protection_fault_handler(registers_t *regs) {
    gpf_info_t gpf_info = parse_gpf_error(regs->err_code);
    const char* table_names[] = {"GDT", "IDT", "LDT", "LDT"};
    
    bool from_usermode = (regs->cs & 0x3) == 3;
    
    kprintf("\n=========================== GENERAL PROTECTION FAULT ===========================\n");
    kprintf("Error code: 0x%llx\n", regs->err_code);
    kprintf("\nDetails:\n");
    kprintf("  Segment: %s[%u] (selector: 0x%x)\n", 
            table_names[gpf_info.table], 
            gpf_info.index,
            (gpf_info.index << 3) | (gpf_info.table << 1) | gpf_info.external);
    kprintf("  Source: %s\n", gpf_info.external ? "External" : "Internal");
    kprintf("  Mode: %s\n", from_usermode ? "User (Ring 3)" : "Kernel (Ring 0)");
    
    kprintf("\nRegisters:\n");
    kprintf("  RIP=0x%016llx  RSP=0x%016llx  RBP=0x%016llx\n", 
            regs->rip, regs->rsp, regs->rbp);
    kprintf("  CS=0x%04llx\n", regs->cs);
    
    if (from_usermode) {
        kprintf("  User RSP=0x%016llx  User SS=0x%04llx\n", regs->rsp, regs->ss);
    }
    
    kprintf("  RAX=0x%016llx  RBX=0x%016llx  RCX=0x%016llx  RDX=0x%016llx\n",
            regs->rax, regs->rbx, regs->rcx, regs->rdx);
    kprintf("================================================================================\n\n");
    
    LOG_ERROR_MSG("General protection fault (error: 0x%llx)\n", regs->err_code);
    
    /* Halt the system */
    __asm__ volatile("cli; hlt");
    for(;;);
}

/**
 * @brief Double fault handler (exception #8)
 */
static void double_fault_handler(registers_t *regs) {
    bool from_usermode = (regs->cs & 0x3) == 3;
    
    kprintf("\n!!!!!!!! DOUBLE FAULT !!!!!!!!\n");
    kprintf("Error code: 0x%llx\n", regs->err_code);
    kprintf("Mode: %s\n", from_usermode ? "User (Ring 3)" : "Kernel (Ring 0)");
    kprintf("\nThis is a critical error!\n");
    kprintf("The system attempted to handle an exception\n");
    kprintf("while another exception was being processed.\n");
    kprintf("\nRegisters:\n");
    kprintf("  RIP=0x%016llx  RSP=0x%016llx\n", regs->rip, regs->rsp);
    kprintf("  CS=0x%04llx\n", regs->cs);
    
    if (from_usermode) {
        kprintf("  User RSP=0x%016llx  User SS=0x%04llx\n", regs->rsp, regs->ss);
    }
    
    kprintf("================================================================================\n\n");
    
    LOG_ERROR_MSG("DOUBLE FAULT! System halted.\n");
    
    /* Double fault is fatal - halt */
    __asm__ volatile("cli; hlt");
    for(;;);
}

/**
 * @brief Initialize ISR subsystem
 */
void isr64_init(void) {
    LOG_INFO_MSG("Initializing x86_64 ISR...\n");

    /* Register all exception handlers (0-31) in IDT */
    idt64_set_interrupt_gate(0,  (uint64_t)isr0);
    idt64_set_trap_gate(1,       (uint64_t)isr1);   /* Debug - trap gate */
    idt64_set_interrupt_gate_ist(2, (uint64_t)isr2, IDT64_IST_NMI);  /* NMI with IST */
    idt64_set_trap_gate(3,       (uint64_t)isr3);   /* Breakpoint - trap gate */
    idt64_set_interrupt_gate(4,  (uint64_t)isr4);
    idt64_set_interrupt_gate(5,  (uint64_t)isr5);
    idt64_set_interrupt_gate(6,  (uint64_t)isr6);
    idt64_set_interrupt_gate(7,  (uint64_t)isr7);
    idt64_set_interrupt_gate_ist(8, (uint64_t)isr8, IDT64_IST_DOUBLE_FAULT);  /* Double fault with IST */
    idt64_set_interrupt_gate(9,  (uint64_t)isr9);
    idt64_set_interrupt_gate(10, (uint64_t)isr10);
    idt64_set_interrupt_gate(11, (uint64_t)isr11);
    idt64_set_interrupt_gate(12, (uint64_t)isr12);
    idt64_set_interrupt_gate(13, (uint64_t)isr13);
    idt64_set_interrupt_gate(14, (uint64_t)isr14);
    idt64_set_interrupt_gate(15, (uint64_t)isr15);
    idt64_set_interrupt_gate(16, (uint64_t)isr16);
    idt64_set_interrupt_gate(17, (uint64_t)isr17);
    idt64_set_interrupt_gate_ist(18, (uint64_t)isr18, IDT64_IST_MCE);  /* Machine check with IST */
    idt64_set_interrupt_gate(19, (uint64_t)isr19);
    idt64_set_interrupt_gate(20, (uint64_t)isr20);
    idt64_set_interrupt_gate(21, (uint64_t)isr21);
    idt64_set_interrupt_gate(22, (uint64_t)isr22);
    idt64_set_interrupt_gate(23, (uint64_t)isr23);
    idt64_set_interrupt_gate(24, (uint64_t)isr24);
    idt64_set_interrupt_gate(25, (uint64_t)isr25);
    idt64_set_interrupt_gate(26, (uint64_t)isr26);
    idt64_set_interrupt_gate(27, (uint64_t)isr27);
    idt64_set_interrupt_gate(28, (uint64_t)isr28);
    idt64_set_interrupt_gate(29, (uint64_t)isr29);
    idt64_set_interrupt_gate(30, (uint64_t)isr30);
    idt64_set_interrupt_gate(31, (uint64_t)isr31);

    /* Register specialized exception handlers */
    isr64_register_handler(8, double_fault_handler);
    isr64_register_handler(13, general_protection_fault_handler);
    isr64_register_handler(14, page_fault_handler);
    LOG_DEBUG_MSG("  Registered specialized exception handlers\n");

    LOG_INFO_MSG("x86_64 ISR initialized successfully (32 exception handlers)\n");
}

/**
 * @brief Get interrupt count for a specific vector
 */
uint64_t isr64_get_interrupt_count(uint8_t int_no) {
    return interrupt_counts[int_no];
}

/**
 * @brief Get total interrupt count
 */
uint64_t isr64_get_total_interrupt_count(void) {
    uint64_t total = 0;
    for (int i = 0; i < 256; i++) {
        total += interrupt_counts[i];
    }
    return total;
}

/**
 * @brief Reset interrupt statistics
 */
void isr64_reset_interrupt_counts(void) {
    memset(interrupt_counts, 0, sizeof(interrupt_counts));
}

/**
 * @brief Print interrupt statistics
 */
void isr64_print_statistics(void) {
    kprintf("\n============================= Interrupt Statistics =============================\n");
    kprintf("Total interrupts: %llu\n\n", isr64_get_total_interrupt_count());
    
    kprintf("CPU Exceptions (0-31):\n");
    for (int i = 0; i < 32; i++) {
        if (interrupt_counts[i] > 0) {
            kprintf("  #%d (%s): %llu\n", 
                   i, exception_messages[i], interrupt_counts[i]);
        }
    }
    
    kprintf("\nHardware Interrupts (32-47):\n");
    const char* irq_names[] = {
        "Timer", "Keyboard", "Cascade", "COM2",
        "COM1", "LPT2", "Floppy", "LPT1",
        "RTC", "Free", "Free", "Free",
        "PS/2 Mouse", "FPU", "Primary ATA", "Secondary ATA"
    };
    for (int i = 32; i < 48; i++) {
        if (interrupt_counts[i] > 0) {
            kprintf("  IRQ %d (%s): %llu\n", 
                   i - 32, irq_names[i - 32], interrupt_counts[i]);
        }
    }
    
    kprintf("\nOther interrupts:\n");
    for (int i = 48; i < 256; i++) {
        if (interrupt_counts[i] > 0) {
            kprintf("  #%d: %llu\n", i, interrupt_counts[i]);
        }
    }
    kprintf("================================================================================\n\n");
}
