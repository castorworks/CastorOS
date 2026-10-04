// ============================================================================
// elf.c - ELF 可执行文件加载器
// 支持 32 位 (i686)、64 位 (x86_64) 和 ARM64 ELF 格式
// ============================================================================

#include <kernel/elf.h>
#include <kernel/task.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/mm_types.h>
#include <lib/klog.h>
#include <lib/string.h>
#if defined(ARCH_ARM64)
#include <hal/hal.h>
#endif

/* 本架构使用的 ELF 头/程序头类型 */
#if defined(ARCH_X86_64) || defined(ARCH_ARM64)
typedef elf64_ehdr_t elf_native_ehdr_t;
typedef elf64_phdr_t elf_native_phdr_t;
#else
typedef elf32_ehdr_t elf_native_ehdr_t;
typedef elf32_phdr_t elf_native_phdr_t;
#endif

/**
 * 用户程序映像可以占用的地址上界（不含）。
 * 用户栈固定放在用户空间顶部，映像必须整体位于栈区之下，
 * 这样段既到不了内核半区，也不会和栈重叠。
 */
static inline uint64_t elf_user_image_limit(void) {
#if defined(ARCH_ARM64)
    return (uint64_t)ARM64_USER_STACK_TOP - USER_STACK_SIZE;
#else
    return (uint64_t)USER_SPACE_END - USER_STACK_SIZE;
#endif
}

bool kernel::Elf::validate_header(const void *elf_data, size_t size) {
    if (!elf_data) return false;
    /* 文件至少要装得下完整的 ELF 头，否则下面读到的就是缓冲区之外的内容 */
    if (size < sizeof(elf_native_ehdr_t)) {
        LOG_ERROR_MSG("ELF: File too small for ELF header (%u bytes)\n", (unsigned)size);
        return false;
    }
    const uint8_t *ident = (const uint8_t *)elf_data;
    if (ident[0] != 0x7F || ident[1] != 'E' || ident[2] != 'L' || ident[3] != 'F') {
        LOG_ERROR_MSG("ELF: Invalid magic number\n");
        return false;
    }
    if (ident[5] != ELF_DATA_LSB) {
        LOG_ERROR_MSG("ELF: Not little-endian\n");
        return false;
    }
    if (ident[6] != EV_CURRENT) {
        LOG_ERROR_MSG("ELF: Invalid version\n");
        return false;
    }
    const elf_native_ehdr_t *ehdr = (const elf_native_ehdr_t *)elf_data;
#if defined(ARCH_X86_64)
    if (ident[4] != ELF_CLASS_64) {
        LOG_ERROR_MSG("ELF: Expected 64-bit ELF for x86_64\n");
        return false;
    }
    if (ehdr->e_machine != EM_X86_64) {
        LOG_ERROR_MSG("ELF: Not x86_64 (machine=%d)\n", ehdr->e_machine);
        return false;
    }
#elif defined(ARCH_ARM64)
    if (ident[4] != ELF_CLASS_64) {
        LOG_ERROR_MSG("ELF: Expected 64-bit ELF for ARM64\n");
        return false;
    }
    if (ehdr->e_machine != EM_AARCH64) {
        LOG_ERROR_MSG("ELF: Not ARM64 (machine=%d)\n", ehdr->e_machine);
        return false;
    }
#else
    if (ident[4] != ELF_CLASS_32) {
        LOG_ERROR_MSG("ELF: Expected 32-bit ELF for i686\n");
        return false;
    }
    if (ehdr->e_machine != EM_386) {
        LOG_ERROR_MSG("ELF: Not i386 (machine=%d)\n", ehdr->e_machine);
        return false;
    }
#endif
    if (ehdr->e_type != ET_EXEC) {
        LOG_ERROR_MSG("ELF: Not executable (type=%d)\n", ehdr->e_type);
        return false;
    }
    return true;
}

