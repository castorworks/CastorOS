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

bool elf_is_64bit(const void *elf_data) {
    if (!elf_data) return false;
    const uint8_t *ident = (const uint8_t *)elf_data;
    return ident[4] == ELF_CLASS_64;
}

bool elf_validate_header(const void *elf_data) {
    if (!elf_data) return false;
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
#if defined(ARCH_X86_64)
    if (ident[4] != ELF_CLASS_64) {
        LOG_ERROR_MSG("ELF: Expected 64-bit ELF for x86_64\n");
        return false;
    }
    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)elf_data;
    if (ehdr->e_type != ET_EXEC) {
        LOG_ERROR_MSG("ELF: Not executable (type=%d)\n", ehdr->e_type);
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
    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)elf_data;
    if (ehdr->e_type != ET_EXEC) {
        LOG_ERROR_MSG("ELF: Not executable (type=%d)\n", ehdr->e_type);
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
    const elf32_ehdr_t *ehdr = (const elf32_ehdr_t *)elf_data;
    if (ehdr->e_type != ET_EXEC) {
        LOG_ERROR_MSG("ELF: Not executable (type=%d)\n", ehdr->e_type);
        return false;
    }
    if (ehdr->e_machine != EM_386) {
        LOG_ERROR_MSG("ELF: Not i386 (machine=%d)\n", ehdr->e_machine);
        return false;
    }
#endif
    return true;
}

uintptr_t elf_get_entry(const void *elf_data) {
    if (!elf_validate_header(elf_data)) return 0;
#if defined(ARCH_X86_64) || defined(ARCH_ARM64)
    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)elf_data;
    return (uintptr_t)ehdr->e_entry;
#else
    const elf32_ehdr_t *ehdr = (const elf32_ehdr_t *)elf_data;
    return (uintptr_t)ehdr->e_entry;
#endif
}

#if defined(ARCH_X86_64)
static bool elf_load_impl(const void *elf_data, uint32_t size, page_directory_t *page_dir,
                          uintptr_t *entry_point, uintptr_t *program_end) {
    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)elf_data;
    const elf64_phdr_t *phdr = (const elf64_phdr_t *)((uint8_t *)elf_data + ehdr->e_phoff);
    LOG_INFO_MSG("ELF: Loading executable\n");
    LOG_INFO_MSG("  Entry point: 0x%llx\n", (unsigned long long)ehdr->e_entry);
    LOG_INFO_MSG("  Program headers: %u\n", ehdr->e_phnum);
    uintptr_t page_dir_phys = VIRT_TO_PHYS((uintptr_t)page_dir);
    uintptr_t max_vaddr = 0;
    for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
        const elf64_phdr_t *ph = &phdr[i];
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_offset + ph->p_filesz > size) {
            LOG_ERROR_MSG("ELF: Segment exceeds file size\n");
            return false;
        }
        if (ph->p_vaddr >= KERNEL_VIRTUAL_BASE) {
            LOG_ERROR_MSG("ELF: Segment in kernel space\n");
            return false;
        }
        uint32_t flags = PAGE_PRESENT | PAGE_USER;
        if (ph->p_flags & PF_W) flags |= PAGE_WRITE;
        if (ph->p_flags & PF_X) flags |= PAGE_EXEC;
        uintptr_t vaddr_start = PAGE_ALIGN_DOWN(ph->p_vaddr);
        uintptr_t vaddr_end = PAGE_ALIGN_UP(ph->p_vaddr + ph->p_memsz);
        uint32_t num_pages = (vaddr_end - vaddr_start) / PAGE_SIZE;
        if (vaddr_end > max_vaddr) max_vaddr = vaddr_end;
        for (uint32_t pg = 0; pg < num_pages; pg++) {
            uintptr_t vaddr = vaddr_start + pg * PAGE_SIZE;
            paddr_t phys = pmm_alloc_frame();
            if (phys == PADDR_INVALID) {
                LOG_ERROR_MSG("ELF: Failed to allocate page\n");
                return false;
            }
            uint8_t *phys_ptr = (uint8_t *)PHYS_TO_VIRT(phys);
            memset(phys_ptr, 0, PAGE_SIZE);
            if (!vmm_map_page_in_directory(page_dir_phys, vaddr, (uintptr_t)phys, flags)) {
                LOG_ERROR_MSG("ELF: Failed to map page\n");
                pmm_free_frame(phys);
                return false;
            }
            uint64_t pg_off = (vaddr >= ph->p_vaddr) ? 0 : (ph->p_vaddr - vaddr);
            uint64_t seg_off = (vaddr >= ph->p_vaddr) ? (vaddr - ph->p_vaddr) : 0;
            if (seg_off < ph->p_filesz) {
                uint64_t cpy = ph->p_filesz - seg_off;
                if (cpy > PAGE_SIZE - pg_off) cpy = PAGE_SIZE - pg_off;
                const uint8_t *src = (const uint8_t *)elf_data + ph->p_offset + seg_off;
                memcpy(phys_ptr + pg_off, src, cpy);
            }
        }
    }
    *entry_point = (uintptr_t)ehdr->e_entry;
    if (program_end) *program_end = max_vaddr;
    LOG_INFO_MSG("ELF: Load complete, entry=0x%llx\n", (unsigned long long)*entry_point);
    return true;
}
#elif defined(ARCH_ARM64)
/**
 * ARM64 ELF loader implementation
 * Uses HAL MMU interface for page mapping instead of x86-specific VMM functions
 * 
 * @param elf_data ELF file data
 * @param size ELF file size
 * @param page_dir Address space handle (TTBR0 physical address cast to page_directory_t*)
 * @param entry_point Output: program entry point
 * @param program_end Output: highest loaded address
 * @return true on success
 */
