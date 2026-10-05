// ============================================================================
// kernel.cpp - 内核主函数
// ============================================================================
//
// 内核只做四件事：CPU/中断、内存管理、任务调度、系统调用。
// 初始化完成后加载内嵌的 init 程序，其余功能都在用户态。
// 体系结构相关的部分通过 HAL 完成；x86 由 Multiboot 提供启动信息，
// arm64 由 DTB 提供。
// ============================================================================

#include <drivers/serial.h>
#include <drivers/timer.h>

#include <kernel/version.h>
#include <kernel/task.h>
#include <kernel/syscall.h>
#include <kernel/loader.h>

#include <lib/kprintf.h>
#include <lib/klog.h>
#include <lib/cxxrt.h>

#include <hal/hal.h>

#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/heap.h>

#if defined(ARCH_ARM64)
#include <arch/arm64/arch_types.h>
#include <dtb.h>
#else
#include <kernel/multiboot.h>
#include <kernel/panic.h>
#endif

#ifdef KTEST
#include <tests/test_runner.h>
#endif

/** 固件报告的内存区域最多记这么多个 */
#define PMM_BOOT_REGIONS 32

static void print_banner(void) {
    kprintf("\n================================================================================\n");
    kprintf("CastorOS v%s (%s)\n", KERNEL_VERSION, hal_arch_name());
    kprintf("Compiled on: %s %s\n", __DATE__, __TIME__);
    kprintf("================================================================================\n");
}

/**
 * 内存管理就绪之后的公共启动流程：调度器、（可选的）内核测试、init
 */
static void kernel_start(void) __attribute__((noreturn));
static void kernel_start(void) {
    kernel::Scheduler::init();
    LOG_INFO_MSG("Scheduler initialized\n");

    hal::Interrupt::enable();

#ifdef KTEST
    LOG_INFO_MSG("Running test suite...\n");
    run_all_tests();
    kprintf("\n");
#endif

    if (!load_init()) {
        LOG_WARN_MSG("No init process; kernel idles\n");
    }

    LOG_INFO_MSG("Kernel entering scheduler...\n");
    kernel::Scheduler::schedule();

    while (1) {
        hal::Cpu::halt();
    }
}

#if defined(ARCH_ARM64)

extern "C" void kernel_main(void *dtb_addr);
void kernel_main(void *dtb_addr) {
    cxx_global_ctors_init();  // 运行 C++ 全局构造函数（必须最先执行）

    drivers::Serial::init();  // PL011
    print_banner();

    // 硬件的描述（包括物理内存的范围）来自固件给的设备树
    const dtb_info_t *dtb = dtb_parse(dtb_find(dtb_addr));
    if (!dtb || dtb->num_memory_regions == 0) {
        kprintf("PANIC: no usable device tree (x0 was 0x%llx)\n",
                (unsigned long long)(uintptr_t)dtb_addr);
        while (1) {
            hal::Cpu::halt();
        }
    }
    static mm::MemRegion regions[DTB_MAX_MEMORY_REGIONS];
    uint32_t region_count = dtb->num_memory_regions;
    for (uint32_t i = 0; i < region_count; i++) {
        regions[i].start = (paddr_t)dtb->memory[i].base;
        regions[i].end = (paddr_t)(dtb->memory[i].base + dtb->memory[i].size);
    }

    hal::Cpu::init();
    hal::Interrupt::init();   // 异常向量 + GIC
    syscall_init();

    mm::Pmm::init(regions, region_count);
    mm::Vmm::init();

    // 堆放在 PMM 数据结构之后，不超过物理内存末尾
    uintptr_t heap_start = PAGE_ALIGN_UP(mm::Pmm::get_bitmap_end());
    mm::PmmInfo pmm_info = mm::Pmm::get_info();
    uintptr_t max_heap_virt = PHYS_TO_VIRT((uint64_t)pmm_info.total_frames * PAGE_SIZE);
    uint64_t available_space = max_heap_virt - heap_start;
    uint32_t heap_size = (uint32_t)ARM64_HEAP_INIT_SIZE;
    if (available_space < heap_size) {
        heap_size = (uint32_t)(available_space / 2);
    }
    mm::Heap::init(heap_start, heap_size);
    // 通知 PMM 堆的虚拟地址范围，防止分配会与堆重叠的物理帧
    mm::Pmm::set_heap_reserved_range(heap_start, heap_start + heap_size);
    LOG_INFO_MSG("Memory management initialized\n");

    hal::Timer::init(100, kernel::Scheduler::timer_tick);  // ARM Generic Timer, 100 Hz

    kernel_start();
}

