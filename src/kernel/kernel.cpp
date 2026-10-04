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
#include <boot/boot_info.h>
#include <arch/arm64/arch_types.h>
#else
#include <kernel/multiboot.h>
#endif

#ifdef KTEST
#include <tests/test_runner.h>
#endif

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

    boot_info_t *boot_info = boot_info_init_dtb(dtb_addr);
    if (!boot_info) {
        kprintf("PANIC: no usable boot info in DTB at 0x%llx\n",
                (unsigned long long)(uintptr_t)dtb_addr);
        while (1) {
            hal::Cpu::halt();
        }
    }

    hal::Cpu::init();
    hal::Interrupt::init();   // 异常向量 + GIC
    syscall_init();

    mm::Pmm::init_boot_info(boot_info);
    mm::Vmm::init();

    // 堆放在 PMM 数据结构之后，不超过物理内存末尾
    uintptr_t heap_start = PAGE_ALIGN_UP(mm::Pmm::get_data_end_virt());
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

extern "C" void kernel_main(multiboot_info_t *mbi);
void kernel_main(multiboot_info_t *mbi) {
    cxx_global_ctors_init();  // 运行 C++ 全局构造函数（必须最先执行）

    drivers::Serial::init();  // COM1
    print_banner();

    hal::Cpu::init();         // GDT + TSS
    hal::Interrupt::init();   // IDT + PIC/APIC
    syscall_init();

    mm::Pmm::init(mbi);
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
