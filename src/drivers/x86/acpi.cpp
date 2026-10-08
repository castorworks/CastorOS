// ============================================================================
// acpi.cpp - 从 ACPI 的表里读出机器上有哪些 CPU、怎么关机、怎么复位
// ============================================================================
//
// 路径是：RSDP（固件留在低端内存里的一个签名，从它找到）-> RSDT（一张"表的表"）
// -> MADT（签名 "APIC"，里面一项一项列出中断控制器，类型 0 的项是一个 CPU 的 Local APIC）
// -> FADT（签名 "FACP"，电源管理的寄存器在哪个端口上）-> 它指向的 DSDT（一段 AML
//    字节码，里面的 \_S5 说关机时往那个寄存器里写什么）。

#include <drivers/x86/acpi.h>
#include <hal/hal.h>
#include <kernel/interrupt.h>
#include <mm/heap.h>
#include <lib/string.h>

struct rsdp {
    char signature[8];          // "RSD PTR "
    uint8_t checksum;           // 前 20 个字节加起来是 0
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed));

struct sdt_header {             // 每张表开头都是它
    char signature[4];
    uint32_t length;            // 整张表的长度，含这个头
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct madt {
    struct sdt_header header;
    uint32_t local_apic_address;
    uint32_t flags;
    // 后面是一项接一项：类型（1 字节）、长度（1 字节）、内容
} __attribute__((packed));

#define MADT_LOCAL_APIC         0
#define MADT_LOCAL_APIC_ENABLED 0x1

struct madt_local_apic {
    uint8_t type;
    uint8_t length;
    uint8_t processor_id;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed));

struct fadt {
    struct sdt_header header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;              // DSDT 的物理地址
    uint8_t reserved;
    uint8_t preferred_pm_profile;
    uint16_t sci_interrupt;
    uint32_t smi_command;       // 往这个端口写 acpi_enable，固件就把电源管理交给系统
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_request;
    uint8_t pstate_control;
    uint32_t pm1a_event;        // 电源管理的事件寄存器块所在的端口：前一半是状态，后一半是使能
    uint32_t pm1b_event;
    uint32_t pm1a_control;      // 电源管理的控制寄存器（16 位）所在的端口
    uint32_t pm1b_control;      // 有的机器有第二个，同样的值也要写给它；没有就是 0
    uint8_t unused[16];         // 定时器、通用事件等等，这里用不到
    uint8_t pm1_event_length;   // 事件寄存器块有几个字节
    uint8_t unused2[23];
    uint32_t flags;
    struct {                    // 复位寄存器：往这个地址写 reset_value，机器就复位
        uint8_t space;          // 它在哪种地址空间里
        uint8_t bit_width;
        uint8_t bit_offset;
        uint8_t access_size;
        uint64_t address;
    } __attribute__((packed)) reset_register;
    uint8_t reset_value;
} __attribute__((packed));

#define FADT_LENGTH_V1          116     // 最早的 FADT 到 flags 为止，没有复位寄存器
#define FADT_FLAG_NO_POWER_BUTTON (1u << 4)     // flags 里：电源键不是固定事件（要解释 AML 才用得了）
#define FADT_FLAG_RESET         (1u << 10)      // flags 里：有复位寄存器
#define ACPI_SPACE_IO           1       // 地址空间：I/O 端口

// 电源管理控制寄存器（PM1 control）里的位
#define PM1_SCI_ENABLE          (1u << 0)       // 电源管理归系统管（而不是固件）
#define PM1_SLEEP_TYPE_SHIFT    10              // 3 位：进入哪种睡眠状态，值由 DSDT 给
#define PM1_SLEEP_TYPE_MASK     (7u << PM1_SLEEP_TYPE_SHIFT)
#define PM1_SLEEP_ENABLE        (1u << 13)      // 写 1：现在就进入

// AML 的几个操作码
#define AML_NAME                0x08
#define AML_PACKAGE             0x12
#define AML_BYTE_PREFIX         0x0A    // 后面跟一个字节的整数
#define AML_ROOT_PREFIX         '\\'

/**
 * 物理地址 [phys, phys + len) 在内核里的指针；这段内存没有映射进内核时返回 NULL。
 * 表在固件保留的内存里，不属于可用内存，内核的直接映射区不一定盖得到它。
 */
