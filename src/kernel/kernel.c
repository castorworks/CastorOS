// ============================================================================
// kernel.c - 内核主函数
// ============================================================================

#include <drivers/vga.h>
#include <drivers/serial.h>
#include <drivers/timer.h>
#include <drivers/keyboard.h>
#include <drivers/ata.h>
#include <drivers/rtc.h>
#include <drivers/pci.h>
#include <drivers/e1000.h>

#include <kernel/multiboot.h>
#include <kernel/version.h>

#include <lib/kprintf.h>
#include <lib/klog.h>

#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/irq.h>
#include <kernel/isr.h>
#include <kernel/task.h>
#include <kernel/kernel_shell.h>
#include <kernel/fs_bootstrap.h>
#include <kernel/syscall.h>

#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/heap.h>

#include <net/netdev.h>

#include <kernel/loader.h>

#include <tests/test_runner.h>

// 声明引导栈顶地址（定义在 boot.asm）
extern uint32_t stack_top;

// 内核主函数
void kernel_main(multiboot_info_t* mbi) {
    // ========================================================================
    // 阶段 0: 早期初始化
    // ========================================================================    
    vga_init(); // 初始化 VGA
    serial_init(); // 初始化串口
    
    /* 启用调试日志 */
    klog_set_level(LOG_DEBUG);

    // ========================================================================
    // 启动信息
    // ========================================================================
    kprintf("================================================================================\n");
    kprintf("Welcome to CastorOS!\n");
    kprintf("Version v%s\n", KERNEL_VERSION);
    kprintf("Compiled on: %s %s\n", __DATE__, __TIME__);
    kprintf("================================================================================\n");

    // ========================================================================
    // 阶段 1: CPU 基础架构（CPU Architecture）
    // ========================================================================
    LOG_INFO_MSG("[Stage 1] Initializing CPU architecture...\n");
    
    /* 使用内核引导栈作为临时内核栈（位于 BSS 段，安全可靠） */
    uint32_t temp_kernel_stack = (uint32_t)&stack_top;
    uint16_t kernel_ss = GDT_KERNEL_DATA_SEGMENT; /* 一般为 0x10 */

    gdt_init_all_with_tss(temp_kernel_stack, kernel_ss);

    LOG_INFO_MSG("  [1.1] GDT and TSS installed\n");
    
    // ========================================================================
    // 阶段 2: 中断系统（Interrupt System）
    // ========================================================================
    LOG_INFO_MSG("[Stage 2] Initializing interrupt system...\n");
    
    // 2.1 初始化 IDT（Interrupt Descriptor Table）
    idt_init();
    LOG_INFO_MSG("  [2.1] IDT initialized\n");
    
    // 2.2 初始化 ISR（Interrupt Service Routines - 异常处理）
    isr_init();
    LOG_INFO_MSG("  [2.2] ISR initialized (Exception handlers)\n");
    
    // 2.3 初始化 IRQ（Hardware Interrupt Requests - 硬件中断）
    irq_init();
    LOG_INFO_MSG("  [2.3] IRQ initialized (Hardware interrupts)\n");
    
    // 2.4 初始化系统调用（System Calls）
    syscall_init();
    LOG_INFO_MSG("  [2.4] System calls initialized\n");

    // ========================================================================
    // 阶段 3: 内存管理（Memory Management）
    // ========================================================================   
    LOG_INFO_MSG("[Stage 3] Initializing memory management...\n");
    
    // 3.0 显示 Multiboot 内存信息
    if (mbi && (mbi->flags & 0x01)) {
        kprintf("  Memory detected: %u KB (lower) + %u KB (upper) = %u MB\n",
                mbi->mem_lower, mbi->mem_upper,
                (mbi->mem_lower + mbi->mem_upper) / 1024);
    } else {
        LOG_WARN_MSG("  Warning: Memory info not available from bootloader\n");
    }
    
    // 3.1 初始化 PMM（Physical Memory Manager - 物理内存管理）阶段1
    //     解析内存映射，记录所有可用区域
    pmm_init(mbi);
    LOG_INFO_MSG("  [3.1] PMM phase 1 initialized\n");
    
    // 3.2 初始化 VMM（Virtual Memory Manager - 虚拟内存管理）
    vmm_init();
    LOG_INFO_MSG("  [3.2] VMM initialized\n");
    
    // 3.5 初始化 Heap（堆内存分配器）
    // 堆起始地址：PMM 位图之后（避免与位图重叠）
    uint32_t heap_start = pmm_get_bitmap_end();
    
    // 确保堆不会覆盖 multiboot 模块
    if (mbi->flags & MULTIBOOT_INFO_MODS && mbi->mods_count > 0) {
        multiboot_module_t *modules = (multiboot_module_t *)PHYS_TO_VIRT(mbi->mods_addr);
        
        // 检查模块列表结束位置
        uint32_t mods_list_end = PHYS_TO_VIRT(mbi->mods_addr + sizeof(multiboot_module_t) * mbi->mods_count);
        if (mods_list_end > heap_start) {
            heap_start = mods_list_end;
        }
        
        // 检查每个模块的结束位置
        for (uint32_t i = 0; i < mbi->mods_count; i++) {
            uint32_t mod_end_virt = PHYS_TO_VIRT(modules[i].mod_end);
            if (mod_end_virt > heap_start) {
                heap_start = mod_end_virt;
            }
        }
        
        heap_start = PAGE_ALIGN_UP(heap_start);
        LOG_INFO_MSG("  Heap start adjusted for multiboot modules: %x\n", heap_start);
    }
    
    uint32_t heap_size = 32 * 1024 * 1024;  // 32MB 堆
    heap_init(heap_start, heap_size);
    
    // 【关键】通知 PMM 堆的虚拟地址范围，防止分配会与堆重叠的物理帧
    // 这解决了堆扩展时覆盖已分配帧的恒等映射导致的页目录损坏问题
    pmm_set_heap_reserved_range(heap_start, heap_start + heap_size);
    
    heap_print_info();
    LOG_INFO_MSG("  [3.3] Heap initialized\n");

    // ========================================================================
    // 阶段 4: 设备驱动（Device Drivers）
    // ========================================================================
    
    LOG_INFO_MSG("[Stage 4] Initializing device drivers...\n");
    
    // 4.1 初始化 PIT（Programmable Interval Timer - 可编程定时器）
    timer_init(100);  // 100 Hz
    LOG_INFO_MSG("  [4.1] PIT initialized (100 Hz)\n");
    
    // 4.2 初始化键盘驱动
    keyboard_init();
    LOG_INFO_MSG("  [4.2] Keyboard initialized\n");

    // 4.3 初始化 ATA 驱动
    ata_init();
    LOG_INFO_MSG("  [4.3] ATA driver initialized\n");

    // 4.4 初始化 RTC（实时时钟）
    rtc_init();
    LOG_INFO_MSG("  [4.4] RTC initialized\n");

    // 4.5 初始化 PCI 总线
    pci_init();
    pci_scan_devices();
    LOG_INFO_MSG("  [4.5] PCI bus scanned\n");

    // 4.6 初始化网络设备子系统
    netdev_init();
    LOG_INFO_MSG("  [4.6] Network device subsystem initialized\n");

    // 4.7 初始化 E1000 网卡驱动
    int e1000_count = e1000_init();
    if (e1000_count > 0) {
        LOG_INFO_MSG("  [4.7] E1000 driver initialized (%d device(s))\n", e1000_count);
        
        // 如果有网卡，启用第一个网卡
        netdev_t *eth0 = netdev_get_by_name("eth0");
        if (eth0) {
            netdev_up(eth0);
            LOG_INFO_MSG("  Network: eth0 enabled\n");
        }
    } else {
        LOG_DEBUG_MSG("  [4.7] No E1000 network card found\n");
    }

    // ========================================================================
    // 阶段 5: 高级子系统（Advanced Subsystems）
    // ========================================================================
    LOG_INFO_MSG("[Stage 5] Initializing advanced subsystems...\n");

    // 5.1 初始化进程管理
    task_init();
    LOG_INFO_MSG("  [5.1] Task management initialized\n");

    // 5.2 初始化文件系统
    fs_init();
    LOG_INFO_MSG("  [5.2] File system initialized\n");

    // ========================================================================
    // 单元测试
    // ========================================================================
    LOG_INFO_MSG("Running test suite...\n");
    run_all_tests();
    kprintf("\n");

    // ========================================================================
    // 阶段 6: Shell（Shell）
    // ========================================================================
    LOG_INFO_MSG("[Stage 6] Starting Shell...\n");

    LOG_INFO_MSG("  [6.1] Loading user shell...\n");
    bool ok = load_user_shell();
    if (!ok) {
        LOG_ERROR_MSG("Failed to load user shell, trying to initialize kernel shell...\n");

        // 初始化 Shell
        kernel_shell_init();
        LOG_INFO_MSG("  [6.2] Kernel shell initialized\n");
        
        // 将 Shell 作为内核线程运行，这样它会出现在进程列表中
        task_create_kernel_thread(kernel_shell_run, "kernel_shell");
    }
    
    // 主线程进入空闲循环（让调度器接管）
    LOG_INFO_MSG("Kernel entering scheduler...\n");
    
    // 触发首次调度，切换到用户进程
    task_schedule();
    
    while (1) {
        __asm__ volatile ("hlt");
    }
}
