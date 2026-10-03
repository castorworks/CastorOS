// ============================================================================
// panic.c - 内核 Panic 和断言实现
// ============================================================================

#include <kernel/panic.h>
#include <lib/kprintf.h>
#include <lib/klog.h>

/* 架构特定的禁用中断和挂起指令 */
#if defined(ARCH_I686) || defined(ARCH_X86_64)
    #define DISABLE_INTERRUPTS() __asm__ volatile("cli")
    #define HALT_CPU()           __asm__ volatile("hlt")
#elif defined(ARCH_ARM64)
    #define DISABLE_INTERRUPTS() __asm__ volatile("msr daifset, #0xf")
    #define HALT_CPU()           __asm__ volatile("wfi")
#else
    #define DISABLE_INTERRUPTS() do {} while(0)
    #define HALT_CPU()           do {} while(0)
#endif

/**
 * 内核 Panic
 * 显示错误信息并挂起系统
 * 
 * @param message 错误消息
 * @param file 源文件名
 * @param line 行号
 */
void kernel_panic(const char* message, const char* file, int line) {
    /* 禁用中断，防止进一步的中断干扰 */
    DISABLE_INTERRUPTS();
    
    /* 显示 Panic 信息 */
    kprintf("\n");
    kprintf("================================================================================\n");
    kprintf("        KERNEL PANIC!\n");
    kprintf("================================================================================\n");
    kprintf("\n");
    kprintf("Message: %s\n", message);
    kprintf("File:    %s\n", file);
    kprintf("Line:    %d\n", line);
    kprintf("\n");
    kprintf("System halted. Please reboot.\n");
    kprintf("================================================================================\n");
    kprintf("\n");
    
    /* 同时记录到日志 */
    LOG_ERROR_MSG("KERNEL PANIC: %s (at %s:%d)\n", message, file, line);
    
    /* 挂起系统 */
    for(;;) {
        HALT_CPU();
    }
}

