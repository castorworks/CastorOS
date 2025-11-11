// ============================================================================
// kernel.c - 内核主函数
// ============================================================================

#include <drivers/vga.h>
#include <drivers/serial.h>
#include <kernel/multiboot.h>
#include <kernel/version.h>

#include <lib/klog.h>

#include <tests/test_runner.h>

// 内核主函数
void kernel_main(multiboot_info_t* mbi) {
    // ========================================================================
    // 基础初始化
    // ========================================================================
    
    // 初始化 VGA
    vga_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    // 初始化串口
    serial_init();

    // ========================================================================
    // 启动信息
    // ========================================================================
    
    vga_print("Hello CastorOS!\n");
    serial_print("Hello CastorOS!\n");
    vga_print("Version: "KERNEL_VERSION"\n");
    serial_print("Version: "KERNEL_VERSION"\n");


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
