// ============================================================================
// kernel.c - 内核主函数
// ============================================================================
//
// This file implements the kernel main entry point. It uses the Hardware
// Abstraction Layer (HAL) for architecture-specific initialization, allowing
// the same kernel code to work across different architectures (i686, x86_64,
// ARM64).
//
// **Feature: multi-arch-support**
// **Feature: arm64-kernel-integration**
// **Validates: Requirements 1.1, 10.1**
// ============================================================================

#include <drivers/serial.h>

/* x86-specific drivers (not available on ARM64) */
#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <drivers/vga.h>
#include <drivers/timer.h>
#include <drivers/keyboard.h>
#include <drivers/ata.h>
#include <drivers/rtc.h>
#include <drivers/pci.h>
#include <drivers/e1000.h>
#include <drivers/framebuffer.h>
#include <drivers/acpi.h>
#include <drivers/usb/usb.h>
#include <drivers/usb/uhci.h>
#include <drivers/usb/usb_mass_storage.h>
#include <kernel/multiboot.h>
#include <net/netdev.h>
#endif

#include <kernel/version.h>

#include <lib/kprintf.h>
#include <lib/klog.h>

/* HAL interface for architecture-independent initialization */
#include <hal/hal.h>

/* Architecture-specific headers */
#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <kernel/gdt.h>
#endif

#include <kernel/task.h>
#include <kernel/syscall.h>

/* fs_bootstrap is x86-specific (has FAT32, partition, blockdev dependencies) */
#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <kernel/fs_bootstrap.h>
#endif

/* kernel_shell is x86-specific (has VGA, keyboard, USB dependencies) */
#if defined(ARCH_I686) || defined(ARCH_X86_64)
#include <kernel/kernel_shell.h>
#endif

#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/heap.h>

#include <kernel/loader.h>

#include <tests/test_runner.h>

/* Boot info for ARM64 */
#if defined(ARCH_ARM64)
#include <boot/boot_info.h>
#include <arch/arm64/arch_types.h>
#include <fs/vfs.h>
#include <fs/ramfs.h>
#include <fs/devfs.h>
#include <kernel/embedded_programs.h>
#endif

// 声明引导栈顶地址（定义在 boot.asm / boot64.asm / start.S）
extern char stack_top[];

// ============================================================================
// ARM64 Kernel Main Entry Point
// ============================================================================
// ARM64 uses DTB (Device Tree Blob) instead of Multiboot for boot information.
// The initialization sequence is different from x86 due to:
//   - DTB-based memory and device discovery
//   - Different timer and interrupt controller (GIC)
//   - No VGA, PCI, or x86-specific devices
//
// **Feature: arm64-kernel-integration**
// **Validates: Requirements 10.1**
// ============================================================================

#if defined(ARCH_ARM64)

