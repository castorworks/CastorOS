// ============================================================================
// cxxrt_test.cpp - C++ 运行时支持测试
// ============================================================================
//
// 验证内核 C++ 运行时（lib/cxxrt.cpp）：
//   - 全局对象构造函数在 kernel_main 之前/之初被调用
//   - operator new / delete 与内核堆配合工作
//   - 虚函数分发、placement new
// ============================================================================

#include <tests/ktest.h>
#include <tests/lib/cxxrt_test.h>
#include <lib/cxxrt.h>
#include <types.h>

namespace {

// 构造函数有副作用，编译器无法把它折叠成静态初始化，
// 因此只有全局构造函数表被正确执行时 value 才会是 MAGIC。
volatile uint32_t g_ctor_seed = 0x1234;

struct GlobalCounter {
    static constexpr uint32_t MAGIC = 0xC0DE0000;
    uint32_t value;
    GlobalCounter() : value(MAGIC + g_ctor_seed) {}
};

GlobalCounter g_counter;

struct Shape {
    virtual ~Shape() { destroyed = destroyed + 1; }
    virtual uint32_t area() const = 0;
    static uint32_t destroyed;
};
uint32_t Shape::destroyed = 0;

struct Rect : Shape {
    uint32_t w, h;
    Rect(uint32_t w_, uint32_t h_) : w(w_), h(h_) {}
    uint32_t area() const override { return w * h; }
};

struct Square : Rect {
    explicit Square(uint32_t s) : Rect(s, s) {}
};

} // namespace

TEST_CASE(test_cxxrt_global_ctor_ran) {
    ASSERT_EQ_UINT(GlobalCounter::MAGIC + 0x1234, g_counter.value);
}

TEST_CASE(test_cxxrt_new_delete) {
    uint32_t *p = new uint32_t(42);
    ASSERT_NOT_NULL(p);
    ASSERT_EQ_UINT(42, *p);
    delete p;

    uint8_t *arr = new uint8_t[128];
    ASSERT_NOT_NULL(arr);
    for (uint32_t i = 0; i < 128; i++) {
        arr[i] = (uint8_t)i;
    }
    ASSERT_EQ_UINT(127, arr[127]);
    delete[] arr;
}

TEST_CASE(test_cxxrt_virtual_dispatch) {
    uint32_t before = Shape::destroyed;

    Shape *a = new Rect(3, 4);
    Shape *b = new Square(5);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);
    ASSERT_EQ_UINT(12, a->area());
    ASSERT_EQ_UINT(25, b->area());

    delete a;
    delete b;
    ASSERT_EQ_UINT(before + 2, Shape::destroyed);
}

TEST_CASE(test_cxxrt_placement_new) {
    alignas(Rect) uint8_t storage[sizeof(Rect)];
    Rect *r = new (storage) Rect(6, 7);
    ASSERT_TRUE((void *)r == (void *)storage);
    ASSERT_EQ_UINT(42, r->area());
    r->~Rect();
}

TEST_SUITE(cxxrt_tests) {
    RUN_TEST(test_cxxrt_global_ctor_ran);
    RUN_TEST(test_cxxrt_new_delete);
    RUN_TEST(test_cxxrt_virtual_dispatch);
    RUN_TEST(test_cxxrt_placement_new);
}

void run_cxxrt_tests(void) {
    RUN_SUITE(cxxrt_tests);
}
