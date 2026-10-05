/**
 * @file pmm.h
 * @brief 物理内存管理器接口（重构版）
 * 
 * 使用位图管理物理页帧的分配和释放。
 * 支持 64-bit 物理地址，兼容 i686、x86_64 和 ARM64 架构。
 */

#ifndef _MM_PMM_H_
#define _MM_PMM_H_

#include <types.h>
#include <mm/mm_types.h>
#include <kernel/multiboot.h>

/*============================================================================
 * 内存区域定义
 *============================================================================*/


struct boot_info;

/** @brief 大页大小 (2MB) */
#define HUGE_PAGE_SIZE          (2 * 1024 * 1024)


namespace mm {

/**
 * @brief 物理内存信息结构
 * 
 * 使用 pfn_t 类型支持大于 4GB 的物理内存
 */
struct PmmInfo {
    pfn_t total_frames;     ///< 总页帧数
    pfn_t free_frames;      ///< 空闲页帧数
    pfn_t used_frames;      ///< 已使用页帧数
    pfn_t reserved_frames;  ///< 保留页帧数（内核+位图）
    pfn_t kernel_frames;    ///< 内核占用页帧数
    pfn_t bitmap_frames;    ///< 位图占用页帧数
};

/**
 * @brief 物理内存管理器（位图页帧分配器，带引用计数）
 */
class Pmm {
public:
    /*============================================================================
     * PMM 核心接口
     *============================================================================*/

    /**
     * @brief 初始化物理内存管理器 (Multiboot)
     * @param mbi Multiboot信息结构指针（i686/x86_64）
     * 
     * 解析内存映射，初始化位图，标记已使用和空闲的页帧
     */
    static void init(multiboot_info_t *mbi);

    /**
     * @brief 初始化物理内存管理器 (boot_info_t)
     * @param boot_info 标准化引导信息结构指针（ARM64 DTB 或其他来源）
     * 
     * 使用架构无关的 boot_info_t 结构初始化 PMM。
     * 适用于 ARM64 (DTB) 和其他非 Multiboot 引导方式。
     */
    static void init_boot_info(struct boot_info *boot_info);

    /**
     * @brief 分配一个物理页帧
     * @return 成功返回页帧的物理地址，失败返回 PADDR_INVALID
     * 
     * 分配后会清零页帧内容。返回的地址保证是页对齐的。
     */
    static paddr_t alloc_frame();

    /**
     * @brief 释放一个物理页帧
     * @param frame 页帧的物理地址
     * 
     * 地址必须是页对齐的。
     * COW 支持：如果帧的引用计数 > 1，只递减计数，不实际释放。
     */
    static void free_frame(paddr_t frame);

    /*============================================================================
     * 大页分配接口（2MB 对齐）
     *============================================================================*/

    /**
     * @brief 检查物理地址是否 2MB 对齐
     * @param addr 物理地址
     * @return true 如果 2MB 对齐
     */
    static inline bool is_huge_page_aligned(paddr_t addr) {
        return (addr & (HUGE_PAGE_SIZE - 1)) == 0;
    }

    /*============================================================================
     * 引用计数接口（COW 支持）
     *============================================================================*/

    /**
     * @brief 增加物理页帧的引用计数
     * @param frame 页帧的物理地址
     * @return 新的引用计数值
     */
    static uint32_t frame_ref_inc(paddr_t frame);

    /**
     * @brief 分配 count 个物理上连续的页帧（清零，每帧引用计数为 1）
     *
     * 给需要把物理地址交给设备的驱动用（DMA）。逐帧用 free_frame 释放。
     * @return 起始物理地址，失败返回 PADDR_INVALID
     */
    static paddr_t alloc_contiguous(size_t count);

    /**
     * @brief 又多了一个映射指向 frame：是 PMM 管理的帧就增加引用计数
     *
     * 与 frame_ref_inc 的区别：frame 可以是物理内存范围之外的设备内存，
     * 那样的帧不计数（也永远不会被释放），这里直接忽略。
     */
    static void frame_ref_share(paddr_t frame);

    /**
     * @brief 把设备内存帧钉住，使它可以像普通帧一样被映射/取消映射而不会被回收
     *
     * 调用者负责确认 frame 位于设备地址区。物理内存范围之外的帧不归 PMM 管，
     * 直接接受；范围之内的必须是启动时就被保留的帧，这里给它加一份永不归还的
     * 引用，之后各个映射的引用计数增减都不会把它释放。
     *
     * @return 可以映射返回 true；frame 是空闲的可分配内存返回 false
     */
    static bool pin_device_frame(paddr_t frame);

    /**
     * @brief [start, end) 里有没有固件报告为可用内存的部分
     *
     * 只在用 Multiboot 内存映射初始化的架构（x86）上有内容。可用内存区域之间的空洞
     * （640K-1M、4GB 以下的 PCI 空洞）是设备内存，即使它们的地址比内存的最高地址低。
     */
    static bool overlaps_ram(paddr_t start, paddr_t end);

    /**
     * @brief 减少物理页帧的引用计数
     * @param frame 页帧的物理地址
     * @return 新的引用计数值
     */
    static uint32_t frame_ref_dec(paddr_t frame);

    /**
     * @brief 获取物理页帧的引用计数
     * @param frame 页帧的物理地址
     * @return 引用计数值
     */
    static uint32_t frame_get_refcount(paddr_t frame);

    /*============================================================================
     * 信息查询接口
     *============================================================================*/

    /**
     * @brief 获取物理内存信息
     * @return 物理内存信息结构
     */
    static PmmInfo get_info();

    /**
     * @brief 获取 PMM 数据结构结束的虚拟地址
     * @return PMM 数据结构（位图+引用计数表）结束后的虚拟地址
     * 
     * 用于确定堆的起始位置，确保堆不会与 PMM 数据结构重叠。
     */
    static uintptr_t get_data_end_virt();

    /**
     * @brief 打印物理内存使用信息
     */
    static void print_info();

    /**
     * @brief 获取位图结束地址（虚拟地址）
     * @return 位图结束的虚拟地址（页对齐）
     * 
     * 实际返回的是包括引用计数表在内的所有 PMM 数据结构的结束地址。
     */
    static uintptr_t get_bitmap_end();

    /**
     * @brief 设置堆保留区域的物理地址范围
     * @param heap_virt_start 堆虚拟起始地址
     * @param heap_virt_end 堆虚拟结束地址（最大地址）
     * 
     * 将堆虚拟地址范围转换为物理地址范围，并标记这些物理帧不可分配。
     * 这防止了堆扩展时重新映射已分配帧的恒等映射导致的内存损坏。
     */
    static void set_heap_reserved_range(uintptr_t heap_virt_start, uintptr_t heap_virt_end);
};

} // namespace mm

#endif // _MM_PMM_H_
