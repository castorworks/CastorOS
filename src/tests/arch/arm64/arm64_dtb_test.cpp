// ============================================================================
// arm64_dtb_test.cpp - 设备树解析测试
// ============================================================================
//
// 两类：对着 QEMU 实际给的那份设备树检查解析结果；用手工拼出来的小设备树检查
// 解析规则（属性的先后顺序、单元数的作用范围、损坏的输入）。

#include <tests/ktest.h>
#include <tests/arch/arm64/arm64_dtb_test.h>
#include <lib/string.h>
#include <types.h>
#include "../../../arch/arm64/include/dtb.h"
#include "../../../arch/arm64/include/gic.h"
#include <drivers/serial.h>
#include <hal/hal.h>
#include <kernel/syscall.h>

// ---------------------------------------------------------------------------
// QEMU virt 的设备树
// ---------------------------------------------------------------------------

TEST_CASE(test_dtb_qemu_virt_memory) {
    const dtb_info_t *info = dtb_parse(dtb_find(NULL));
    ASSERT_TRUE(info != NULL);
    ASSERT_TRUE(info->num_memory_regions >= 1);
    ASSERT_TRUE(info->memory[0].base == 0x40000000ULL);     // virt 的内存从 1GB 开始
    ASSERT_TRUE(info->memory[0].size >= 64ULL * 1024 * 1024);
    ASSERT_TRUE(info->total_memory >= info->memory[0].size);
}

TEST_CASE(test_dtb_qemu_virt_devices) {
    const dtb_info_t *info = dtb_parse(dtb_find(NULL));
    ASSERT_TRUE(info != NULL);

    // QEMU virt 上这些设备的位置是固定的
    ASSERT_TRUE(info->gic.found);
    ASSERT_TRUE(info->gic.distributor_base == 0x08000000ULL);
    if (info->gic.version == 2) {
        ASSERT_TRUE(info->gic.cpu_interface_base == 0x08010000ULL);
    }
    ASSERT_TRUE(info->uart_found);
    ASSERT_TRUE(info->uart_base == 0x09000000ULL);
    ASSERT_EQ_UINT(33, info->uart_irq);                     // SPI 1
    ASSERT_TRUE(info->timer_found);
    ASSERT_EQ_UINT(30, info->timer_irq);                    // PPI 14：非安全物理定时器

    // virtio-mmio 的槽位各是一个设备：名字（带地址）互不相同，各有各的基址和中断
    uint32_t virtio = 0;
    uint64_t first_base = 0;
    for (uint32_t i = 0; i < info->num_devices; i++) {
        const dtb_device_t *dev = &info->devices[i];
        if (!dtb_device_is(dev, "virtio,mmio")) {
            continue;
        }
        ASSERT_TRUE(dev->base_addr != 0 && dev->has_irq && dev->irq >= 32);
        if (virtio == 0) {
            first_base = dev->base_addr;
        } else {
            ASSERT_TRUE(dev->base_addr != first_base);
        }
        virtio++;
    }
    ASSERT_TRUE(virtio >= 2);
}

/* 定义在 arch/arm64/hal.cpp */
uint32_t arm64_timer_irq(void);

TEST_CASE(test_dtb_values_reach_the_drivers) {
    // 启动时驱动拿到的就是设备树里的值
    const dtb_info_t *info = dtb_parse(dtb_find(NULL));
    ASSERT_TRUE(info != NULL);
    ASSERT_TRUE(drivers::Serial::base() == info->uart_base);
    ASSERT_TRUE(gic_distributor_base() == info->gic.distributor_base);
    ASSERT_TRUE(gic_cpu_interface_base() == info->gic.cpu_interface_base);
    ASSERT_EQ_UINT(info->timer_irq, arm64_timer_irq());
}

