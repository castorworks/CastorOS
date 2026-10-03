#include <lib/libgcc_stub.h>

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