static const uint8_t *phys_ptr(uint64_t phys, size_t len) {
    if (len == 0 || phys + len > (uint64_t)(~(uintptr_t)0 - KERNEL_VIRTUAL_BASE)) {
        return NULL;
    }
    uintptr_t first = PHYS_TO_VIRT((uintptr_t)phys);
    uintptr_t last = first + len - 1;
    if (!hal::Mmu::query(HAL_ADDR_SPACE_CURRENT, first, NULL, NULL) ||
        !hal::Mmu::query(HAL_ADDR_SPACE_CURRENT, last, NULL, NULL)) {
        return NULL;
    }
    return (const uint8_t *)first;
}

static bool sums_to_zero(const uint8_t *p, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum = (uint8_t)(sum + p[i]);
    }
    return sum == 0;
}

/** 在 [start, end) 里每隔 16 字节找一次 RSDP 的签名 */
static const struct rsdp *find_rsdp_in(uint32_t start, uint32_t end) {
    for (uint32_t addr = start; addr + sizeof(struct rsdp) <= end; addr += 16) {
        const uint8_t *p = phys_ptr(addr, sizeof(struct rsdp));
        if (p && memcmp(p, "RSD PTR ", 8) == 0 && sums_to_zero(p, sizeof(struct rsdp))) {
            return (const struct rsdp *)p;
        }
    }
    return NULL;
}

static const struct rsdp *find_rsdp(void) {
    // 两个地方：扩展 BIOS 数据区的开头 1KB（它的段地址记在 0x40E），和 BIOS 的只读区
    const uint8_t *ebda_segment = phys_ptr(0x40E, 2);
    if (ebda_segment) {
        uint32_t ebda = ((uint32_t)ebda_segment[0] | ((uint32_t)ebda_segment[1] << 8)) << 4;
        const struct rsdp *found = ebda ? find_rsdp_in(ebda, ebda + 1024) : NULL;
        if (found) {
            return found;
        }
    }
    return find_rsdp_in(0xE0000, 0x100000);
}

/**
 * 借来读物理内存的一页：内核自己的一页数据，它的内容没人用，所以可以临时把这一页虚拟地址
 * 改成指向别的物理页，读完再改回来。
 */
static uint8_t window[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));

/**
 * 经过 window 读物理页 page 里从 offset 开始的 len 个字节。
 * 内核的数据是用大页映射的话（x86_64）改不了单独一页，返回 false。
 */
static bool read_through_window(uint64_t page, size_t offset, void *dst, size_t len) {
    vaddr_t virt = (vaddr_t)window;
    paddr_t own;
    uint32_t flags;
    if (!hal::Mmu::query(HAL_ADDR_SPACE_CURRENT, virt, &own, &flags) || (own & (PAGE_SIZE - 1)) != 0) {
        return false;
    }
    // 改动期间不能被换到别的 CPU 上去：别的 CPU 没有碰过这一页，但刷新 TLB 只管得了自己
    bool irq_state = kernel::Interrupts::disable();
    bool ok = hal::Mmu::map(HAL_ADDR_SPACE_CURRENT, virt, (paddr_t)page, HAL_PAGE_PRESENT);
    if (ok) {
        hal::Mmu::flush_tlb(virt);
        const uint8_t *source = window + offset;
        __asm__ volatile("" : "+r"(source));    // 编译器以为 window 里永远是 0
        memcpy(dst, source, len);
        hal::Mmu::map(HAL_ADDR_SPACE_CURRENT, virt, own, flags);
        hal::Mmu::flush_tlb(virt);
    }
    kernel::Interrupts::restore(irq_state);
    return ok;
}

/**
 * 把物理地址 [phys, phys + len) 的内容拷出来。在内核的直接映射区里就直接读；不在的话
 * （i686 上内存超过 2GB 时，表在直接映射区盖不到的地方）一页一页地经过 window 读。
 */
static bool read_phys(uint64_t phys, void *dst, size_t len) {
    uint8_t *out = (uint8_t *)dst;
    while (len > 0) {
        size_t offset = (size_t)(phys & (PAGE_SIZE - 1));
        size_t n = PAGE_SIZE - offset < len ? PAGE_SIZE - offset : len;
        const uint8_t *direct = phys_ptr(phys, n);
        if (direct) {
            memcpy(out, direct, n);
        } else if (!read_through_window(phys - offset, offset, out, n)) {
            return false;
        }
        phys += n;
        out += n;
        len -= n;
    }
    return true;
}

/** 一张表最大多大：再大就当作表头是坏的 */
#define TABLE_MAX_LENGTH    (4u * 1024 * 1024)

/**
 * 物理地址 phys 上签名是 signature、长度至少 min_length 的那张表，整张读进新分配的内存
 * （用完 kfree）。不是这张表、读不到、校验和不对都返回 NULL。
 */