bool kernel::Elf::validate(const void *elf_data, size_t size) {
    if (!kernel::Elf::validate_header(elf_data, size)) return false;

    /* 以下所有来自文件的偏移/长度/地址都按 64 位、用“先比较再相减”的写法检查，不会回绕 */
    const elf_native_ehdr_t *ehdr = (const elf_native_ehdr_t *)elf_data;
    if (ehdr->e_phentsize != sizeof(elf_native_phdr_t)) {
        LOG_ERROR_MSG("ELF: Unexpected program header size %u\n", ehdr->e_phentsize);
        return false;
    }
    uint64_t phoff = ehdr->e_phoff;
    uint64_t phsize = (uint64_t)ehdr->e_phnum * sizeof(elf_native_phdr_t);
    if (ehdr->e_phnum == 0 || phoff > size || phsize > size - phoff) {
        LOG_ERROR_MSG("ELF: Program header table outside file\n");
        return false;
    }

    const elf_native_phdr_t *phdr =
        (const elf_native_phdr_t *)((const uint8_t *)elf_data + phoff);
    const uint64_t limit = elf_user_image_limit();
    const uint64_t entry = ehdr->e_entry;
    bool entry_ok = false;

    for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
        const elf_native_phdr_t *ph = &phdr[i];
        if (ph->p_type != PT_LOAD) continue;

        uint64_t offset = ph->p_offset;
        uint64_t filesz = ph->p_filesz;
        uint64_t memsz = ph->p_memsz;
        uint64_t vaddr = ph->p_vaddr;

        if (filesz > memsz) {
            LOG_ERROR_MSG("ELF: Segment %u has p_filesz > p_memsz\n", i);
            return false;
        }
        if (offset > size || filesz > size - offset) {
            LOG_ERROR_MSG("ELF: Segment %u exceeds file size\n", i);
            return false;
        }
        /* 段必须整体位于用户映像区内：既不能进入内核半区，也不能压到用户栈 */
        if (vaddr >= limit || memsz > limit - vaddr) {
            LOG_ERROR_MSG("ELF: Segment %u outside user image area (vaddr=0x%llx, memsz=0x%llx)\n",
                          i, (unsigned long long)vaddr, (unsigned long long)memsz);
            return false;
        }
        if ((ph->p_flags & PF_X) && entry >= vaddr && entry - vaddr < memsz) {
            entry_ok = true;
        }
    }

    /* 入口点不在任何可执行段内的映像不能运行；x86_64 上非规范入口地址
     * 还会让 SYSRET 在 Ring 0 触发 #GP */
    if (!entry_ok) {
        LOG_ERROR_MSG("ELF: Entry point 0x%llx is not inside an executable segment\n",
                      (unsigned long long)entry);
        return false;
    }
    return true;
}

uintptr_t kernel::Elf::get_entry(const void *elf_data, size_t size) {
    if (!kernel::Elf::validate_header(elf_data, size)) return 0;
    const elf_native_ehdr_t *ehdr = (const elf_native_ehdr_t *)elf_data;
    return (uintptr_t)ehdr->e_entry;
}

/**
 * 把一个物理页以用户权限映射到目标地址空间
 *
 * ARM64 上 page_dir 实际是地址空间句柄（TTBR0 物理地址），通过 HAL MMU 接口映射；
 * x86 上通过 VMM 的页目录接口映射。
 */
static bool elf_map_user_page(page_directory_t *page_dir, uintptr_t vaddr, paddr_t phys,
                              uint32_t p_flags) {
#if defined(ARCH_ARM64)
    uint32_t flags = HAL_PAGE_PRESENT | HAL_PAGE_USER;
    if (p_flags & PF_W) flags |= HAL_PAGE_WRITE;
    if (p_flags & PF_X) flags |= HAL_PAGE_EXEC;
    return hal::Mmu::map((hal_addr_space_t)(uintptr_t)page_dir, vaddr, phys, flags);
#else
    uint32_t flags = PAGE_PRESENT | PAGE_USER;
    if (p_flags & PF_W) flags |= PAGE_WRITE;
    if (p_flags & PF_X) flags |= PAGE_EXEC;
    return mm::Vmm::map_page_in_directory(VIRT_TO_PHYS((uintptr_t)page_dir), vaddr,
                                          (uintptr_t)phys, flags);
#endif
}

/**
 * 把已通过 validate() 的映像加载到地址空间
 *
 * 失败时已映射的页留在 page_dir 中，由调用者销毁整个地址空间来回收。
 */
