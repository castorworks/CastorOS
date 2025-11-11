// ============================================================================
// kernel.c - 内核主函数
// ============================================================================

#include <drivers/vga.h>
#include <drivers/serial.h>
#include <kernel/multiboot.h>
#include <kernel/version.h>

#include <lib/kprintf.h>
#include <lib/klog.h>

#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/irq.h>
#include <kernel/isr.h>

#include <tests/test_runner.h>

// 内核主函数
void kernel_main(multiboot_info_t* mbi) {
    // ========================================================================
    // 阶段 0: 早期初始化
    // ========================================================================    
    vga_init(); // 初始化 VGA
    serial_init(); // 初始化串口

    // ========================================================================
    // 启动信息
    // ========================================================================
    kprintf("Welcome to CastorOS!\n");
    kprintf("Version v%s\n", KERNEL_VERSION);
    kprintf("Compiled on: %s %s\n", __DATE__, __TIME__);

    // ========================================================================
    // 阶段 1: CPU 基础架构（CPU Architecture）
    // ========================================================================
    LOG_INFO_MSG("[Stage 1] Initializing CPU architecture...\n");
    
    // 1.1 初始化 GDT（Global Descriptor Table）
    gdt_init();
    LOG_DEBUG_MSG("  [1.1] GDT initialized\n");
    
    // ========================================================================
    // 阶段 2: 中断系统（Interrupt System）
    // ========================================================================
    LOG_INFO_MSG("[Stage 2] Initializing interrupt system...\n");
    
    // 2.1 初始化 IDT（Interrupt Descriptor Table）
    idt_init();
    LOG_DEBUG_MSG("  [2.1] IDT initialized\n");
    
    // 2.2 初始化 ISR（Interrupt Service Routines - 异常处理）
    isr_init();
    LOG_DEBUG_MSG("  [2.2] ISR initialized (Exception handlers)\n");
    
    // 2.3 初始化 IRQ（Hardware Interrupt Requests - 硬件中断）
    irq_init();
    LOG_DEBUG_MSG("  [2.3] IRQ initialized (Hardware interrupts)\n");

    // ========================================================================
    // 单元测试
    // ========================================================================
    LOG_INFO_MSG("Running test suite...\n");
    run_all_tests();
    kprintf("\n");
    
    // 进入空闲循环
    while (1) {
        __asm__ volatile ("hlt");
    }
}