TEST_CASE(test_platform_find_device) {
    // 驱动通过 device_find 系统调用走到这里：按型号、按序号查设备
    ASSERT_TRUE(dtb_parse(dtb_find(NULL)) != NULL);

    struct device_info info;
    memset(&info, 0, sizeof(info));
    strncpy(info.compatible, "arm,pl011", sizeof(info.compatible) - 1);
    ASSERT_TRUE(hal::Platform::find_device(&info));
    ASSERT_TRUE(info.base == 0x09000000ULL);
    ASSERT_TRUE(info.has_irq && info.irq == 33);

    // 同一型号的设备按序号一个个取，各不相同，取完为止
    uint64_t previous = 0;
    uint32_t count = 0;
    for (;; count++) {
        memset(&info, 0, sizeof(info));
        strncpy(info.compatible, "virtio,mmio", sizeof(info.compatible) - 1);
        info.index = count;
        if (!hal::Platform::find_device(&info)) {
            break;
        }
        ASSERT_TRUE(info.base != 0 && info.base != previous && info.size > 0);
        ASSERT_TRUE(info.has_irq && info.irq >= 32);
        previous = info.base;
    }
    ASSERT_TRUE(count >= 2);

    // 设备树里一个设备列出几个型号（从具体到一般）：按哪一个都找得到同一个设备
    // （QEMU 的 PL011 是 "arm,pl011", "arm,primecell"；别的 primecell 设备也排在这个型号下面）
    bool uart_among_primecells = false;
    for (uint32_t i = 0; ; i++) {
        memset(&info, 0, sizeof(info));
        strncpy(info.compatible, "arm,primecell", sizeof(info.compatible) - 1);
        info.index = i;
        if (!hal::Platform::find_device(&info)) {
            break;
        }
        uart_among_primecells = uart_among_primecells || info.base == 0x09000000ULL;
    }
    ASSERT_TRUE(uart_among_primecells);

    // 没有的型号、型号的前缀都找不到
    memset(&info, 0, sizeof(info));
    strncpy(info.compatible, "no,such-device", sizeof(info.compatible) - 1);
    ASSERT_FALSE(hal::Platform::find_device(&info));
    memset(&info, 0, sizeof(info));
    strncpy(info.compatible, "virtio", sizeof(info.compatible) - 1);
    ASSERT_FALSE(hal::Platform::find_device(&info));
}

// ---------------------------------------------------------------------------
// 手工拼的设备树
// ---------------------------------------------------------------------------

static uint8_t blob[1024];
static uint32_t struct_len;         // 结构块已经写了多少
static char strings[256];
static uint32_t strings_len;

#define HEADER_SIZE     40
#define STRUCT_OFFSET   HEADER_SIZE

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static void emit32(uint32_t v) {
    put32(blob + STRUCT_OFFSET + struct_len, v);
    struct_len += 4;
}

static void emit_bytes(const void *data, uint32_t len) {
    memset(blob + STRUCT_OFFSET + struct_len, 0, (len + 3) & ~3u);
    memcpy(blob + STRUCT_OFFSET + struct_len, data, len);
    struct_len += (len + 3) & ~3u;
}

static void blob_begin(void) {
    memset(blob, 0, sizeof(blob));
    struct_len = 0;
    strings_len = 0;
}

static void node_begin(const char *name) {
    emit32(1);
    emit_bytes(name, (uint32_t)strlen(name) + 1);
}

static void node_end(void) {
    emit32(2);
}

static void prop(const char *name, const void *data, uint32_t len) {
    uint32_t off = strings_len;
    memcpy(strings + off, name, strlen(name) + 1);
    strings_len += (uint32_t)strlen(name) + 1;
    emit32(3);
    emit32(len);
    emit32(off);
    emit_bytes(data, len);
}

static void prop_cells(const char *name, const uint32_t *cells, uint32_t count) {
    uint8_t raw[64];
    for (uint32_t i = 0; i < count; i++) {
        put32(raw + 4 * i, cells[i]);
    }
    prop(name, raw, 4 * count);
}

static void prop_string(const char *name, const char *value) {
    prop(name, value, (uint32_t)strlen(value) + 1);
}

/** 写上 END 标记、字符串块和头 */
static void blob_finish(void) {
    emit32(9);
    uint32_t strings_off = STRUCT_OFFSET + struct_len;
    memcpy(blob + strings_off, strings, strings_len);
    put32(blob + 0, 0xD00DFEED);
    put32(blob + 4, strings_off + strings_len);     // totalsize
    put32(blob + 8, STRUCT_OFFSET);
    put32(blob + 12, strings_off);
    put32(blob + 20, 17);                           // version
    put32(blob + 24, 16);                           // last_comp_version
    put32(blob + 32, strings_len);
    put32(blob + 36, struct_len);
}

/** 根节点开头：声明子节点的 reg 用几个单元 */
static void root_begin(uint32_t address_cells, uint32_t size_cells) {
    blob_begin();
    node_begin("");
    prop_cells("#address-cells", &address_cells, 1);
    prop_cells("#size-cells", &size_cells, 1);
}