static struct sdt_header *load_table(uint32_t phys, const char *signature, uint32_t min_length) {
    struct sdt_header header;
    if (!read_phys(phys, &header, sizeof(header)) || memcmp(header.signature, signature, 4) != 0 ||
        header.length < sizeof(header) || header.length < min_length || header.length > TABLE_MAX_LENGTH) {
        return NULL;
    }
    uint8_t *table = (uint8_t *)kmalloc(header.length);
    if (!table) {
        return NULL;
    }
    if (!read_phys(phys, table, header.length) || !sums_to_zero(table, header.length)) {
        kfree(table);
        return NULL;
    }
    return (struct sdt_header *)table;
}

/** RSDT 里签名是 signature、长度至少 min_length 的那张表（用完 kfree）；没有返回 NULL */
static struct sdt_header *find_table(const char *signature, uint32_t min_length) {
    const struct rsdp *rsdp = find_rsdp();
    struct sdt_header *rsdt = rsdp ? load_table(rsdp->rsdt_address, "RSDT", sizeof(struct sdt_header)) : NULL;
    if (!rsdt) {
        return NULL;
    }

    // RSDT 的头后面是一串 32 位的物理地址，每个指向一张表
    uint32_t entries = (rsdt->length - sizeof(struct sdt_header)) / 4;
    const uint8_t *pointers = (const uint8_t *)rsdt + sizeof(struct sdt_header);
    struct sdt_header *found = NULL;
    for (uint32_t i = 0; i < entries && !found; i++) {
        uint32_t address;
        memcpy(&address, pointers + 4 * i, 4);
        found = load_table(address, signature, min_length);
    }
    kfree(rsdt);
    return found;
}

/** 等一小会儿：往 0x80 端口（主板上的诊断端口，写什么都没有后果）写一次大约 1 微秒 */
static void io_delay(uint32_t writes) {
    for (uint32_t i = 0; i < writes; i++) {
        hal::Port::write8(0x80, 0);
    }
}

/**
 * 电源管理还在固件手里的话（开机时常常如此），让它交出来：不交的话往控制寄存器里写的不算，
 * 电源键按下时中断也不来（固件自己收走了）。
 */
static void take_power_management(uint16_t control, uint32_t smi_command, uint8_t acpi_enable) {
    if (!(hal::Port::read16(control) & PM1_SCI_ENABLE) && smi_command != 0 &&
        smi_command <= 0xFFFF && acpi_enable != 0) {
        hal::Port::write8((uint16_t)smi_command, acpi_enable);
        for (int i = 0; i < 3000 && !(hal::Port::read16(control) & PM1_SCI_ENABLE); i++) {
            io_delay(1000);
        }
    }
}

