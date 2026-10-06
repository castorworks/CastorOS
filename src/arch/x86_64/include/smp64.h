#ifndef _ARCH_X86_64_SMP64_H_
#define _ARCH_X86_64_SMP64_H_

#include <types.h>
#include <mm/mm_types.h>

/* What starting the other CPUs (cpu/smp64.cpp) needs from the page-table code
 * (mm/paging64.cpp); see the comments there. */

/** Map the 2MB a device register page lies in into the kernel's direct map, uncached */
uintptr_t paging64_map_device(paddr_t phys);

/** A page table a starting CPU can turn paging on with: the kernel's plus an identity mapping */
paddr_t paging64_make_startup_table(void);

#endif /* _ARCH_X86_64_SMP64_H_ */
