// ACPI：从 DSDT 的 AML 里读出关机用的值（Acpi::find_s5）。i686 和 x86_64 共用，
// 别的架构上这个文件是空的。

#if defined(ARCH_I686) || defined(ARCH_X86_64)

#include <tests/arch/acpi_test.h>
#include <tests/ktest.h>
#include <drivers/x86/acpi.h>

using drivers::Acpi;

static void test_s5_plain_bytes(void) {
    // QEMU 的写法：Name (_S5, Package (4) { Zero, Zero, Zero, Zero })，前后有别的东西
    static const uint8_t aml[] = { 0x10, 0x20, 0x08, '_', 'S', '5', '_', 0x12, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00, 0x14 };
    uint8_t a = 0xFF, b = 0xFF;
    ASSERT_TRUE(Acpi::find_s5(aml, sizeof(aml), &a, &b));
    ASSERT_EQ_UINT(0, a);
    ASSERT_EQ_UINT(0, b);
}

static void test_s5_byte_prefix_and_root(void) {
    // 真机上常见的写法：名字前带根前缀，值带"一个字节的整数"的前缀
    static const uint8_t aml[] = { 0x08, '\\', '_', 'S', '5', '_', 0x12, 0x08, 0x04, 0x0A, 0x07, 0x0A, 0x05, 0x00, 0x00 };
    uint8_t a = 0, b = 0;
    ASSERT_TRUE(Acpi::find_s5(aml, sizeof(aml), &a, &b));
    ASSERT_EQ_UINT(7, a);
    ASSERT_EQ_UINT(5, b);

    // 一项带前缀、一项不带；包的长度占两个字节
    static const uint8_t mixed[] = { 0x08, '_', 'S', '5', '_', 0x12, 0x47, 0x00, 0x02, 0x0A, 0x05, 0x01 };
    ASSERT_TRUE(Acpi::find_s5(mixed, sizeof(mixed), &a, &b));
    ASSERT_EQ_UINT(5, a);
    ASSERT_EQ_UINT(1, b);
}

static void test_s5_not_found(void) {
    uint8_t a = 0, b = 0;
    // 没有这个名字
    static const uint8_t other[] = { 0x08, '_', 'S', '4', '_', 0x12, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00 };
    ASSERT_FALSE(Acpi::find_s5(other, sizeof(other), &a, &b));
    // 这四个字节出现了，但不是在定义它：一个方法里引用了它
    static const uint8_t reference[] = { 0x70, '_', 'S', '5', '_', 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    ASSERT_FALSE(Acpi::find_s5(reference, sizeof(reference), &a, &b));
    // 引用在前、定义在后：找到的是定义
    static const uint8_t both[] = { 0x70, '_', 'S', '5', '_', 0x60, 0x08, '_', 'S', '5', '_', 0x12, 0x06, 0x04, 0x0A, 0x03, 0x0A, 0x03 };
    ASSERT_TRUE(Acpi::find_s5(both, sizeof(both), &a, &b));
    ASSERT_EQ_UINT(3, a);
    ASSERT_FALSE(Acpi::find_s5(NULL, 0, &a, &b));
}

static void test_s5_truncated(void) {
    // 表在定义的中途结束，或者值不是 3 位放得下的数：不认，也不读到表的外面去
    static const uint8_t aml[] = { 0x08, '_', 'S', '5', '_', 0x12, 0x06, 0x04, 0x0A, 0x05, 0x0A, 0x05 };
    uint8_t a = 0, b = 0;
    for (size_t length = 0; length < sizeof(aml); length++) {
        ASSERT_FALSE(Acpi::find_s5(aml, length, &a, &b));
    }
    ASSERT_TRUE(Acpi::find_s5(aml, sizeof(aml), &a, &b));

    static const uint8_t too_big[] = { 0x08, '_', 'S', '5', '_', 0x12, 0x06, 0x04, 0x0A, 0x55, 0x0A, 0x05 };
    ASSERT_FALSE(Acpi::find_s5(too_big, sizeof(too_big), &a, &b));
    static const uint8_t one_element[] = { 0x08, '_', 'S', '5', '_', 0x12, 0x03, 0x01, 0x05, 0x00, 0x00 };
    ASSERT_FALSE(Acpi::find_s5(one_element, sizeof(one_element), &a, &b));
}

void run_acpi_tests(void) {
    unittest_begin_suite("ACPI Table Tests");
    unittest_run_test("_S5 with plain values", test_s5_plain_bytes);
    unittest_run_test("_S5 with prefixes", test_s5_byte_prefix_and_root);
    unittest_run_test("no _S5 definition", test_s5_not_found);
    unittest_run_test("truncated or invalid _S5", test_s5_truncated);
    unittest_end_suite();
}

#endif