static bool elf_load_impl(const void *elf_data, page_directory_t *page_dir,
                          uintptr_t *entry_point, uintptr_t *program_end) {
    const elf_native_ehdr_t *ehdr = (const elf_native_ehdr_t *)elf_data;
    const elf_native_phdr_t *phdr =
        (const elf_native_phdr_t *)((const uint8_t *)elf_data + ehdr->e_phoff);

    LOG_INFO_MSG("ELF: Loading executable\n");
    LOG_INFO_MSG("  Entry point: 0x%llx\n", (unsigned long long)ehdr->e_entry);
    LOG_INFO_MSG("  Program headers: %u\n", ehdr->e_phnum);

    uintptr_t max_vaddr = 0;

    for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
        const elf_native_phdr_t *ph = &phdr[i];
        if (ph->p_type != PT_LOAD) continue;

        /* validate() 已保证下面的加法不回绕，且范围在用户映像区内 */
        const uintptr_t seg_start = (uintptr_t)ph->p_vaddr;
        const uintptr_t seg_filesz = (uintptr_t)ph->p_filesz;
        uintptr_t vaddr_start = PAGE_ALIGN_DOWN(seg_start);
        uintptr_t vaddr_end = PAGE_ALIGN_UP(seg_start + (uintptr_t)ph->p_memsz);
        if (vaddr_end > max_vaddr) max_vaddr = vaddr_end;

        LOG_DEBUG_MSG("ELF: Loading segment %u: vaddr=0x%llx-0x%llx, flags=0x%x\n",
                     i, (unsigned long long)vaddr_start, (unsigned long long)vaddr_end,
                     ph->p_flags);

        for (uintptr_t vaddr = vaddr_start; vaddr < vaddr_end; vaddr += PAGE_SIZE) {
            paddr_t phys = mm::Pmm::alloc_frame();
            if (phys == PADDR_INVALID) {
                LOG_ERROR_MSG("ELF: Failed to allocate page for vaddr 0x%llx\n",
                             (unsigned long long)vaddr);
                return false;
            }
            uint8_t *phys_ptr = (uint8_t *)PHYS_TO_VIRT((uintptr_t)phys);
            memset(phys_ptr, 0, PAGE_SIZE);

            if (!elf_map_user_page(page_dir, vaddr, phys, ph->p_flags)) {
                LOG_ERROR_MSG("ELF: Failed to map page vaddr=0x%llx\n",
                             (unsigned long long)vaddr);
                mm::Pmm::free_frame(phys);
                return false;
            }

            /* 把段在这一页内的文件内容拷进来，其余部分保持为 0（.bss） */
            uintptr_t pg_off = (vaddr >= seg_start) ? 0 : (seg_start - vaddr);
            uintptr_t seg_off = (vaddr >= seg_start) ? (vaddr - seg_start) : 0;
            if (seg_off < seg_filesz) {
                uintptr_t cpy = seg_filesz - seg_off;
                if (cpy > PAGE_SIZE - pg_off) cpy = PAGE_SIZE - pg_off;
                const uint8_t *src = (const uint8_t *)elf_data + (uintptr_t)ph->p_offset + seg_off;
                memcpy(phys_ptr + pg_off, src, cpy);
            }
        }
    }

    *entry_point = (uintptr_t)ehdr->e_entry;
    if (program_end) *program_end = max_vaddr;

    LOG_INFO_MSG("ELF: Load complete, entry=0x%llx, program_end=0x%llx\n",
                (unsigned long long)*entry_point, (unsigned long long)max_vaddr);
    return true;
}

bool kernel::Elf::load(const void *elf_data, uint32_t size, page_directory_t *page_dir,
              uintptr_t *entry_point, uintptr_t *program_end) {
    if (!elf_data || !page_dir || !entry_point) {
        LOG_ERROR_MSG("ELF: Invalid parameters\n");
        return false;
    }
    /* 先完整校验，再开始分配和映射：映射循环里不再有任何未经检查的文件数值 */
    if (!kernel::Elf::validate(elf_data, size)) return false;
    return elf_load_impl(elf_data, page_dir, entry_point, program_end);
}
