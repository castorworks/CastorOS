// ============================================================================
// x86_driver_test.cpp - x86 驱动回归测试（控制台 ANSI 解析、DMA 内存）
// ============================================================================
//
// 测试覆盖:
//   - VGA 文本终端：超长 ANSI 参数不能把光标移出屏幕
//   - 帧缓冲终端：同上
//   - drivers::Dma：物理连续、页对齐、已清零、4GB 以下
//
// 这些套件由 run_pci_tests() 运行（见 pci_test.cpp），计入 "PCI Tests" 模块。
// ============================================================================

#include <tests/ktest.h>
#include <lib/kprintf.h>

#if defined(ARCH_I686) || defined(ARCH_X86_64)

#include <drivers/vga.h>
#include <drivers/framebuffer.h>
#include <drivers/x86/dma.h>
#include <mm/pmm.h>
#include <kernel/interrupt.h>
#include <lib/string.h>

#define VGA_TEST_WIDTH   80
#define VGA_TEST_HEIGHT  25

// ============================================================================
// VGA 文本终端
// ============================================================================

/** 把 VGA 光标放回 (row, col)：测试不应该打乱文本模式下的屏幕输出位置 */
static void vga_test_restore_cursor(int row, int col) {
    char seq[16];

    drivers::Vga::print("\033[H");
    if (row > 0) {
        snprintf(seq, sizeof(seq), "\033[%dB", row);
        drivers::Vga::print(seq);
    }
    if (col > 0) {
        snprintf(seq, sizeof(seq), "\033[%dC", col);
        drivers::Vga::print(seq);
    }
}

static bool vga_test_cursor_on_screen(void) {
    int row = -1, col = -1;
    drivers::Vga::get_cursor(&row, &col);
    return row >= 0 && row < VGA_TEST_HEIGHT && col >= 0 && col <= VGA_TEST_WIDTH;
}

/**
 * 参数回绕成负数（4294965453 == (uint32_t)-1843）时，旧代码把 'A' 算成
 * 下移 1843 行，随后的字符写到显存之外的内核内存
 */
TEST_CASE(test_vga_ansi_param_wraparound) {
    int saved_row, saved_col;
    drivers::Vga::get_cursor(&saved_row, &saved_col);

    drivers::Vga::print("\033[4294965453A");
    ASSERT_TRUE(vga_test_cursor_on_screen());
    drivers::Vga::print("\033[4294965453B");
    ASSERT_TRUE(vga_test_cursor_on_screen());
    drivers::Vga::print("\033[4294965453C");
    ASSERT_TRUE(vga_test_cursor_on_screen());
    drivers::Vga::print("\033[4294965453D");
    ASSERT_TRUE(vga_test_cursor_on_screen());

    // 更长的数字串（多次回绕）
    drivers::Vga::print("\033[99999999999999999999999999B");
    ASSERT_TRUE(vga_test_cursor_on_screen());
    drivers::Vga::print("\033[99999999999999999999999999;7A");
    ASSERT_TRUE(vga_test_cursor_on_screen());

    vga_test_restore_cursor(saved_row, saved_col);
}

/** 光标移动停在屏幕边缘，正常的小步数不受影响 */
TEST_CASE(test_vga_ansi_cursor_moves_clamped) {
    int saved_row, saved_col;
    int row, col;
    drivers::Vga::get_cursor(&saved_row, &saved_col);

    drivers::Vga::print("\033[H");
    drivers::Vga::get_cursor(&row, &col);
    ASSERT_EQ(0, row);
    ASSERT_EQ(0, col);

    drivers::Vga::print("\033[5B");
    drivers::Vga::get_cursor(&row, &col);
    ASSERT_EQ(5, row);
    ASSERT_EQ(0, col);

    drivers::Vga::print("\033[200B");
    drivers::Vga::get_cursor(&row, &col);
    ASSERT_EQ(VGA_TEST_HEIGHT - 1, row);

    drivers::Vga::print("\033[200C");
    drivers::Vga::get_cursor(&row, &col);
    ASSERT_EQ(VGA_TEST_WIDTH - 1, col);

    drivers::Vga::print("\033[3D");
    drivers::Vga::get_cursor(&row, &col);
    ASSERT_EQ(VGA_TEST_WIDTH - 4, col);

    drivers::Vga::print("\033[200A");
    drivers::Vga::get_cursor(&row, &col);
    ASSERT_EQ(0, row);

    drivers::Vga::print("\033[200D");
    drivers::Vga::get_cursor(&row, &col);
    ASSERT_EQ(0, col);

    vga_test_restore_cursor(saved_row, saved_col);
}

// ============================================================================
// 帧缓冲终端
// ============================================================================

static void fb_test_puts(const char *s) {
    while (*s) {
        drivers::Framebuffer::terminal_putchar(*s++);
    }
}

static bool fb_test_cursor_on_screen(void) {
    int row = drivers::Framebuffer::terminal_get_cursor_row();
    int col = drivers::Framebuffer::terminal_get_cursor_col();
    return row >= 0 && row < drivers::Framebuffer::get_rows() &&
           col >= 0 && col < drivers::Framebuffer::get_cols();
}