TEST_CASE(test_dtb_memory_regions) {
    // 一个内存节点里两段，再加一个内存节点；地址 2 个单元、长度 1 个
    root_begin(2, 1);
    node_begin("memory@40000000");
    prop_string("device_type", "memory");
    const uint32_t reg1[] = { 0, 0x40000000, 0x08000000,   1, 0x00000000, 0x10000000 };
    prop_cells("reg", reg1, 6);
    node_end();
    node_begin("memory@200000000");
    prop_string("device_type", "memory");
    const uint32_t reg2[] = { 2, 0, 0x00100000 };
    prop_cells("reg", reg2, 3);
    node_end();
    // 名字像内存、其实是设备的节点不算
    node_begin("memory-controller@1000");
    prop_string("compatible", "vendor,memory-controller");
    const uint32_t reg3[] = { 0, 0x1000, 0x100 };
    prop_cells("reg", reg3, 3);
    node_end();
    node_end();
    blob_finish();

    const dtb_info_t *info = dtb_parse(blob);
    ASSERT_TRUE(info != NULL);
    ASSERT_EQ_UINT(3, info->num_memory_regions);
    ASSERT_TRUE(info->memory[0].base == 0x40000000ULL && info->memory[0].size == 0x08000000ULL);
    ASSERT_TRUE(info->memory[1].base == 0x100000000ULL && info->memory[1].size == 0x10000000ULL);
    ASSERT_TRUE(info->memory[2].base == 0x200000000ULL && info->memory[2].size == 0x00100000ULL);
    ASSERT_TRUE(info->total_memory == 0x08000000ULL + 0x10000000ULL + 0x00100000ULL);
    ASSERT_EQ_UINT(1, info->num_devices);
}

TEST_CASE(test_dtb_property_order_does_not_matter) {
    // reg 和 interrupts 排在 compatible 前面；compatible 是一个列表，要找的在第二项
    root_begin(1, 1);
    node_begin("serial@9000000");
    const uint32_t reg[] = { 0x09000000, 0x1000 };
    prop_cells("reg", reg, 2);
    const uint32_t irq[] = { 0, 5, 4 };                     // SPI 5
    prop_cells("interrupts", irq, 3);
    prop("compatible", "vendor,uart\0arm,pl011", 22);
    node_end();
    node_begin("intc@8000000");
    const uint32_t gic_reg[] = { 0x08000000, 0x10000, 0x08010000, 0x10000 };
    prop_cells("reg", gic_reg, 4);
    prop_string("compatible", "arm,cortex-a15-gic");
    node_end();
    node_end();
    blob_finish();

    const dtb_info_t *info = dtb_parse(blob);
    ASSERT_TRUE(info != NULL);
    ASSERT_TRUE(info->uart_found);
    ASSERT_TRUE(info->uart_base == 0x09000000ULL);
    ASSERT_EQ_UINT(37, info->uart_irq);
    ASSERT_TRUE(info->gic.found);
    ASSERT_EQ_UINT(2, info->gic.version);
    ASSERT_TRUE(info->gic.distributor_base == 0x08000000ULL);
    ASSERT_TRUE(info->gic.cpu_interface_base == 0x08010000ULL);

    // 设备列表里留着完整的 compatible 列表：按第二项也查得到这个串口
    struct device_info found;
    memset(&found, 0, sizeof(found));
    strncpy(found.compatible, "arm,pl011", sizeof(found.compatible) - 1);
    ASSERT_TRUE(hal::Platform::find_device(&found));
    ASSERT_TRUE(found.base == 0x09000000ULL && found.irq == 37);
    ASSERT_TRUE(strcmp(found.name, "serial@9000000") == 0);
    memset(&found, 0, sizeof(found));
    strncpy(found.compatible, "vendor,uart", sizeof(found.compatible) - 1);
    ASSERT_TRUE(hal::Platform::find_device(&found));
    ASSERT_TRUE(found.base == 0x09000000ULL);
    // 列表里的一项的一部分不算
    memset(&found, 0, sizeof(found));
    strncpy(found.compatible, "arm", sizeof(found.compatible) - 1);
    ASSERT_FALSE(hal::Platform::find_device(&found));
}


TEST_CASE(test_dtb_cells_come_from_the_parent) {
    // 一个总线节点给它的子节点声明了不同的单元数：只影响它的子节点，
    // 不影响它自己的 reg，也不影响它后面的兄弟节点
    root_begin(2, 2);
    node_begin("bus@10000000");
    prop_string("compatible", "simple-bus");
    const uint32_t one = 1;
    prop_cells("#address-cells", &one, 1);
    prop_cells("#size-cells", &one, 1);
    const uint32_t bus_reg[] = { 0, 0x10000000, 0, 0x1000 };        // 按根节点的 2 + 2 读
    prop_cells("reg", bus_reg, 4);
    node_begin("dev@100");
    prop_string("compatible", "vendor,dev");
    const uint32_t dev_reg[] = { 0x100, 0x20 };                     // 按总线的 1 + 1 读
    prop_cells("reg", dev_reg, 2);
    node_end();
    node_end();
    node_begin("after@20000000");
    prop_string("compatible", "vendor,after");
    const uint32_t after_reg[] = { 0, 0x20000000, 0, 0x2000 };      // 又回到 2 + 2
    prop_cells("reg", after_reg, 4);
    node_end();
    node_end();
    blob_finish();

    const dtb_info_t *info = dtb_parse(blob);
    ASSERT_TRUE(info != NULL);
    ASSERT_EQ_UINT(3, info->num_devices);
    // 节点结束时才归类，所以子节点排在它的父节点前面
    ASSERT_TRUE(strcmp(info->devices[0].name, "dev@100") == 0);
    ASSERT_TRUE(info->devices[0].base_addr == 0x100 && info->devices[0].size == 0x20);
    ASSERT_TRUE(strcmp(info->devices[1].name, "bus@10000000") == 0);
    ASSERT_TRUE(info->devices[1].base_addr == 0x10000000ULL && info->devices[1].size == 0x1000);
    ASSERT_TRUE(info->devices[2].base_addr == 0x20000000ULL && info->devices[2].size == 0x2000);
    ASSERT_FALSE(info->devices[2].has_irq);
}

