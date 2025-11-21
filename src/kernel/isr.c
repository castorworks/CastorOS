// ============================================================================
// isr.c - 中断服务例程实现
// ============================================================================

#include <kernel/isr.h>
#include <kernel/idt.h>
#include <kernel/gdt.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>

/* 中断处理函数数组 */
static isr_handler_t interrupt_handlers[256] = {0};

/* 中断统计计数器 */
static uint64_t interrupt_counts[256] = {0};

/* CPU 异常名称 */
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
    "Reserved",
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
 * 注册中断处理函数
 */
void isr_register_handler(uint8_t n, isr_handler_t handler) {
    interrupt_handlers[n] = handler;
    LOG_DEBUG_MSG("Registered ISR handler for interrupt %u\n", n);
}

/**
 * 通用中断处理程序
 * 由汇编 ISR 存根调用
 */
void isr_handler(registers_t *regs) {
    /* 统计中断次数 */
    interrupt_counts[regs->int_no]++;

    /* 如果注册了自定义处理函数，调用它 */
    if (interrupt_handlers[regs->int_no] != 0) {
        isr_handler_t handler = interrupt_handlers[regs->int_no];
        handler(regs);
    } else {
        /* 未处理的异常 - 显示错误信息 */
        LOG_ERROR_MSG("Unhandled exception: %s (%u)\n", 
                     exception_messages[regs->int_no], regs->int_no);
        
        /* 检查中断来源（Ring 0 还是 Ring 3） */
        bool from_usermode = (regs->cs & 0x3) == 3;
        
        kprintf("\n================================= KERNEL PANIC =================================\n");
        kprintf("Exception: %s\n", exception_messages[regs->int_no]);
        kprintf("Interrupt number: %u\n", regs->int_no);
        kprintf("Error code: %x\n", regs->err_code);
        kprintf("Mode: %s\n", from_usermode ? "User (Ring 3)" : "Kernel (Ring 0)");
        kprintf("\nRegisters:\n");
        kprintf("  EAX=%x  EBX=%x  ECX=%x  EDX=%x\n",
                regs->eax, regs->ebx, regs->ecx, regs->edx);
        kprintf("  ESI=%x  EDI=%x  EBP=%x  ESP=%x\n",
                regs->esi, regs->edi, regs->ebp, regs->esp);
        kprintf("  EIP=%x  EFLAGS=%x\n", regs->eip, regs->eflags);
        kprintf("  CS=%x  DS=%x\n", regs->cs, regs->ds);
        
        /* 只在用户态中断时打印用户栈信息 */
        if (from_usermode) {
            kprintf("  User ESP=%x  User SS=%x\n", regs->useresp, regs->ss);
        }
        
        kprintf("================================================================================\n\n");
        
        /* 挂起系统 */
        __asm__ volatile("cli; hlt");
        for(;;);
    }
}

#include <mm/vmm.h>

/**
 * 页错误处理函数（异常 #14）
 */
 static void page_fault_handler(registers_t *regs) {
    uint32_t faulting_address = get_cr2();
    
    // 尝试处理内核空间缺页（同步页目录）
    if (vmm_handle_kernel_page_fault(faulting_address)) {
        return;
    }

    page_fault_info_t pf_info = parse_page_fault_error(regs->err_code);
    
    // 检查中断来源
    bool from_usermode = (regs->cs & 0x3) == 3;
    
    LOG_ERROR_MSG("Page fault at %x (error: %x)\n", 
                 faulting_address, regs->err_code);
    
    kprintf("\n================================== PAGE FAULT ==================================\n");
    kprintf("Faulting address: %x\n", faulting_address);
    kprintf("Error code: %x\n", regs->err_code);
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
    kprintf("  EIP=%x  ESP=%x  EBP=%x\n", 
            regs->eip, regs->esp, regs->ebp);
    kprintf("  CS=%x  DS=%x\n", regs->cs, regs->ds);
    
    if (from_usermode) {
        kprintf("  User ESP=%x  User SS=%x\n", regs->useresp, regs->ss);
    }
    
    kprintf("  EFLAGS=%x\n", regs->eflags);
    
    /* 未来可以在这里实现按需分页等功能 */
    
    kprintf("================================================================================\n\n");
    
    /* 挂起系统 */
    __asm__ volatile("cli; hlt");
    for(;;);
}

/**
 * 一般保护错误处理函数（异常 #13）
 */