static bool elf_load_impl(const void *elf_data, uint32_t size, page_directory_t *page_dir,
                          uintptr_t *entry_point, uintptr_t *program_end) {
    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)elf_data;
    const elf64_phdr_t *phdr = (const elf64_phdr_t *)((uint8_t *)elf_data + ehdr->e_phoff);
    
    LOG_INFO_MSG("ELF: Loading ARM64 executable\n");
    LOG_INFO_MSG("  Entry point: 0x%llx\n", (unsigned long long)ehdr->e_entry);
    LOG_INFO_MSG("  Program headers: %u\n", ehdr->e_phnum);
    
    /* On ARM64, page_dir is actually the address space handle (TTBR0 physical address) */
    hal_addr_space_t addr_space = (hal_addr_space_t)(uintptr_t)page_dir;
    
    uintptr_t max_vaddr = 0;
    
    for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
        const elf64_phdr_t *ph = &phdr[i];
        
        /* Only load PT_LOAD segments */
        if (ph->p_type != PT_LOAD) continue;
        
        /* Validate segment bounds */
        if (ph->p_offset + ph->p_filesz > size) {
            LOG_ERROR_MSG("ELF: Segment exceeds file size\n");
            return false;
        }
        
        /* Ensure segment is in user space (below kernel space) */
        if (ph->p_vaddr >= USER_SPACE_END) {
            LOG_ERROR_MSG("ELF: Segment in kernel space (vaddr=0x%llx)\n", 
                         (unsigned long long)ph->p_vaddr);
            return false;
        }
        
        /* Convert ELF flags to HAL page flags */
        uint32_t flags = HAL_PAGE_PRESENT | HAL_PAGE_USER;
        if (ph->p_flags & PF_W) flags |= HAL_PAGE_WRITE;
        if (ph->p_flags & PF_X) flags |= HAL_PAGE_EXEC;
        
        /* Calculate page-aligned bounds */
        uintptr_t vaddr_start = PAGE_ALIGN_DOWN(ph->p_vaddr);
        uintptr_t vaddr_end = PAGE_ALIGN_UP(ph->p_vaddr + ph->p_memsz);
        uint32_t num_pages = (vaddr_end - vaddr_start) / PAGE_SIZE;
        
        if (vaddr_end > max_vaddr) max_vaddr = vaddr_end;
        
        LOG_DEBUG_MSG("ELF: Loading segment %u: vaddr=0x%llx-0x%llx, %u pages, flags=0x%x\n",
                     i, (unsigned long long)vaddr_start, (unsigned long long)vaddr_end,
                     num_pages, flags);
        
        /* Map and load each page */
        for (uint32_t pg = 0; pg < num_pages; pg++) {
            uintptr_t vaddr = vaddr_start + pg * PAGE_SIZE;
            
            /* Allocate physical frame */
            paddr_t phys = pmm_alloc_frame();
            if (phys == PADDR_INVALID) {
                LOG_ERROR_MSG("ELF: Failed to allocate page for vaddr 0x%llx\n",
                             (unsigned long long)vaddr);
                return false;
            }
            
            /* Zero the page first */
            uint8_t *phys_ptr = (uint8_t *)PHYS_TO_VIRT(phys);
            memset(phys_ptr, 0, PAGE_SIZE);
            
            /* Map the page using HAL MMU interface */
            if (!hal_mmu_map(addr_space, vaddr, phys, flags)) {
                LOG_ERROR_MSG("ELF: Failed to map page vaddr=0x%llx phys=0x%llx\n",
                             (unsigned long long)vaddr, (unsigned long long)phys);
                pmm_free_frame(phys);
                return false;
            }
            
            /* Copy segment data to the page */
            uint64_t pg_off = (vaddr >= ph->p_vaddr) ? 0 : (ph->p_vaddr - vaddr);
            uint64_t seg_off = (vaddr >= ph->p_vaddr) ? (vaddr - ph->p_vaddr) : 0;
            
            if (seg_off < ph->p_filesz) {
                uint64_t cpy = ph->p_filesz - seg_off;
                if (cpy > PAGE_SIZE - pg_off) cpy = PAGE_SIZE - pg_off;
                const uint8_t *src = (const uint8_t *)elf_data + ph->p_offset + seg_off;
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
#else
static bool elf_load_impl(const void *elf_data, uint32_t size, page_directory_t *page_dir,
                          uintptr_t *entry_point, uintptr_t *program_end) {
    const elf32_ehdr_t *ehdr = (const elf32_ehdr_t *)elf_data;
    const elf32_phdr_t *phdr = (const elf32_phdr_t *)((uint8_t *)elf_data + ehdr->e_phoff);
    LOG_INFO_MSG("ELF: Loading executable\n");
    LOG_INFO_MSG("  Entry point: 0x%x\n", ehdr->e_entry);
    LOG_INFO_MSG("  Program headers: %u\n", ehdr->e_phnum);
    uint32_t page_dir_phys = VIRT_TO_PHYS((uint32_t)page_dir);
    uint32_t max_vaddr = 0;
    for (uint32_t i = 0; i < ehdr->e_phnum; i++) {
        const elf32_phdr_t *ph = &phdr[i];
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_offset + ph->p_filesz > size) {
            LOG_ERROR_MSG("ELF: Segment exceeds file size\n");
            return false;
        }
        if (ph->p_vaddr >= KERNEL_VIRTUAL_BASE) {
            LOG_ERROR_MSG("ELF: Segment in kernel space\n");
            return false;
        }
        uint32_t flags = PAGE_PRESENT | PAGE_USER;
        if (ph->p_flags & PF_W) flags |= PAGE_WRITE;
        if (ph->p_flags & PF_X) flags |= PAGE_EXEC;
        uint32_t vaddr_start = PAGE_ALIGN_DOWN(ph->p_vaddr);
        uint32_t vaddr_end = PAGE_ALIGN_UP(ph->p_vaddr + ph->p_memsz);
        uint32_t num_pages = (vaddr_end - vaddr_start) / PAGE_SIZE;
        if (vaddr_end > max_vaddr) max_vaddr = vaddr_end;
        for (uint32_t pg = 0; pg < num_pages; pg++) {
            uint32_t vaddr = vaddr_start + pg * PAGE_SIZE;
            paddr_t phys = pmm_alloc_frame();
            if (phys == PADDR_INVALID) {
                LOG_ERROR_MSG("ELF: Failed to allocate page\n");
                return false;
            }
            uint8_t *phys_ptr = (uint8_t *)PHYS_TO_VIRT((uintptr_t)phys);
            memset(phys_ptr, 0, PAGE_SIZE);
            if (!vmm_map_page_in_directory(page_dir_phys, vaddr, (uintptr_t)phys, flags)) {
                LOG_ERROR_MSG("ELF: Failed to map page\n");
                pmm_free_frame(phys);
                return false;
            }
            uint32_t pg_off = (vaddr >= ph->p_vaddr) ? 0 : (ph->p_vaddr - vaddr);
            uint32_t seg_off = (vaddr >= ph->p_vaddr) ? (vaddr - ph->p_vaddr) : 0;
            if (seg_off < ph->p_filesz) {
                uint32_t cpy = ph->p_filesz - seg_off;
                if (cpy > PAGE_SIZE - pg_off) cpy = PAGE_SIZE - pg_off;
                const uint8_t *src = (const uint8_t *)elf_data + ph->p_offset + seg_off;
                memcpy(phys_ptr + pg_off, src, cpy);
            }
        }
    }
    *entry_point = ehdr->e_entry;
    if (program_end) *program_end = max_vaddr;
    LOG_INFO_MSG("ELF: Load complete, entry=0x%x\n", *entry_point);
    return true;
}
#endif

bool elf_load(const void *elf_data, uint32_t size, page_directory_t *page_dir,
              uintptr_t *entry_point, uintptr_t *program_end) {
    if (!elf_data || !page_dir || !entry_point) {
        LOG_ERROR_MSG("ELF: Invalid parameters\n");
        return false;
    }
    if (!elf_validate_header(elf_data)) return false;
    return elf_load_impl(elf_data, size, page_dir, entry_point, program_end);
}