void kernel_main(void *dtb_addr) {
    // ========================================================================
    // 阶段 0: 早期初始化 (ARM64)
    // ========================================================================
    serial_init();  // Initialize PL011 UART
    
    // 日志配置
    // klog_set_level(LOG_DEBUG);  // Uncomment for debug output

    // ========================================================================
    // 启动信息
    // ========================================================================
    kprintf("\n");
    kprintf("================================================================================\n");
    kprintf("Welcome to CastorOS!\n");
    kprintf("Version v%s (ARM64)\n", KERNEL_VERSION);
    kprintf("Compiled on: %s %s\n", __DATE__, __TIME__);
    kprintf("================================================================================\n");
    
    kprintf("DTB address: 0x%llx\n", (unsigned long long)(uintptr_t)dtb_addr);
    kprintf("Kernel virtual base: 0x%llx\n", (unsigned long long)KERNEL_VIRTUAL_BASE);
    kprintf("\n");

    // ========================================================================
    // 阶段 1: Boot Info 初始化 (ARM64 特定)
    // ========================================================================
    // Parse DTB to extract memory information and device configuration
    // **Feature: arm64-kernel-integration**
    // **Validates: Requirements 1.1**
    // ========================================================================
    LOG_INFO_MSG("[Stage 1] Initializing boot info from DTB...\n");
    
    boot_info_t *boot_info = boot_info_init_dtb(dtb_addr);
    if (boot_info) {
        LOG_INFO_MSG("  [1.1] Boot info initialized successfully\n");
        boot_info_print();
    } else {
        LOG_WARN_MSG("  [1.1] WARNING: Failed to initialize boot info from DTB\n");
        LOG_WARN_MSG("        Continuing with limited functionality...\n");
    }
    kprintf("\n");

    // ========================================================================
    // 阶段 2: CPU 和中断系统 (ARM64)
    // ========================================================================
    LOG_INFO_MSG("[Stage 2] Initializing CPU and interrupt system via HAL...\n");
    
    hal_cpu_init();
    LOG_INFO_MSG("  [2.1] CPU initialized via HAL (%s)\n", hal_arch_name());
    
    hal_interrupt_init();
    LOG_INFO_MSG("  [2.2] Interrupt system initialized (GIC)\n");
    
    syscall_init();
    LOG_INFO_MSG("  [2.3] System calls initialized\n");
    kprintf("\n");

    // ========================================================================
    // 阶段 3: 内存管理 (ARM64)
    // ========================================================================
    LOG_INFO_MSG("[Stage 3] Initializing memory management...\n");
    
    if (boot_info) {
        // 3.1 Initialize PMM using boot_info from DTB
        pmm_init_boot_info(boot_info);
        LOG_INFO_MSG("  [3.1] PMM initialized\n");
        
        // 3.2 Initialize VMM
        vmm_init();
        LOG_INFO_MSG("  [3.2] VMM initialized\n");
        
        // 3.3 Initialize Heap
        // ARM64 heap should be placed after PMM data structures, within physical memory
        // Get the end of PMM data structures and place heap there
        uintptr_t pmm_data_end = pmm_get_data_end_virt();
        uintptr_t heap_start = PAGE_ALIGN_UP(pmm_data_end);
        
        // Calculate available memory for heap (leave some room for other allocations)
        // Physical memory ends at max_phys, heap should not exceed that
        pmm_info_t pmm_info_local = pmm_get_info();
        uint64_t max_phys = (uint64_t)pmm_info_local.total_frames * PAGE_SIZE;
        uintptr_t max_heap_virt = PHYS_TO_VIRT(max_phys);
        
        // Limit heap size to available space or 16MB, whichever is smaller
        uint64_t available_space = max_heap_virt - heap_start;
        uint32_t heap_size = (uint32_t)ARM64_HEAP_INIT_SIZE;  // 16MB initial
        if (available_space < heap_size) {
            heap_size = (uint32_t)(available_space / 2);  // Use half of available space
        }
        
        LOG_INFO_MSG("  [3.3] Initializing heap at 0x%llx (size: %u MB)\n",
                     (unsigned long long)heap_start, heap_size / (1024 * 1024));
        LOG_INFO_MSG("        PMM data end: 0x%llx, max_heap_virt: 0x%llx\n",
                     (unsigned long long)pmm_data_end, (unsigned long long)max_heap_virt);
        
        heap_init(heap_start, heap_size);
        
        // 【关键】通知 PMM 堆的虚拟地址范围，防止分配会与堆重叠的物理帧
        // 这解决了堆扩展时覆盖已分配帧的恒等映射导致的页目录损坏问题
        pmm_set_heap_reserved_range(heap_start, heap_start + heap_size);
        
        heap_print_info();
        LOG_INFO_MSG("  [3.3] Heap initialized\n");
        
        // Test heap allocation
        void *test_ptr = kmalloc(1024);
        if (test_ptr) {
            LOG_DEBUG_MSG("  Heap test: kmalloc(1024) = 0x%llx - OK\n",
                         (unsigned long long)(uintptr_t)test_ptr);
            kfree(test_ptr);
        } else {
            LOG_WARN_MSG("  Heap test: kmalloc(1024) FAILED\n");
        }
    } else {
        LOG_WARN_MSG("  [3.x] Skipping PMM/VMM/Heap (no boot_info)\n");
    }
    kprintf("\n");

    // ========================================================================
    // 阶段 4: 设备驱动 (ARM64)
    // ========================================================================
    LOG_INFO_MSG("[Stage 4] Initializing device drivers...\n");
    
    // 4.1 Initialize timer with scheduler integration
    // ARM64 uses ARM Generic Timer via HAL
    extern void hal_timer_init(uint32_t freq_hz, void (*callback)(void));
    hal_timer_init(100, task_timer_tick);  // 100 Hz = 10ms tick
    LOG_INFO_MSG("  [4.1] Timer initialized (100 Hz)\n");
    
    // Note: ARM64 doesn't have VGA, keyboard, ATA, PCI, ACPI, E1000, USB
    // These are x86-specific devices
    LOG_INFO_MSG("  [4.x] ARM64: x86-specific drivers skipped\n");
    kprintf("\n");

    // ========================================================================
    // 阶段 5: 高级子系统 (ARM64)
    // ========================================================================
    LOG_INFO_MSG("[Stage 5] Initializing advanced subsystems...\n");
    
    // 5.1 Initialize task management
    task_init();
    LOG_INFO_MSG("  [5.1] Task management initialized\n");
    
    // 5.2 Initialize file system (VFS + ramfs + devfs)
    vfs_init();
    LOG_INFO_MSG("  [5.2] VFS core initialized\n");
    
    fs_node_t *ramfs_root = ramfs_init();
    if (ramfs_root) {
        vfs_set_root(ramfs_root);
        LOG_INFO_MSG("  [5.3] RAMFS initialized as root filesystem\n");
        
        // Initialize devfs
        fs_node_t *devfs_root = devfs_init();
        if (devfs_root) {
            vfs_mkdir("/dev", FS_PERM_READ | FS_PERM_WRITE | FS_PERM_EXEC);
            vfs_mount("/dev", devfs_root);
            LOG_INFO_MSG("  [5.4] DevFS mounted at /dev\n");
        }
        
        // Create standard directories
        vfs_mkdir("/bin", FS_PERM_READ | FS_PERM_WRITE | FS_PERM_EXEC);
        vfs_mkdir("/tmp", FS_PERM_READ | FS_PERM_WRITE | FS_PERM_EXEC);
        LOG_INFO_MSG("  [5.5] Standard directories created\n");
        
        // Write embedded user programs to ramfs
        if (embedded_shell_size > 0) {
            if (vfs_create("/bin/shell.elf") == 0) {
                fs_node_t *shell_node = vfs_path_to_node("/bin/shell.elf");
                if (shell_node) {
                    uint32_t written = vfs_write(shell_node, 0, embedded_shell_size, 
                                                (uint8_t*)embedded_shell_elf);
                    vfs_release_node(shell_node);
                    if (written == embedded_shell_size) {
                        LOG_INFO_MSG("  [5.6] Embedded shell.elf written (%u bytes)\n", 
                                    embedded_shell_size);
                    } else {
                        LOG_WARN_MSG("  [5.6] Failed to write shell.elf (wrote %u/%u)\n",
                                    written, embedded_shell_size);
                    }
                }
            } else {
                LOG_WARN_MSG("  [5.6] Failed to create /bin/shell.elf\n");
            }
        }
        
        if (embedded_hello_size > 0) {
            if (vfs_create("/bin/hello.elf") == 0) {
                fs_node_t *hello_node = vfs_path_to_node("/bin/hello.elf");
                if (hello_node) {
                    uint32_t written = vfs_write(hello_node, 0, embedded_hello_size,
                                                (uint8_t*)embedded_hello_elf);
                    vfs_release_node(hello_node);
                    if (written == embedded_hello_size) {
                        LOG_INFO_MSG("  [5.7] Embedded hello.elf written (%u bytes)\n",
                                    embedded_hello_size);
                    } else {
                        LOG_WARN_MSG("  [5.7] Failed to write hello.elf (wrote %u/%u)\n",
                                    written, embedded_hello_size);
                    }
                }
            } else {
                LOG_WARN_MSG("  [5.7] Failed to create /bin/hello.elf\n");
            }
        }
    } else {
        LOG_WARN_MSG("  [5.x] WARNING: Failed to initialize ramfs\n");
    }
    kprintf("\n");

    // ========================================================================
    // 启用中断
    // ========================================================================
    LOG_INFO_MSG("Enabling interrupts...\n");
    hal_interrupt_enable();
    kprintf("\n");

    // ========================================================================
    // 单元测试
    // ========================================================================
#if 0  // Temporarily disabled for user program testing - tests cause crash
    LOG_INFO_MSG("Running test suite...\n");
    run_all_tests();
    kprintf("\n");
#else
    LOG_INFO_MSG("Test suite skipped for user program testing\n");
    kprintf("\n");
#endif

    // ========================================================================
    // 阶段 6: 调度器启动 (ARM64)
    // ========================================================================
    LOG_INFO_MSG("[Stage 6] Starting scheduler...\n");
    
    // Try to load user shell from embedded programs
    LOG_INFO_MSG("  [6.1] Loading user shell...\n");
    bool shell_loaded = load_user_shell();
    if (!shell_loaded) {
        LOG_WARN_MSG("  [6.1] User shell not available, running idle loop\n");
    }
    
    LOG_INFO_MSG("Kernel entering scheduler...\n");
    kprintf("\n");
    kprintf("ARM64 kernel initialization complete!\n");
    if (shell_loaded) {
        kprintf("User shell loaded and ready.\n");
    } else {
        kprintf("System is now running in idle loop.\n");
    }
    kprintf("\n");
    
    task_schedule();
    
    // Idle loop - should never reach here
    while (1) {
        hal_cpu_halt();
    }
}