#else /* i686, x86_64 */

/**
 * 把堆的起始地址移到所有已分配物理帧之后
 *
 * 堆的虚拟区间取自直接映射窗口，[heap_start, heap_start + heap_size) 对应的
 * 物理帧必须全部空闲：VMM 初始化时已经从 PMM 数据结构之后分配了页表帧，
 * 如果堆区间盖住它们，i686 上堆扩展会把这些帧的直接映射地址改映射到别的帧
 * （之后经 PHYS_TO_VIRT 访问页表就写进了堆里），x86_64 上堆则直接与它们重叠。
 */
static uintptr_t heap_start_after_used_frames(uintptr_t heap_start, uint32_t heap_size) {
    uintptr_t start = PAGE_ALIGN_UP(heap_start);
    for (uintptr_t addr = start; addr - start < heap_size; addr += PAGE_SIZE) {
        if (mm::Pmm::frame_get_refcount((paddr_t)VIRT_TO_PHYS(addr)) != 0) {
            start = addr + PAGE_SIZE;
        }
    }
    return start;
}

/**
 * 从 Multiboot 内存映射里取出可用的物理内存区域
 */
static uint32_t multiboot_memory_regions(const multiboot_info_t *mbi, mm::MemRegion *regions, uint32_t max) {
    if (!(mbi->flags & MULTIBOOT_INFO_MEM_MAP)) {
        return 0;
    }
    uint32_t count = 0;
    uintptr_t entry = PHYS_TO_VIRT(mbi->mmap_addr);
    uintptr_t end = entry + mbi->mmap_length;
    while (entry < end && count < max) {
        const multiboot_memory_map_t *mmap = (const multiboot_memory_map_t *)entry;
        if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
            uint64_t region_end = mmap->addr + mmap->len;
#if defined(ARCH_I686)
            // 内核的地址空间只有 2GB，直接映射区放不下更多的物理内存
            const uint64_t limit = 0x80000000ULL;
            if (region_end > limit) {
                region_end = limit;
            }
#endif
            if (region_end > mmap->addr) {
                regions[count].start = (paddr_t)mmap->addr;
                regions[count].end = (paddr_t)region_end;
                count++;
            }
        }
        entry += mmap->size + 4;        // size 字段不包括它自己
    }
    return count;
}

extern "C" void kernel_main(multiboot_info_t *mbi);
void kernel_main(multiboot_info_t *mbi) {
    cxx_global_ctors_init();  // 运行 C++ 全局构造函数（必须最先执行）

    drivers::Serial::init();  // COM1
    print_banner();

    hal::Cpu::init();         // GDT + TSS
    hal::Interrupt::init();   // IDT + PIC/APIC
    syscall_init();

    static mm::MemRegion regions[PMM_BOOT_REGIONS];
    uint32_t region_count = multiboot_memory_regions(mbi, regions, PMM_BOOT_REGIONS);
    if (region_count == 0) {
        PANIC("No memory map from the boot loader");
    }
    mm::Pmm::init(regions, region_count);
    mm::Vmm::init();

    // 堆起始地址：PMM 位图之后，并且在 VMM 初始化已分配的页表帧之后
    uint32_t heap_size = 32 * 1024 * 1024;
    uintptr_t heap_start = heap_start_after_used_frames(mm::Pmm::get_bitmap_end(), heap_size);
    mm::Heap::init(heap_start, heap_size);
    // 通知 PMM 堆的虚拟地址范围，防止分配会与堆重叠的物理帧
    mm::Pmm::set_heap_reserved_range(heap_start, heap_start + heap_size);
    LOG_INFO_MSG("Memory management initialized\n");

    drivers::Timer::init(100);  // PIT, 100 Hz

    kernel_start();
}

#endif