/**
 * 2147483647 加上当前行号会溢出成负数，旧代码据此把光标放到负的行上
 */
TEST_CASE(test_fb_terminal_ansi_param_overflow) {
    if (!drivers::Framebuffer::is_initialized()) {
        // 文本模式启动：没有帧缓冲终端可测
        return;
    }

    int saved_row = drivers::Framebuffer::terminal_get_cursor_row();
    int saved_col = drivers::Framebuffer::terminal_get_cursor_col();

    drivers::Framebuffer::terminal_set_cursor(3, 3);
    fb_test_puts("\033[2147483647B");
    ASSERT_TRUE(fb_test_cursor_on_screen());
    ASSERT_EQ(drivers::Framebuffer::get_rows() - 1, drivers::Framebuffer::terminal_get_cursor_row());

    fb_test_puts("\033[2147483647C");
    ASSERT_TRUE(fb_test_cursor_on_screen());
    ASSERT_EQ(drivers::Framebuffer::get_cols() - 1, drivers::Framebuffer::terminal_get_cursor_col());

    fb_test_puts("\033[4294965453A");
    ASSERT_TRUE(fb_test_cursor_on_screen());
    fb_test_puts("\033[4294965453D");
    ASSERT_TRUE(fb_test_cursor_on_screen());
    fb_test_puts("\033[99999999999999999999;99999999999999999999H");
    ASSERT_TRUE(fb_test_cursor_on_screen());

    drivers::Framebuffer::terminal_set_cursor(saved_col, saved_row);
}

// ============================================================================
// DMA 内存
// ============================================================================

/** 分配结果：页对齐、4GB 以下、已清零、虚拟地址就是该物理地址的直接映射 */
TEST_CASE(test_dma_alloc_properties) {
    const size_t size = 3 * PAGE_SIZE - 100;  // 向上取整到 3 页
    paddr_t phys = PADDR_INVALID;

    uint8_t *virt = (uint8_t *)drivers::Dma::alloc(size, &phys);
    ASSERT_NOT_NULL(virt);
    ASSERT_TRUE(phys != PADDR_INVALID);
    ASSERT_TRUE((phys & (PAGE_SIZE - 1)) == 0);
    ASSERT_TRUE(phys + 3 * PAGE_SIZE <= 0x100000000ULL);
    ASSERT_TRUE((uintptr_t)virt == PHYS_TO_VIRT((uintptr_t)phys));

    bool zeroed = true;
    for (size_t i = 0; i < 3 * PAGE_SIZE; i++) {
        if (virt[i] != 0) {
            zeroed = false;
            break;
        }
    }
    ASSERT_TRUE(zeroed);

    // 整个区域可写（跨页）
    memset(virt, 0xA5, 3 * PAGE_SIZE);
    ASSERT_TRUE(virt[0] == 0xA5 && virt[PAGE_SIZE] == 0xA5 && virt[3 * PAGE_SIZE - 1] == 0xA5);

    drivers::Dma::free(virt, size);
}

/** 释放后页帧全部归还 */
TEST_CASE(test_dma_free_returns_frames) {
    // 关中断：网卡中断里分配缓冲区可能让堆扩展，干扰空闲页帧计数
    kernel::InterruptGuard guard;
    
    pfn_t free_before = mm::Pmm::get_info().free_frames;

    paddr_t phys_a, phys_b;
    void *a = drivers::Dma::alloc(PAGE_SIZE + 1, &phys_a);   // 2 页
    void *b = drivers::Dma::alloc(1, &phys_b);               // 1 页
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);

    // 两块不重叠
    ASSERT_TRUE(phys_b >= phys_a + 2 * PAGE_SIZE || phys_b + PAGE_SIZE <= phys_a);
    ASSERT_TRUE(mm::Pmm::get_info().free_frames == free_before - 3);

    drivers::Dma::free(a, PAGE_SIZE + 1);
    drivers::Dma::free(b, 1);
    ASSERT_TRUE(mm::Pmm::get_info().free_frames == free_before);
}

/** 无效参数 */
TEST_CASE(test_dma_alloc_invalid) {
    paddr_t phys;
    ASSERT_NULL(drivers::Dma::alloc(0, &phys));
    ASSERT_NULL(drivers::Dma::alloc(PAGE_SIZE, NULL));

    // 不应崩溃
    drivers::Dma::free(NULL, PAGE_SIZE);
}

// ============================================================================
// 测试套件
// ============================================================================

TEST_SUITE(vga_ansi_tests) {
    RUN_TEST(test_vga_ansi_param_wraparound);
    RUN_TEST(test_vga_ansi_cursor_moves_clamped);
    RUN_TEST(test_fb_terminal_ansi_param_overflow);
}

TEST_SUITE(dma_alloc_tests) {
    RUN_TEST(test_dma_alloc_properties);
    RUN_TEST(test_dma_free_returns_frames);
    RUN_TEST(test_dma_alloc_invalid);
}

void run_x86_driver_suites(void) {
    RUN_SUITE(vga_ansi_tests);
    RUN_SUITE(dma_alloc_tests);
}

#endif // ARCH_I686 || ARCH_X86_64
