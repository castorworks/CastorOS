/**
 * @file dtb.h
 * @brief 设备树 (Device Tree Blob) 解析
 *
 * arm64 上固件（这里是 QEMU）交给内核一份设备树：一棵描述硬件的树，每个节点有若干
 * 属性。dtb_parse() 走一遍这棵树，把内核关心的东西整理出来：物理内存的范围、
 * 中断控制器 (GIC)、ARM 通用定时器、串口，以及所有带 compatible 属性的设备的列表。
 */

#ifndef _ARM64_DTB_H_
#define _ARM64_DTB_H_

#include <types.h>

#define DTB_MAX_MEMORY_REGIONS  8
#define DTB_MAX_DEVICES         64
#define DTB_MAX_NAME_LEN        32
#define DTB_MAX_COMPATIBLE_LEN  96
#define DTB_MAX_CPUS            8

/** PSCI（启动其余 CPU 用的固件接口）怎么调用 */
#define DTB_PSCI_NONE           0       /**< 设备树里没有 PSCI */
#define DTB_PSCI_HVC            1       /**< 用 hvc 指令 */
#define DTB_PSCI_SMC            2       /**< 用 smc 指令 */

/** 一段物理内存 */
typedef struct {
    uint64_t base;
    uint64_t size;
} dtb_memory_region_t;

/** 中断控制器 */
typedef struct {
    bool     found;
    uint32_t version;               /**< 2 或 3 */
    uint64_t distributor_base;      /**< GICD */
    uint64_t cpu_interface_base;    /**< GICC（GICv2） */
    uint64_t redistributor_base;    /**< GICR（GICv3） */
} dtb_gic_info_t;

/** 一个设备节点 */
typedef struct {
    char     name[DTB_MAX_NAME_LEN];        /**< 节点名，含 @ 后面的地址，如 "virtio_mmio@a000000" */
    /** compatible 列表：一个接一个的字符串，各以 NUL 结尾，从最具体的型号到最一般的。
     *  放不下时在一项的边界上截断。第一项就是 compatible 本身当字符串读 */
    char     compatible[DTB_MAX_COMPATIBLE_LEN];
    uint32_t compatible_len;                /**< 列表占的字节数（含各项结尾的 NUL） */
    uint64_t base_addr;                     /**< reg 的第一项；没有 reg 时为 0 */
    uint64_t size;
    uint32_t irq;                           /**< interrupts 的第一项对应的 GIC 中断号 */
    bool     has_irq;
} dtb_device_t;

/** 解析结果 */
typedef struct {
    uint32_t num_memory_regions;
    dtb_memory_region_t memory[DTB_MAX_MEMORY_REGIONS];
    uint64_t total_memory;

    dtb_gic_info_t gic;

    bool     timer_found;
    uint32_t timer_irq;             /**< 非安全物理定时器的 GIC 中断号 */

    bool     uart_found;            /**< 第一个 PL011 */
    uint64_t uart_base;
    uint32_t uart_irq;

    uint32_t num_cpus;              /**< device_type = "cpu" 的节点个数 */
    uint64_t cpu_mpidr[DTB_MAX_CPUS];   /**< 各个 CPU 的 reg：它的 MPIDR 亲和值，PSCI 用它指明启动哪一个 */
    uint32_t psci_method;           /**< DTB_PSCI_* */

    uint32_t num_devices;           /**< 所有带 compatible 的节点（包括上面单独记了一份的那几个） */
    dtb_device_t devices[DTB_MAX_DEVICES];
} dtb_info_t;

/**
 * @brief 找到设备树
 * @param hint 固件给的地址（启动时 x0 的值），可以是 NULL
 * @return 设备树的地址；找不到返回 NULL
 */
const void *dtb_find(const void *hint);

/**
 * @brief 解析设备树
 * @return 解析结果（内核里只有一份，下次调用会覆盖）；不是合法的设备树返回 NULL
 */
const dtb_info_t *dtb_parse(const void *dtb);

/** 最近一次成功解析的结果；还没有解析过返回 NULL */
const dtb_info_t *dtb_get_info(void);

/** 设备的 compatible 列表里有没有 model 这一项 */
bool dtb_device_is(const dtb_device_t *dev, const char *model);

#endif /* _ARM64_DTB_H_ */