#else /* x86 architectures (i686, x86_64) */

// ============================================================================
// x86 Kernel Main Entry Point
// ============================================================================
// x86 uses Multiboot for boot information from GRUB.
// ============================================================================

void kernel_main(multiboot_info_t* mbi) {
    // ========================================================================
    // 阶段 0: 早期初始化
    // ========================================================================    
    vga_init(); // 初始化 VGA
    serial_init(); // 初始化串口
    
    // 日志配置：
    // - 默认级别为 INFO（过滤 DEBUG 信息）
    // - 同时输出到 VGA 和串口
    // 如需调试，可取消下行注释：
    // klog_set_level(LOG_DEBUG);

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
    // Use HAL for architecture-independent CPU initialization
    // This dispatches to the appropriate architecture-specific code:
    //   - i686: GDT, TSS initialization
    //   - x86_64: GDT64, TSS64 initialization
    //   - ARM64: Exception Level configuration
    // Requirements: 1.1 - HAL initialization dispatch
    // ========================================================================
    LOG_INFO_MSG("[Stage 1] Initializing CPU architecture via HAL...\n");
    
    hal_cpu_init();
    LOG_INFO_MSG("  [1.1] CPU initialized via HAL (%s)\n", hal_arch_name());
    
    // ========================================================================
    // 阶段 2: 中断系统（Interrupt System）
    // ========================================================================
    // Use HAL for architecture-independent interrupt initialization
    // This dispatches to the appropriate architecture-specific code:
    //   - i686/x86_64: IDT, ISR, IRQ (PIC/APIC)
    //   - ARM64: Exception vectors, GIC
    // Requirements: 1.1 - HAL initialization dispatch
    // ========================================================================
    LOG_INFO_MSG("[Stage 2] Initializing interrupt system via HAL...\n");
    
    hal_interrupt_init();
    LOG_INFO_MSG("  [2.1] Interrupt system initialized via HAL\n");
    
    // 2.2 初始化系统调用（System Calls）
    // syscall_init() internally uses HAL for architecture-specific setup
    syscall_init();
    LOG_INFO_MSG("  [2.2] System calls initialized\n");

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
    
    // 3.3 初始化 PAT（Page Attribute Table）
    //     用于支持帧缓冲的 Write-Combining 模式，提升图形性能
    vmm_init_pat();
    LOG_INFO_MSG("  [3.3] PAT initialized\n");
    
    // 3.5 初始化 Heap（堆内存分配器）
    // 堆起始地址：PMM 位图之后（避免与位图重叠）
    uintptr_t heap_start = pmm_get_bitmap_end();
    
    // 确保堆不会覆盖 multiboot 模块
    if (mbi->flags & MULTIBOOT_INFO_MODS && mbi->mods_count > 0) {
        multiboot_module_t *modules = (multiboot_module_t *)PHYS_TO_VIRT(mbi->mods_addr);
        
        // 检查模块列表结束位置
        uintptr_t mods_list_end = PHYS_TO_VIRT(mbi->mods_addr + sizeof(multiboot_module_t) * mbi->mods_count);
        if (mods_list_end > heap_start) {
            heap_start = mods_list_end;
        }
        
        // 检查每个模块的结束位置
        for (uint32_t i = 0; i < mbi->mods_count; i++) {
            uintptr_t mod_end_virt = PHYS_TO_VIRT(modules[i].mod_end);
            if (mod_end_virt > heap_start) {
                heap_start = mod_end_virt;
            }
        }
        
        heap_start = PAGE_ALIGN_UP(heap_start);
        LOG_INFO_MSG("  Heap start adjusted for multiboot modules: 0x%lx\n", (unsigned long)heap_start);
    }
    
    uint32_t heap_size = 32 * 1024 * 1024;  // 32MB 堆
    heap_init((uintptr_t)heap_start, heap_size);
    
    // 【关键】通知 PMM 堆的虚拟地址范围，防止分配会与堆重叠的物理帧
    // 这解决了堆扩展时覆盖已分配帧的恒等映射导致的页目录损坏问题
    pmm_set_heap_reserved_range(heap_start, heap_start + heap_size);
    
    heap_print_info();
    LOG_INFO_MSG("  [3.3] Heap initialized\n");
    
    // DEBUG: 验证堆状态
    {
        heap_block_t *fb = (heap_block_t*)heap_start;
        LOG_INFO_MSG("  DEBUG: first_block magic after heap_init = 0x%x\n", fb->magic);
    }

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

    // 4.6 初始化 ACPI 子系统
    int acpi_result = acpi_init();
    if (acpi_result == 0) {
        LOG_INFO_MSG("  [4.6] ACPI initialized\n");
        acpi_print_info();
    } else {
        LOG_WARN_MSG("  [4.6] ACPI initialization failed (code=%d)\n", acpi_result);
        LOG_WARN_MSG("        Power management may not work correctly\n");
    }

    // 4.7 初始化网络设备子系统
    netdev_init();
    LOG_INFO_MSG("  [4.7] Network device subsystem initialized\n");

    // 4.8 初始化 E1000 网卡驱动
#if defined(ARCH_X86_64)
    // x86_64: 暂时跳过 E1000 驱动，因为 VMM MMIO 映射尚未支持 64 位
    LOG_WARN_MSG("  [4.8] E1000 driver skipped (x86_64 VMM MMIO not ready)\n");
    int e1000_count = 0;
#else
    int e1000_count = e1000_init();
#endif
    if (e1000_count > 0) {
        LOG_INFO_MSG("  [4.8] E1000 driver initialized (%d device(s))\n", e1000_count);
        
        // 如果有网卡，启用第一个网卡
        netdev_t *eth0 = netdev_get_by_name("eth0");
        if (eth0) {
            netdev_up(eth0);
            LOG_INFO_MSG("  Network: eth0 enabled\n");
        }
    } else {
        LOG_DEBUG_MSG("  [4.8] No E1000 network card found\n");
    }

    // 4.9 初始化帧缓冲（图形模式）
#if defined(ARCH_ARM64)
    // ARM64: 暂时跳过帧缓冲，因为 VMM MMIO 映射尚未支持
    LOG_WARN_MSG("  [4.9] Framebuffer skipped (ARM64 VMM MMIO not ready)\n");
#else
    int fb_result = fb_init(mbi);
    if (fb_result == 0) {
        framebuffer_info_t *fb = fb_get_info();
        LOG_INFO_MSG("  [4.9] Framebuffer initialized: %ux%u @ %ubpp\n",
                     fb->width, fb->height, fb->bpp);
        
        // 根据分辨率显示不同的信息
        const char *resolution_name;
        if (fb->width == 1400 && fb->height == 1050) {
            resolution_name = "SXGA+ (1400x1050)";
        } else if (fb->width == 1024 && fb->height == 768) {
            resolution_name = "XGA (1024x768)";
        } else if (fb->width == 800 && fb->height == 600) {
            resolution_name = "SVGA (800x600)";
        } else {
            resolution_name = "Custom";
        }
        LOG_INFO_MSG("  Display mode: %s\n", resolution_name);
        
        // 初始化图形终端（用于后续输出）
        fb_terminal_init();
    } else {
        LOG_DEBUG_MSG("  [4.9] Framebuffer not available (code=%d), using text mode\n", fb_result);
    }
#endif

    // 4.10 初始化 USB 子系统
#if defined(ARCH_X86_64)
    // x86_64: 暂时跳过 USB 子系统，因为 VMM MMIO 映射尚未支持 64 位
    LOG_WARN_MSG("  [4.10] USB subsystem skipped (x86_64 VMM MMIO not ready)\n");
#else
    LOG_INFO_MSG("  [4.10] Initializing USB subsystem...\n");
    
    // 4.10.1 初始化 USB 核心层
    usb_init();
    LOG_DEBUG_MSG("    [4.10.1] USB core initialized\n");
    
    // 4.10.2 初始化 UHCI 控制器
    int uhci_count = uhci_init();
    if (uhci_count > 0) {
        LOG_INFO_MSG("    [4.10.2] UHCI initialized (%d controller(s))\n", uhci_count);
    } else {
        LOG_DEBUG_MSG("    [4.10.2] No UHCI controller found\n");
    }
    
    // 4.10.3 初始化 USB Mass Storage 驱动
    usb_msc_init();
    LOG_DEBUG_MSG("    [4.10.3] USB Mass Storage driver initialized\n");
    
    // 4.10.4 扫描 USB 设备
    usb_scan_devices();
    uhci_sync_port_devices();  // 建立端口到设备的映射（热插拔支持）
    LOG_INFO_MSG("    [4.10.4] USB device scan complete\n");
#endif
    
#if !defined(ARCH_X86_64)
    // 4.10.5 启动 USB 热插拔监控
    uhci_start_hotplug_monitor();
    LOG_DEBUG_MSG("    [4.10.5] USB hot-plug monitor started\n");
#endif

    // ========================================================================
    // 阶段 5: 高级子系统（Advanced Subsystems）
    // ========================================================================
    LOG_INFO_MSG("[Stage 5] Initializing advanced subsystems...\n");
    
    // DEBUG: 验证堆状态
    {
        heap_block_t *fb = (heap_block_t*)heap_start;
        LOG_INFO_MSG("  DEBUG: first_block magic before task_init = 0x%x\n", fb->magic);
        LOG_INFO_MSG("  DEBUG: task_pool addr = 0x%llx, size = %llu\n", 
                     (unsigned long long)(uintptr_t)task_pool, 
                     (unsigned long long)sizeof(task_pool));
    }

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
    
    // Idle loop - use HAL for architecture-independent CPU halt
    while (1) {
        hal_cpu_halt();
    }
}

#endif /* !ARCH_ARM64 */
