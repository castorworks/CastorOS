/**
 * @file dtb.cpp
 * @brief 设备树解析（接口说明见 dtb.h）
 *
 * 设备树的二进制格式 (FDT)：一个头，后面是几块数据。所有的数都是大端的。
 *   - 结构块：一串 32 位标记。BEGIN_NODE 后面跟节点名，PROP 后面跟属性值的长度、
 *     属性名在字符串块里的偏移和属性值，END_NODE 结束一个节点，END 结束全部。
 *     名字和属性值都补齐到 4 字节。一个节点的属性都排在它的子节点之前。
 *   - 字符串块：属性名。
 *
 * 属性的含义要到节点结束时才能判断：一个节点是什么设备由 compatible 决定，而它的
 * reg、interrupts 可能排在 compatible 前面。所以解析时先把每个打开着的节点的这几个
 * 属性记下来（一个按深度排的栈），等节点结束再归类。
 */

#include <types.h>
#include <lib/klog.h>
#include <lib/string.h>
#include "../include/dtb.h"

#define FDT_MAGIC           0xD00DFEED
#define FDT_VERSION_MIN     16          /* last_comp_version：我们按第 16 版的格式读 */

#define FDT_BEGIN_NODE      1
#define FDT_END_NODE        2
#define FDT_PROP            3
#define FDT_NOP             4
#define FDT_END             9

/** 节点嵌套最深记到这一层；更深的节点照样走过，只是不归类 */
#define DTB_MAX_DEPTH       16

/** 父节点没有声明时的默认值（设备树规范） */
#define DEFAULT_ADDRESS_CELLS   2
#define DEFAULT_SIZE_CELLS      1

/** GIC 的中断说明符是 3 个单元：类型、编号、触发方式 */
#define GIC_INTERRUPT_CELLS     3
#define GIC_SPI                 0       /* 共享外设中断：中断号 = 编号 + 32 */
#define GIC_PPI                 1       /* 每个 CPU 私有的外设中断：中断号 = 编号 + 16 */

struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

/** 一个属性值：指向设备树里的数据 */
struct prop {
    const uint8_t *data;
    uint32_t len;
};

/** 一个打开着的节点：归类要用到的属性，以及它为子节点声明的单元数 */
struct node {
    const char *name;
    struct prop compatible;
    struct prop device_type;
    struct prop reg;
    struct prop interrupts;
    struct prop method;         /* PSCI 节点：怎么调用固件 */
    uint32_t address_cells;     /* 子节点的 reg 里，地址占几个 32 位单元 */
    uint32_t size_cells;        /* 长度占几个 */
};

static dtb_info_t g_info;
static bool g_valid = false;

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/** 读 cells 个单元组成的一个数。最多取低 64 位 */
static uint64_t read_cells(const uint8_t *p, uint32_t cells) {
    uint64_t value = 0;
    for (uint32_t i = 0; i < cells; i++) {
        value = (value << 32) | be32(p + 4 * i);
    }
    return value;
}

/** 字符串属性的值是不是 str */
static bool prop_is(const struct prop *p, const char *str) {
    size_t n = strlen(str);
    return p->len == n + 1 && memcmp(p->data, str, n + 1) == 0;
}

/** compatible 是一串以 NUL 分隔的字符串，从最具体到最一般：其中有没有 str */
static bool compatible_with(const struct prop *compatible, const char *str) {
    size_t n = strlen(str);
    for (uint32_t i = 0; i < compatible->len; ) {
        const char *item = (const char *)compatible->data + i;
        size_t item_len = strnlen(item, compatible->len - i);
        if (item_len == n && memcmp(item, str, n) == 0) {
            return true;
        }
        i += item_len + 1;
    }
    return false;
}

/** 节点名去掉 @ 后面的地址之后是不是 base */
static bool name_is(const char *name, const char *base) {
    size_t n = strlen(base);
    return strncmp(name, base, n) == 0 && (name[n] == '\0' || name[n] == '@');
}

/**
 * 取 reg 属性里的第 index 项（地址、长度）。单元数由父节点声明。
 * @return 有这一项返回 true
 */
