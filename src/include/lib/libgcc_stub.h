#ifndef _LIB_LIBGCC_STUB_H_
#define _LIB_LIBGCC_STUB_H_

#include <types.h>

#ifdef __cplusplus
extern "C" {
#endif

uint64_t __udivdi3(uint64_t n, uint64_t d);
uint64_t __umoddi3(uint64_t n, uint64_t d);

#ifdef __cplusplus
}
#endif

#endif /* _LIB_LIBGCC_STUB_H_ */
