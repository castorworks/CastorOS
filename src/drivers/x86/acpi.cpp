// ============================================================================
// acpi.cpp - 从 ACPI 的表里读出机器上有哪些 CPU
// ============================================================================
//
// 路径是：RSDP（固件留在低端内存里的一个签名，从它找到）-> RSDT（一张"表的表"）
// -> MADT（签名 "APIC"，里面一项一项列出中断控制器，类型 0 的项是一个 CPU 的 Local APIC）。

#include <drivers/x86/acpi.h>
#include <hal/hal.h>
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

/** 一张完整的、校验和正确的表；读不到返回 NULL */
static const struct sdt_header *table_at(uint32_t phys) {
    const struct sdt_header *header = (const struct sdt_header *)phys_ptr(phys, sizeof(struct sdt_header));
    if (!header || header->length < sizeof(struct sdt_header)) {
        return NULL;
    }
    const uint8_t *whole = phys_ptr(phys, header->length);
    return whole && sums_to_zero(whole, header->length) ? (const struct sdt_header *)whole : NULL;
}

namespace drivers {

uint32_t Acpi::cpu_apic_ids(uint8_t *ids, uint32_t max) {
    const struct rsdp *rsdp = find_rsdp();
    const struct sdt_header *rsdt = rsdp ? table_at(rsdp->rsdt_address) : NULL;
    if (!rsdt || memcmp(rsdt->signature, "RSDT", 4) != 0) {
        return 0;
    }

    // RSDT 的头后面是一串 32 位的物理地址，每个指向一张表
    uint32_t entries = (rsdt->length - sizeof(struct sdt_header)) / 4;
    const uint8_t *pointers = (const uint8_t *)rsdt + sizeof(struct sdt_header);
    for (uint32_t i = 0; i < entries; i++) {
        uint32_t address;
        memcpy(&address, pointers + 4 * i, 4);
        const struct sdt_header *table = table_at(address);
        if (!table || memcmp(table->signature, "APIC", 4) != 0 || table->length < sizeof(struct madt)) {
            continue;
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
        return count;
    }
    return 0;
}

} // namespace drivers