static bool reg_entry(const struct node *n, const struct node *parent, uint32_t index,
                      uint64_t *base, uint64_t *size) {
    uint32_t entry = 4 * (parent->address_cells + parent->size_cells);
    if (entry == 0 || n->reg.len < (uint64_t)entry * (index + 1)) {
        return false;
    }
    const uint8_t *p = n->reg.data + entry * index;
    *base = read_cells(p, parent->address_cells);
    *size = read_cells(p + 4 * parent->address_cells, parent->size_cells);
    return true;
}

/**
 * 取 interrupts 属性里的第 index 项，换算成 GIC 的中断号。
 * 假定中断父节点是 GIC（QEMU virt 上都是）。
 */
static bool interrupt_entry(const struct node *n, uint32_t index, uint32_t *irq) {
    uint32_t entry = 4 * GIC_INTERRUPT_CELLS;
    if (n->interrupts.len < (uint64_t)entry * (index + 1)) {
        return false;
    }
    const uint8_t *p = n->interrupts.data + entry * index;
    uint32_t type = be32(p);
    uint32_t number = be32(p + 4);
    if (type != GIC_SPI && type != GIC_PPI) {
        return false;
    }
    *irq = number + (type == GIC_PPI ? 16 : 32);
    return true;
}

/** 把 src 里最多 src_max 字节的字符串抄进 dest（DTB_MAX_NAME_LEN 字节），太长就截断 */
static void copy_name(char *dest, const char *src, size_t src_max) {
    size_t i = 0;
    for (; i < DTB_MAX_NAME_LEN - 1 && i < src_max && src[i]; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

/** 一个节点的属性都读完了：看它是什么，把内核关心的记下来 */
static void classify_node(const struct node *n, const struct node *parent) {
    uint64_t base = 0, size = 0;

    // 内存：device_type = "memory"（老的设备树只靠节点名）。reg 可以有多项
    if (prop_is(&n->device_type, "memory") || (n->device_type.len == 0 && name_is(n->name, "memory"))) {
        for (uint32_t i = 0; reg_entry(n, parent, i, &base, &size); i++) {
            if (size == 0 || g_info.num_memory_regions == DTB_MAX_MEMORY_REGIONS) {
                continue;
            }
            g_info.memory[g_info.num_memory_regions].base = base;
            g_info.memory[g_info.num_memory_regions].size = size;
            g_info.num_memory_regions++;
            g_info.total_memory += size;
        }
        return;
    }
    // CPU：/cpus 下 device_type = "cpu" 的节点，reg 是它的 MPIDR 亲和值（没有长度）
    if (prop_is(&n->device_type, "cpu")) {
        uint32_t cells = parent->address_cells;
        if (g_info.num_cpus < DTB_MAX_CPUS && cells >= 1 && n->reg.len >= 4 * cells) {
            g_info.cpu_mpidr[g_info.num_cpus++] = read_cells(n->reg.data, cells);
        }
    }
    if (n->compatible.len == 0) {
        return;         // 没有 compatible 的节点不是设备（/chosen、/cpus、/aliases 之类）
    }

    // PSCI：启动其余 CPU 的固件接口。method 说明用哪条指令进固件
    if (compatible_with(&n->compatible, "arm,psci-0.2") || compatible_with(&n->compatible, "arm,psci-1.0")) {
        g_info.psci_method = prop_is(&n->method, "hvc") ? DTB_PSCI_HVC
                           : prop_is(&n->method, "smc") ? DTB_PSCI_SMC : DTB_PSCI_NONE;
    }

    // 每个设备都进设备列表（驱动按型号来查）
    if (g_info.num_devices < DTB_MAX_DEVICES) {
        dtb_device_t *dev = &g_info.devices[g_info.num_devices++];
        copy_name(dev->name, n->name, DTB_MAX_NAME_LEN);
        // 整个 compatible 列表都留着（驱动可能按其中任何一项来找）；放不下就只留前面完整的几项
        for (uint32_t i = 0; i < n->compatible.len; ) {
            const char *item = (const char *)n->compatible.data + i;
            size_t item_len = strnlen(item, n->compatible.len - i);
            if (dev->compatible_len + item_len + 1 > DTB_MAX_COMPATIBLE_LEN) {
                break;
            }
            memcpy(dev->compatible + dev->compatible_len, item, item_len);
            dev->compatible_len += (uint32_t)item_len + 1;      // 结尾的 NUL 已经在那里（整个结构清过零）
            i += item_len + 1;
        }
        if (reg_entry(n, parent, 0, &base, &size)) {
            dev->base_addr = base;
            dev->size = size;
        }
        dev->has_irq = interrupt_entry(n, 0, &dev->irq);
    }

    // 内核自己要用的几样另外记一份

    // 中断控制器。reg 的第一项是 distributor，第二项是 CPU interface (v2) 或 redistributor (v3)
    bool gic_v3 = compatible_with(&n->compatible, "arm,gic-v3");
    bool gic_v2 = compatible_with(&n->compatible, "arm,cortex-a15-gic") ||
                  compatible_with(&n->compatible, "arm,gic-400");
    if ((gic_v3 || gic_v2) && !g_info.gic.found) {
        g_info.gic.found = true;
        g_info.gic.version = gic_v3 ? 3 : 2;
        if (reg_entry(n, parent, 0, &base, &size)) {
            g_info.gic.distributor_base = base;
        }
        if (reg_entry(n, parent, 1, &base, &size)) {
            if (gic_v3) {
                g_info.gic.redistributor_base = base;
            } else {
                g_info.gic.cpu_interface_base = base;
            }
        }
    }

    // ARM 通用定时器。interrupts 依次是：安全物理、非安全物理、虚拟、hypervisor
    if (compatible_with(&n->compatible, "arm,armv8-timer") ||
        compatible_with(&n->compatible, "arm,armv7-timer")) {
        g_info.timer_found = interrupt_entry(n, 1, &g_info.timer_irq);
    }

    // 串口：第一个 PL011
    if (compatible_with(&n->compatible, "arm,pl011") && !g_info.uart_found) {
        g_info.uart_found = reg_entry(n, parent, 0, &g_info.uart_base, &size);
        interrupt_entry(n, 0, &g_info.uart_irq);
    }
}

/**
 * 走一遍结构块。
 * @return 格式正确（走到了 END 标记）返回 true
 */
static bool walk(const uint8_t *p, const uint8_t *end, const char *strings, uint32_t strings_size) {
    // stack[d] 是深度为 d 的那个打开着的节点；stack[0] 是根节点的虚拟父节点，只提供默认的单元数
    static struct node stack[DTB_MAX_DEPTH + 1];
    memset(stack, 0, sizeof(stack));
    stack[0].address_cells = DEFAULT_ADDRESS_CELLS;
    stack[0].size_cells = DEFAULT_SIZE_CELLS;
    int depth = 0;

    while (p + 4 <= end) {
        uint32_t token = be32(p);
        p += 4;

        if (token == FDT_BEGIN_NODE) {
            const char *name = (const char *)p;
            size_t name_len = strnlen(name, (size_t)(end - p));
            if (p + name_len >= end) {
                return false;       // 名字没有结束
            }
            p += (name_len + 1 + 3) & ~(size_t)3;
            depth++;
            if (depth <= DTB_MAX_DEPTH) {
                struct node *n = &stack[depth];
                memset(n, 0, sizeof(*n));
                n->name = name;
                // 单元数不从祖先继承：这个节点自己不声明，它的子节点就用默认值
                n->address_cells = DEFAULT_ADDRESS_CELLS;
                n->size_cells = DEFAULT_SIZE_CELLS;
            }
        } else if (token == FDT_END_NODE) {
            if (depth == 0) {
                return false;
            }
            if (depth <= DTB_MAX_DEPTH) {
                classify_node(&stack[depth], &stack[depth - 1]);
            }
            depth--;
        } else if (token == FDT_PROP) {
            if (p + 8 > end) {
                return false;
            }
            uint32_t len = be32(p);
            uint32_t name_off = be32(p + 4);
            p += 8;
            if (len > (size_t)(end - p) || name_off >= strings_size || depth == 0) {
                return false;
            }
            struct prop value = { p, len };
            p += (len + 3) & ~(uint32_t)3;

            if (depth > DTB_MAX_DEPTH) {
                continue;
            }
            struct node *n = &stack[depth];
            const char *name = strings + name_off;
            if (strcmp(name, "compatible") == 0) {
                n->compatible = value;
            } else if (strcmp(name, "device_type") == 0) {
                n->device_type = value;
            } else if (strcmp(name, "reg") == 0) {
                n->reg = value;
            } else if (strcmp(name, "interrupts") == 0) {
                n->interrupts = value;
            } else if (strcmp(name, "method") == 0) {
                n->method = value;
            } else if (strcmp(name, "#address-cells") == 0 && len == 4) {
                n->address_cells = be32(value.data);
            } else if (strcmp(name, "#size-cells") == 0 && len == 4) {
                n->size_cells = be32(value.data);
            }
        } else if (token == FDT_END) {
            return depth == 0;
        } else if (token != FDT_NOP) {
            return false;
        }
    }
    return false;
}

static bool is_dtb(const void *addr) {
    return addr != NULL && be32((const uint8_t *)addr) == FDT_MAGIC;
}

const void *dtb_find(const void *hint) {
    // QEMU 用 -kernel 加载 ELF 映像时不在 x0 里给地址；这时设备树在内存的开头，
    // 或者内存末尾附近的几个固定位置之一
    static const uint64_t candidates[] = {
        0x40000000, 0x44000000, 0x47E00000, 0x48000000, 0x4FE00000, 0x50000000, 0x80000000,
    };
    if (is_dtb(hint)) {
        return hint;
    }
    for (uint32_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (is_dtb((const void *)candidates[i])) {
            return (const void *)candidates[i];
        }
    }
    return NULL;
}

const dtb_info_t *dtb_parse(const void *dtb) {
    g_valid = false;
    memset(&g_info, 0, sizeof(g_info));
    if (!is_dtb(dtb)) {
        return NULL;
    }

    const uint8_t *base = (const uint8_t *)dtb;
    const struct fdt_header *header = (const struct fdt_header *)dtb;
    uint32_t total = be32((const uint8_t *)&header->totalsize);
    uint32_t struct_off = be32((const uint8_t *)&header->off_dt_struct);
    uint32_t struct_size = be32((const uint8_t *)&header->size_dt_struct);
    uint32_t strings_off = be32((const uint8_t *)&header->off_dt_strings);
    uint32_t strings_size = be32((const uint8_t *)&header->size_dt_strings);

    // 我们按第 16 版的格式读：设备树必须声明自己和它兼容；各块必须在设备树之内
    if (be32((const uint8_t *)&header->last_comp_version) > FDT_VERSION_MIN ||
        be32((const uint8_t *)&header->version) < FDT_VERSION_MIN ||
        struct_off > total || struct_size > total - struct_off ||
        strings_off > total || strings_size > total - strings_off) {
        LOG_ERROR_MSG("DTB: unsupported version or corrupt header\n");
        return NULL;
    }

    if (!walk(base + struct_off, base + struct_off + struct_size,
              (const char *)base + strings_off, strings_size)) {
        LOG_ERROR_MSG("DTB: malformed structure block\n");
        return NULL;
    }

    g_valid = true;
    LOG_INFO_MSG("DTB: %llu MB of memory in %u region(s), GIC v%u, %u device(s)\n",
                 (unsigned long long)(g_info.total_memory >> 20), g_info.num_memory_regions,
                 g_info.gic.version, g_info.num_devices);
    return &g_info;
}

bool dtb_device_is(const dtb_device_t *dev, const char *model) {
    struct prop list = { (const uint8_t *)dev->compatible, dev->compatible_len };
    return compatible_with(&list, model);
}

const dtb_info_t *dtb_get_info(void) {
    return g_valid ? &g_info : NULL;
}