TEST_CASE(test_dtb_long_compatible_list) {
    // 列表比设备表里留的位置长：留下前面完整的几项，不会留半项
    static char list[160];
    uint32_t len = 0;
    for (int i = 0; i < 6; i++) {
        const char *item = "vendor,a-rather-long-model-name-";
        memcpy(list + len, item, strlen(item));
        len += (uint32_t)strlen(item);
        list[len++] = (char)('0' + i);
        list[len++] = '\0';
    }
    root_begin(1, 1);
    node_begin("dev@1000");
    prop("compatible", list, len);
    node_end();
    node_end();
    blob_finish();

    const dtb_info_t *info = dtb_parse(blob);
    ASSERT_TRUE(info != NULL);
    ASSERT_EQ_UINT(1, info->num_devices);
    const dtb_device_t *dev = &info->devices[0];
    ASSERT_TRUE(dev->compatible_len > 0 && dev->compatible_len <= DTB_MAX_COMPATIBLE_LEN);
    ASSERT_TRUE(dev->compatible[dev->compatible_len - 1] == '\0');
    ASSERT_TRUE(dtb_device_is(dev, "vendor,a-rather-long-model-name-0"));
    ASSERT_TRUE(dtb_device_is(dev, "vendor,a-rather-long-model-name-1"));
    ASSERT_FALSE(dtb_device_is(dev, "vendor,a-rather-long-model-name-5"));     // 放不下的
    ASSERT_FALSE(dtb_device_is(dev, "vendor,a-rather-long-model-name-"));      // 半项
}

TEST_CASE(test_dtb_rejects_bad_input) {
    ASSERT_TRUE(dtb_parse(NULL) == NULL);
    ASSERT_TRUE(dtb_get_info() == NULL);

    root_begin(2, 1);
    node_begin("memory@0");
    prop_string("device_type", "memory");
    const uint32_t reg[] = { 0, 0, 0x1000 };
    prop_cells("reg", reg, 3);
    node_end();
    node_end();
    blob_finish();
    ASSERT_TRUE(dtb_parse(blob) != NULL);
    ASSERT_TRUE(dtb_get_info() != NULL);

    // 魔数不对
    blob[0] ^= 0xFF;
    ASSERT_TRUE(dtb_parse(blob) == NULL);
    blob[0] ^= 0xFF;

    // 结构块被截断：没有走到 END 标记
    uint32_t full = struct_len;
    put32(blob + 36, full - 8);
    ASSERT_TRUE(dtb_parse(blob) == NULL);
    put32(blob + 36, full);

    // 属性的长度超出了结构块
    put32(blob + STRUCT_OFFSET + 8 + 4 + 4, 0x10000);       // 根节点第一个属性的 len
    ASSERT_TRUE(dtb_parse(blob) == NULL);

    // 块的偏移超出了整个设备树
    blob_finish();
    put32(blob + 12, 0x100000);
    ASSERT_TRUE(dtb_parse(blob) == NULL);
    ASSERT_TRUE(dtb_get_info() == NULL);
}

/** 最后把真的那份重新解析一遍：别的代码之后拿到的应该是真实硬件的信息 */
TEST_CASE(test_dtb_restore_real_tree) {
    ASSERT_TRUE(dtb_parse(dtb_find(NULL)) != NULL);
    ASSERT_TRUE(dtb_get_info() != NULL && dtb_get_info()->uart_found);
}

TEST_SUITE(arm64_dtb_tests) {
    RUN_TEST(test_dtb_qemu_virt_memory);
    RUN_TEST(test_dtb_qemu_virt_devices);
    RUN_TEST(test_dtb_values_reach_the_drivers);
    RUN_TEST(test_platform_find_device);
    RUN_TEST(test_dtb_memory_regions);
    RUN_TEST(test_dtb_property_order_does_not_matter);
    RUN_TEST(test_dtb_cells_come_from_the_parent);
    RUN_TEST(test_dtb_long_compatible_list);
    RUN_TEST(test_dtb_rejects_bad_input);
    RUN_TEST(test_dtb_restore_real_tree);
}

void run_arm64_dtb_tests(void) {
    RUN_SUITE(arm64_dtb_tests);
}
