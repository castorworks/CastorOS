#ifndef _USERLAND_LIB_LIBGCC_STUB_H_
#define _USERLAND_LIB_LIBGCC_STUB_H_

#include <types.h>

// libgcc 函数声明（64位除法/取模）
uint64_t __udivdi3(uint64_t n, uint64_t d);
uint64_t __umoddi3(uint64_t n, uint64_t d);

#endif /* _USERLAND_LIB_LIBGCC_STUB_H_ */