namespace drivers {

uint32_t Acpi::cpu_apic_ids(uint8_t *ids, uint32_t max) {
    struct sdt_header *table = find_table("APIC", sizeof(struct madt));
    if (!table) {
        return 0;
    }

    uint32_t count = 0;
    const uint8_t *p = (const uint8_t *)table + sizeof(struct madt);
    const uint8_t *end = (const uint8_t *)table + table->length;
    while (p + 2 <= end && p[1] >= 2 && p + p[1] <= end) {
        if (p[0] == MADT_LOCAL_APIC && p[1] >= sizeof(struct madt_local_apic)) {
            struct madt_local_apic cpu;
            memcpy(&cpu, p, sizeof(cpu));
            if ((cpu.flags & MADT_LOCAL_APIC_ENABLED) && count < max) {
                ids[count++] = cpu.apic_id;
            }
        }
        p += p[1];
    }
    kfree(table);
    return count;
}

bool Acpi::find_s5(const uint8_t *aml, size_t length, uint8_t *type_a, uint8_t *type_b) {
    // 要找的是：NameOp、（可能有一个根前缀 \）、"_S5_"、PackageOp、包的长度、项数、一项一项
    for (size_t i = 1; i + 4 < length; i++) {
        if (memcmp(aml + i, "_S5_", 4) != 0) {
            continue;
        }
        bool named = aml[i - 1] == AML_NAME ||
                     (i >= 2 && aml[i - 1] == AML_ROOT_PREFIX && aml[i - 2] == AML_NAME);
        if (!named || aml[i + 4] != AML_PACKAGE) {
            continue;
        }
        // 包的长度占 1 到 4 个字节，第一个字节的最高两位说后面还有几个；再后面一个字节是项数
        size_t p = i + 5;
        if (p >= length) {
            return false;
        }
        p += 1 + (aml[p] >> 6);
        if (p >= length || aml[p] < 2) {
            return false;
        }
        p++;
        // 每一项：小整数直接就是一个字节（0 和 1 有自己的操作码，值就是操作码），或者带一个前缀
        uint8_t values[2];
        for (int n = 0; n < 2; n++) {
            if (p < length && aml[p] == AML_BYTE_PREFIX) {
                p++;
            }
            if (p >= length || aml[p] > 7) {
                return false;
            }
            values[n] = aml[p++];
        }
        *type_a = values[0];
        *type_b = values[1];
        return true;
    }
    return false;
}

void Acpi::power_off() {
    // 把要用的几个数从表里拿出来
    struct fadt *fadt = (struct fadt *)find_table("FACP", FADT_LENGTH_V1);
    if (!fadt) {
        return;
    }
    uint32_t pm1a = fadt->pm1a_control, pm1b = fadt->pm1b_control;
    uint32_t smi_command = fadt->smi_command;
    uint8_t acpi_enable = fadt->acpi_enable;
    struct sdt_header *dsdt = load_table(fadt->dsdt, "DSDT", sizeof(struct sdt_header));
    kfree(fadt);
    uint8_t type_a = 0, type_b = 0;
    bool found = dsdt && find_s5((const uint8_t *)dsdt + sizeof(struct sdt_header),
                                 dsdt->length - sizeof(struct sdt_header), &type_a, &type_b);
    kfree(dsdt);
    if (!found || pm1a == 0 || pm1a > 0xFFFF || pm1b > 0xFFFF) {
        return;
    }
    uint16_t control_a = (uint16_t)pm1a;
    uint16_t control_b = (uint16_t)pm1b;

    take_power_management(control_a, smi_command, acpi_enable);

    // 先写进入哪种状态，再置"现在就进入"的那一位
    uint16_t a = (uint16_t)((hal::Port::read16(control_a) & ~(PM1_SLEEP_TYPE_MASK | PM1_SLEEP_ENABLE)) |
                            ((uint32_t)type_a << PM1_SLEEP_TYPE_SHIFT));
    hal::Port::write16(control_a, a);
    if (control_b != 0) {
        uint16_t b = (uint16_t)((hal::Port::read16(control_b) & ~(PM1_SLEEP_TYPE_MASK | PM1_SLEEP_ENABLE)) |
                                ((uint32_t)type_b << PM1_SLEEP_TYPE_SHIFT));
        hal::Port::write16(control_b, b);
        hal::Port::write16(control_b, b | PM1_SLEEP_ENABLE);
    }
    hal::Port::write16(control_a, a | PM1_SLEEP_ENABLE);
    io_delay(1000000);      // 电源不是立刻断的；过了这么久还在运行就是没成
}

bool Acpi::power_button(uint16_t *port, uint32_t *length, uint32_t *irq) {
    struct fadt *fadt = (struct fadt *)find_table("FACP", FADT_LENGTH_V1);
    if (!fadt) {
        return false;
    }
    uint32_t event = fadt->pm1a_event, control = fadt->pm1a_control;
    uint32_t event_length = fadt->pm1_event_length;
    uint32_t sci = fadt->sci_interrupt;
    uint32_t smi_command = fadt->smi_command;
    uint8_t acpi_enable = fadt->acpi_enable;
    bool fixed = !(fadt->flags & FADT_FLAG_NO_POWER_BUTTON);
    kfree(fadt);
    // 状态和使能各至少 2 个字节；中断线要是老式中断控制器上的一条
    if (!fixed || event == 0 || event > 0xFFFF || event_length < 4 || control == 0 || control > 0xFFFF ||
        sci == 0 || sci >= 16) {
        return false;
    }
    take_power_management((uint16_t)control, smi_command, acpi_enable);
    *port = (uint16_t)event;
    *length = event_length;
    *irq = sci;
    return true;
}

void Acpi::reset() {
    struct fadt *fadt = (struct fadt *)find_table("FACP", sizeof(struct fadt));
    if (!fadt) {
        return;
    }
    bool usable = (fadt->flags & FADT_FLAG_RESET) && fadt->reset_register.space == ACPI_SPACE_IO &&
                  fadt->reset_register.address != 0 && fadt->reset_register.address <= 0xFFFF;
    uint16_t port = (uint16_t)fadt->reset_register.address;
    uint8_t value = fadt->reset_value;
    kfree(fadt);
    if (usable) {
        hal::Port::write8(port, value);
        io_delay(100000);
    }
}

} // namespace drivers