static void general_protection_fault_handler(registers_t *regs) {
    gpf_info_t gpf_info = parse_gpf_error(regs->err_code);
    const char* table_names[] = {"GDT", "IDT", "LDT", "LDT"};
    
    /* 检查中断来源（Ring 0 还是 Ring 3） */
    bool from_usermode = (regs->cs & 0x3) == 3;
    
    kprintf("\n=========================== GENERAL PROTECTION FAULT ===========================\n");
    kprintf("Error code: %x\n", regs->err_code);
    kprintf("\nDetails:\n");
    kprintf("  Segment: %s[%u] (selector: %x)\n", 
            table_names[gpf_info.table], 
            gpf_info.index,
            (gpf_info.index << 3) | (gpf_info.table << 1) | gpf_info.external);
    kprintf("  Source: %s\n", gpf_info.external ? "External" : "Internal");
    kprintf("  Mode: %s\n", from_usermode ? "User (Ring 3)" : "Kernel (Ring 0)");
    
    kprintf("\nRegisters:\n");
    kprintf("  EIP=%x  ESP=%x  EBP=%x\n", 
            regs->eip, regs->esp, regs->ebp);
    kprintf("  CS=%x  DS=%x\n", regs->cs, regs->ds);
    
    /* 只在用户态中断时打印用户栈信息 */
    if (from_usermode) {
        kprintf("  User ESP=%x  User SS=%x\n", regs->useresp, regs->ss);
    }
    
    kprintf("  EAX=%x  EBX=%x  ECX=%x  EDX=%x\n",
            regs->eax, regs->ebx, regs->ecx, regs->edx);
    kprintf("================================================================================\n\n");
    
    LOG_ERROR_MSG("General protection fault (error: %x)\n", regs->err_code);
    
    /* 挂起系统 */
    __asm__ volatile("cli; hlt");
    for(;;);
}

/**
 * 双重故障处理函数（异常 #8）
 */
static void double_fault_handler(registers_t *regs) {
    /* 检查中断来源（Ring 0 还是 Ring 3） */
    bool from_usermode = (regs->cs & 0x3) == 3;
    
    kprintf("\n!!!!!!!! DOUBLE FAULT !!!!!!!!\n");
    kprintf("Error code: %x\n", regs->err_code);
    kprintf("Mode: %s\n", from_usermode ? "User (Ring 3)" : "Kernel (Ring 0)");
    kprintf("\nThis is a critical error!\n");
    kprintf("The system attempted to handle an exception\n");
    kprintf("while another exception was being processed.\n");
    kprintf("\nRegisters:\n");
    kprintf("  EIP=%x  ESP=%x\n", regs->eip, regs->esp);
    kprintf("  CS=%x  DS=%x\n", regs->cs, regs->ds);
    
    /* 只在用户态中断时打印用户栈信息 */
    if (from_usermode) {
        kprintf("  User ESP=%x  User SS=%x\n", regs->useresp, regs->ss);
    }
    
    kprintf("================================================================================\n\n");
    
    LOG_ERROR_MSG("DOUBLE FAULT! System halted.\n");
    
    /* 双重故障是致命的，必须停机 */
    __asm__ volatile("cli; hlt");
    for(;;);
}

/**
 * 初始化 ISR
 */
void isr_init(void) {
    LOG_INFO_MSG("Initializing ISR...\n");

    /* 注册所有异常处理程序（0-31） */
    idt_set_gate(0,  (uint32_t)isr0,  GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(1,  (uint32_t)isr1,  GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_TRAP);
    idt_set_gate(2,  (uint32_t)isr2,  GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(3,  (uint32_t)isr3,  GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_TRAP);
    idt_set_gate(4,  (uint32_t)isr4,  GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(5,  (uint32_t)isr5,  GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(6,  (uint32_t)isr6,  GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(7,  (uint32_t)isr7,  GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(8,  (uint32_t)isr8,  GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(9,  (uint32_t)isr9,  GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(10, (uint32_t)isr10, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(11, (uint32_t)isr11, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(12, (uint32_t)isr12, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(13, (uint32_t)isr13, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(14, (uint32_t)isr14, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(15, (uint32_t)isr15, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(16, (uint32_t)isr16, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(17, (uint32_t)isr17, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(18, (uint32_t)isr18, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(19, (uint32_t)isr19, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(20, (uint32_t)isr20, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(21, (uint32_t)isr21, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(22, (uint32_t)isr22, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(23, (uint32_t)isr23, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(24, (uint32_t)isr24, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(25, (uint32_t)isr25, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(26, (uint32_t)isr26, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(27, (uint32_t)isr27, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(28, (uint32_t)isr28, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(29, (uint32_t)isr29, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(30, (uint32_t)isr30, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);
    idt_set_gate(31, (uint32_t)isr31, GDT_KERNEL_CODE_SEGMENT, 
                 IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT);

    /* 注册专门的异常处理函数 */
    isr_register_handler(8, double_fault_handler);           // 双重故障
    isr_register_handler(13, general_protection_fault_handler);  // 一般保护错误
    isr_register_handler(14, page_fault_handler);            // 页错误
    LOG_DEBUG_MSG("  Registered specialized exception handlers\n");

    LOG_INFO_MSG("ISR initialized successfully (32 exception handlers)\n");
}

/**
 * 获取特定中断的触发次数
 */
 uint64_t isr_get_interrupt_count(uint8_t int_no) {
    return interrupt_counts[int_no];
}

/**
 * 获取所有中断的总次数
 */
uint64_t isr_get_total_interrupt_count(void) {
    uint64_t total = 0;
    for (int i = 0; i < 256; i++) {
        total += interrupt_counts[i];
    }
    return total;
}

/**
 * 重置中断统计
 */
void isr_reset_interrupt_counts(void) {
    memset(interrupt_counts, 0, sizeof(interrupt_counts));
}

/**
 * 打印中断统计信息
 */
void isr_print_statistics(void) {
    kprintf("\n============================= Interrupt Statistics =============================\n");
    kprintf("Total interrupts: %llu\n\n", isr_get_total_interrupt_count());
    
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
