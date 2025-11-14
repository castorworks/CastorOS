#include <libgcc_stub.h>
#include <types.h>

// 非内联版本的 libgcc 函数，用于链接
// 这些函数在 -O0 优化级别下可能不会被内联，所以需要提供实际实现

uint64_t __udivdi3(uint64_t n, uint64_t d) {
    uint64_t q = 0, r = 0;
    for (int i = 0; i < 64; i++) {
        r = (r << 1) | ((n >> (63 - i)) & 1);
        if (r >= d) {
            r -= d;
            q |= (1ULL << (63 - i));
        }
    }
    return q;
}

uint64_t __umoddi3(uint64_t n, uint64_t d) {
    uint64_t q = 0, r = 0;
    for (int i = 0; i < 64; i++) {
        r = (r << 1) | ((n >> (63 - i)) & 1);
        if (r >= d) {
            r -= d;
            q |= (1ULL << (63 - i));
        }
    }
    return r;
}

