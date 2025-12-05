/**
 * @file system.c
 * @brief 系统控制功能（重启、关机）
 * 
 * 支持多种关机/重启方式：
 * 1. ACPI（首选，适用于真实硬件）
 * 2. 键盘控制器重置（重启）
 * 3. QEMU/Bochs 特定端口（模拟器）
 */

#include <kernel/system.h>
#include <kernel/io.h>
#include <drivers/acpi.h>
#include <lib/klog.h>
#include <lib/kprintf.h>

/**
 * @brief 永久挂起（关机/重启失败时的最终状态）
 */
static void system_halt_forever(void) __attribute__((noreturn));

static void system_halt_forever(void) {
    kprintf("\n*** System halted ***\n");
    kprintf("It is now safe to turn off your computer.\n");
    
    while (1) {
        __asm__ volatile ("cli; hlt");
    }
}

/**
 * @brief 通过键盘控制器重启系统
 * 
 * 这是传统的 PC 重启方式，通过向键盘控制器发送复位命令
 */
static void system_reboot_keyboard_controller(void) {
    uint8_t temp;
    
    // 等待键盘控制器输入缓冲区为空
    do {
        temp = inb(0x64);
        if (temp & 0x01) {
            inb(0x60);  // 清空输出缓冲区
        }
    } while (temp & 0x02);
    
    // 发送复位命令 (0xFE) 到键盘控制器
    outb(0x64, 0xFE);
}

/**
 * @brief 通过三重故障重启系统
 * 
 * 加载空的 IDT 并触发中断，导致三重故障
 */
static void system_reboot_triple_fault(void) {
    // 加载空的 IDT
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) null_idt = {0, 0};
    
    __asm__ volatile ("lidt %0" : : "m"(null_idt));
    
    // 触发中断导致三重故障
    __asm__ volatile ("int $0x00");
}

void system_reboot(void) {
    LOG_INFO_MSG("System: Initiating reboot...\n");
    
    __asm__ volatile ("cli");
    
    // 方法 1: 尝试 ACPI 重置（如果支持）
    if (acpi_is_initialized()) {
        LOG_DEBUG_MSG("System: Trying ACPI reset...\n");
        acpi_reset();
        // 如果返回说明失败，继续尝试其他方法
    }
    
    // 方法 2: 键盘控制器复位
    LOG_DEBUG_MSG("System: Trying keyboard controller reset...\n");
    system_reboot_keyboard_controller();
    
    // 等待一段时间
    for (volatile int i = 0; i < 10000000; i++) {
        __asm__ volatile ("nop");
    }
    
    // 方法 3: 三重故障
    LOG_DEBUG_MSG("System: Trying triple fault...\n");
    system_reboot_triple_fault();
    
    // 如果所有方法都失败
    system_halt_forever();
}

void system_poweroff(void) {
    LOG_INFO_MSG("System: Initiating power off...\n");
    
    __asm__ volatile ("cli");
    
    // 方法 1: 尝试 ACPI 关机（首选，适用于真实硬件）
    if (acpi_is_initialized()) {
        LOG_INFO_MSG("System: Using ACPI for power off...\n");
        acpi_poweroff();
        // 如果返回说明失败，继续尝试其他方法
    } else {
        LOG_WARN_MSG("System: ACPI not available\n");
    }
    
    // 方法 2: QEMU 关机端口
    // QEMU 支持通过特定端口关机
    LOG_DEBUG_MSG("System: Trying QEMU shutdown ports...\n");
    
    // QEMU 标准关机端口
    outw(0xB004, 0x2000);  // QEMU 旧版本
    outw(0x604, 0x2000);   // QEMU 新版本
    
    // Bochs 关机端口
    outw(0x4004, 0x3400);
    
    // VirtualBox 关机
    outw(0x4004, 0x2000);
    
    // 方法 3: APM 关机（BIOS Power Management，比 ACPI 更老）
    // 通过 APM BIOS 调用关机（需要实模式，这里简化处理）
    LOG_DEBUG_MSG("System: Trying APM shutdown...\n");
    
    // APM 通过 INT 15h AX=5307h BX=0001h CX=0003h 关机
    // 但这需要实模式，在保护模式下不能直接调用
    // 某些 BIOS 可能支持通过 I/O 端口 APM 控制
    
    // 方法 4: 如果都失败，进入挂起状态
    LOG_WARN_MSG("System: All power off methods failed\n");
    system_halt_forever();
}


