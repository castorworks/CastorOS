/**
 * @file vmm.c
 * @brief 虚拟内存管理器实现
 * 
 * 实现分页机制，管理虚拟地址到物理地址的映射
 * 核心逻辑保持架构无关，通过 HAL 接口和 pgtable 抽象层调用架构特定操作
 */

#include <kernel/smp.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>
#include <kernel/sync/spinlock.h>
#include <kernel/task.h>
#include <hal/hal.h>
#include <hal/hal_error.h>

/**
 * 每个 CPU 现在用的是哪张页表（顶层表的物理地址）。下面的 current_dir_phys 指"这个 CPU 的"。
 * 所有 CPU 都从内核自己的页表（kernel_dir_phys）开始。
 */
static uintptr_t current_dir_per_cpu[MAX_CPUS];
#define current_dir_phys (current_dir_per_cpu[hal::Cpu::id()])
static uintptr_t kernel_dir_phys = 0;           ///< 内核自己的页表：idle 任务用它


static sync::Spinlock vmm_lock;                    ///< VMM 自旋锁，保护页表操作

/**
 * @brief 初始化虚拟内存管理器
 * 
 * 使用引导时创建的页目录，设置CR3寄存器
 * 扩展高半核映射以覆盖所有可用的物理内存
 */
void mm::Vmm::init() {
    vmm_lock.init();

    // 引导页表只映射了一部分物理内存：让各架构把内核的直接映射扩展到全部
    hal::Mmu::map_physical_memory();

    kernel_dir_phys = hal::Mmu::get_current_page_table();
    for (uint32_t cpu = 0; cpu < MAX_CPUS; cpu++) {
        current_dir_per_cpu[cpu] = kernel_dir_phys;
    }
    LOG_INFO_MSG("VMM: kernel page table at phys 0x%llx\n", (unsigned long long)current_dir_phys);
}


/**
 * @brief 处理内核空间缺页异常（同步内核页目录）
 * @param addr 缺页地址
 * @return 是否成功处理
 */
bool mm::Vmm::handle_kernel_page_fault(uintptr_t addr) {
    // 只有内核半区按地址空间各有一份顶层表项的架构（i686）才需要同步
    return hal::Mmu::sync_kernel_mapping((vaddr_t)addr);
}

/**
 * @brief 处理写保护异常（COW）- 统一实现
 * @param addr 缺页地址
 * @param error_code 错误码
 * @return 是否成功处理
 * 
 * 使用 HAL 接口实现架构无关的 COW 处理。
 * 
 * x86 Page Fault Error Code:
 *   Bit 0 (P): 1 = 页面存在，0 = 页面不存在
 *   Bit 1 (W): 1 = 写操作，0 = 读操作
 *   Bit 2 (U): 1 = 用户模式，0 = 内核模式
 *   Bit 3 (RSVD): 1 = 保留位被设置
 *   Bit 4 (I/D): 1 = 指令获取导致
 * 
 * COW 异常特征：页面存在(P=1) + 写操作(W=1) + 页面有 PAGE_COW 标志
 */
bool mm::Vmm::handle_cow_page_fault(uintptr_t addr, uint32_t error_code) {
    /* 统一的 COW 处理逻辑，使用 HAL 接口实现架构无关 */
    
    // error_code bit 1: 写入导致的异常
    // error_code bit 0: 页面存在
    // COW 异常应该是：页面存在(bit0=1) + 写入(bit1=1) = 0x3 或 0x7
    if ((error_code & 0x3) != 0x3) {
        LOG_DEBUG_MSG("COW: Not a COW fault - addr=0x%lx, error=0x%x\n", 
                     (unsigned long)addr, error_code);
        return false;  // 不是写保护异常
    }
    
    bool irq_state;
    vmm_lock.lock_irqsave(irq_state);
    
    // 使用 HAL 接口查询页面映射
    paddr_t old_frame;
    uint32_t hal_flags;
    if (!hal::Mmu::query(HAL_ADDR_SPACE_CURRENT, (vaddr_t)addr, &old_frame, &hal_flags)) {
        LOG_DEBUG_MSG("COW: Page not mapped - addr=0x%lx\n", (unsigned long)addr);
        vmm_lock.unlock_irqrestore(irq_state);
        return false;
    }
    
    // 检查页面是否标记为 COW
    if (!(hal_flags & HAL_PAGE_COW)) {
        LOG_DEBUG_MSG("COW: Not a COW page - addr=0x%lx, flags=0x%x\n",
                     (unsigned long)addr, hal_flags);
        vmm_lock.unlock_irqrestore(irq_state);
        return false;
    }
    
    // 【安全检查】验证旧物理帧地址
    if (old_frame == PADDR_INVALID || old_frame == 0) {
        LOG_ERROR_MSG("COW: Invalid old frame address 0x%llx at addr=0x%lx\n", 
                     (unsigned long long)old_frame, (unsigned long)addr);
        vmm_lock.unlock_irqrestore(irq_state);
        return false;
    }
    
    uint32_t refcount = mm::Pmm::frame_get_refcount(old_frame);
    
    LOG_DEBUG_MSG("COW: Handling page fault - addr=0x%lx, old_frame=0x%llx, refcount=%u\n", 
                (unsigned long)addr, (unsigned long long)old_frame, refcount);
    
    if (refcount == 0) {
        // 异常情况：COW 页面但引用计数为 0
        // 这不应该发生，但为了安全，我们恢复写权限并继续
        LOG_WARN_MSG("COW: Page at 0x%lx has refcount=0 but is marked COW, restoring write\n", 
                    (unsigned long)addr);
        hal::Mmu::protect(HAL_ADDR_SPACE_CURRENT, (vaddr_t)addr, 
                       HAL_PAGE_WRITE, HAL_PAGE_COW);
        hal::Mmu::flush_tlb((vaddr_t)addr);
        vmm_lock.unlock_irqrestore(irq_state);
        return true;
    }
    
    if (refcount == 1) {
        // 只有当前进程引用，直接恢复写权限，无需复制
        hal::Mmu::protect(HAL_ADDR_SPACE_CURRENT, (vaddr_t)addr, 
                       HAL_PAGE_WRITE, HAL_PAGE_COW);
        hal::Mmu::flush_tlb((vaddr_t)addr);
        vmm_lock.unlock_irqrestore(irq_state);
        LOG_DEBUG_MSG("COW: Single reference (refcount=1), restored write permission\n");
        return true;
    }
    
    // 多个进程共享（refcount > 1），需要复制页面
    paddr_t new_frame = mm::Pmm::alloc_frame();
    if (new_frame == PADDR_INVALID) {
        vmm_lock.unlock_irqrestore(irq_state);
        LOG_ERROR_MSG("COW: Failed to allocate frame for COW copy (out of memory)\n");
        return false;
    }
    
    // 复制页面内容
    void *src_virt = (void*)PHYS_TO_VIRT((uintptr_t)old_frame);
    void *dst_virt = (void*)PHYS_TO_VIRT((uintptr_t)new_frame);
    memcpy(dst_virt, src_virt, PAGE_SIZE);
    
    // 计算新的标志：去掉 COW，恢复写权限
    uint32_t new_flags = (hal_flags & ~HAL_PAGE_COW) | HAL_PAGE_WRITE;
    
    // 页对齐地址
    vaddr_t page_addr = (vaddr_t)(addr & ~(PAGE_SIZE - 1));
    
    // 取消旧映射并创建新映射
    hal::Mmu::unmap(HAL_ADDR_SPACE_CURRENT, page_addr);
    hal::Mmu::map(HAL_ADDR_SPACE_CURRENT, page_addr, new_frame, new_flags);
    
    // 刷新 TLB
    hal::Mmu::flush_tlb((vaddr_t)addr);
    
    // 减少旧页面的引用计数
    mm::Pmm::frame_ref_dec(old_frame);
    
    vmm_lock.unlock_irqrestore(irq_state);
    
    LOG_DEBUG_MSG("COW: Copied page (refcount was %u), old_frame=0x%llx -> new_frame=0x%llx\n", 
                 refcount, (unsigned long long)old_frame, (unsigned long long)new_frame);
    return true;
}

/**
 * @brief 将 VMM 页标志转换为 HAL 页标志
 * @param vmm_flags VMM 页标志 (PAGE_*)
 * @return HAL 页标志 (HAL_PAGE_*)
 */
static uint32_t vmm_flags_to_hal(uint32_t vmm_flags) {
    uint32_t hal_flags = 0;
    
    if (vmm_flags & PAGE_PRESENT)       hal_flags |= HAL_PAGE_PRESENT;
    if (vmm_flags & PAGE_WRITE)         hal_flags |= HAL_PAGE_WRITE;
    if (vmm_flags & PAGE_USER)          hal_flags |= HAL_PAGE_USER;
    if (vmm_flags & PAGE_CACHE_DISABLE) hal_flags |= HAL_PAGE_NOCACHE;
    if (vmm_flags & PAGE_COW)           hal_flags |= HAL_PAGE_COW;
    if (vmm_flags & PAGE_SHARED)        hal_flags |= HAL_PAGE_SHARED;
    if (vmm_flags & PAGE_EXEC)          hal_flags |= HAL_PAGE_EXEC;
    
    return hal_flags;
}

/**
 * @brief 用户可访问的映射只能建在用户地址范围内
 *
 * 内核半区的页表由所有地址空间共享，在那里建立带 PAGE_USER 的映射会改写
 * 每个进程的内核映射并把它暴露给用户态。
 */
static bool user_mapping_allowed(uintptr_t virt, uint32_t flags) {
    if ((flags & PAGE_USER) && virt >= VMM_USER_VADDR_END) {
        LOG_ERROR_MSG("VMM: refusing user mapping at kernel address 0x%llx\n",
                      (unsigned long long)virt);
        return false;
    }
    return true;
}

/* ============================================================================
 * 错误码转换函数实现
 * 
 * 提供 HAL 错误码与 VMM 错误码之间的转换，确保错误处理一致性。
 * 
 * ========================================================================== */

/**
 * @brief 映射虚拟页到物理页
 * @param virt 虚拟地址（页对齐）
 * @param phys 物理地址（页对齐）
 * @param flags 页标志（PAGE_PRESENT, PAGE_WRITE, PAGE_USER）
 * @return 成功返回 true，失败返回 false
 * 
 * 使用 HAL MMU 接口实现跨架构页面映射
 */
bool mm::Vmm::map_page(uintptr_t virt, uintptr_t phys, uint32_t flags) {
    // 检查页对齐
    if ((virt | phys) & (PAGE_SIZE-1)) return false;
    if (!user_mapping_allowed(virt, flags)) return false;
    
    sync::SpinlockIrqGuard guard(vmm_lock);
    
    // 转换为 HAL 标志
    uint32_t hal_flags = vmm_flags_to_hal(flags);
    
    // 使用 HAL 接口映射页面
    bool result = hal::Mmu::map(HAL_ADDR_SPACE_CURRENT, (vaddr_t)virt, (paddr_t)phys, hal_flags);
    
    if (result) {
        // 刷新 TLB
        hal::Mmu::flush_tlb((vaddr_t)virt);
    }
    
    return result;
}

/**
 * @brief 获取当前页目录的物理地址
 * @return 页目录的物理地址
 */
uintptr_t mm::Vmm::get_page_directory() {
    return current_dir_phys;
}

uintptr_t mm::Vmm::kernel_page_directory() {
    return kernel_dir_phys;
}

/**
 * @brief 创建新的页目录（用于新进程）
 * @return 成功返回页目录的物理地址，失败返回 0
 * 
 * 使用 HAL MMU 接口实现跨架构地址空间创建
 */
uintptr_t mm::Vmm::create_page_directory() {
    bool irq_state;
    vmm_lock.lock_irqsave(irq_state);
    
    // 使用 HAL 接口创建新地址空间
    hal_addr_space_t new_space = hal::Mmu::create_space();
    
    if (new_space == HAL_ADDR_SPACE_INVALID) {
        vmm_lock.unlock_irqrestore(irq_state);
        return 0;
    }
    
    vmm_lock.unlock_irqrestore(irq_state);
    return (uintptr_t)new_space;
}

/**
 * @brief 克隆页目录（用于 fork）
 * @param src_dir_phys 源页目录的物理地址
 * @return 成功返回新页目录的物理地址，失败返回 0
 * 
 * 实现 Copy-on-Write (COW) 语义：
 * - 父子进程共享物理页，但页表是独立的
 * - 共享的可写页面被标记为只读 + COW
 * - 首次写入时触发 page fault，由 mm::Vmm::handle_cow_page_fault 处理
 * 
 * 使用 HAL MMU 接口实现跨架构地址空间克隆
 */
uintptr_t mm::Vmm::clone_page_directory(uintptr_t src_dir_phys) {
    hal_addr_space_t new_space;
    {
        sync::SpinlockIrqGuard guard(vmm_lock);
        hal_addr_space_t src_space = (src_dir_phys == 0) 
                                     ? HAL_ADDR_SPACE_CURRENT 
                                     : (hal_addr_space_t)src_dir_phys;
    
        new_space = hal::Mmu::clone_space(src_space);
    }
    
    if (new_space == HAL_ADDR_SPACE_INVALID) {
        return 0;
    }
    
    return (uintptr_t)new_space;
}


/**
 * @brief 释放页目录及其用户空间页表和物理页
 * @param dir_phys 页目录的物理地址
 * 
 * 物理页由引用计数管理：和别的地址空间共享的页只减计数
 * 
 * 使用 HAL MMU 接口实现跨架构地址空间销毁
 */
void mm::Vmm::free_page_directory(uintptr_t dir_phys) {
    if (!dir_phys) return;

    // 【安全检查】防止释放正在使用的页目录（任何一个 CPU 上）
    for (uint32_t cpu = 0; cpu < MAX_CPUS; cpu++) {
        if (dir_phys == current_dir_per_cpu[cpu]) {
            LOG_ERROR_MSG("mm::Vmm::free_page_directory: BLOCKED! Page directory 0x%llx is in use on CPU %u!\n",
                         (unsigned long long)dir_phys, cpu);
            return;
        }
    }
    
    {
        sync::SpinlockIrqGuard guard(vmm_lock);
        hal::Mmu::destroy_space((hal_addr_space_t)dir_phys);
    }
}

/**
 * @brief 同步 VMM 的 current_dir_phys（不切换 CR3）
 * @param dir_phys 当前页目录的物理地址
 * 
 * 仅更新内部状态变量，不修改 CR3 寄存器
 * 用于在 task_switch_context 已经切换 CR3 后同步状态
 */
void mm::Vmm::sync_current_dir(uintptr_t dir_phys) {
    if (!dir_phys) return;
    
    sync::SpinlockIrqGuard guard(vmm_lock);
    
    current_dir_phys = dir_phys;
}

/**
 * @brief 切换到指定的页目录
 * @param dir_phys 页目录的物理地址
 * 
 * 通过 HAL 接口切换地址空间
 */
void mm::Vmm::switch_page_directory(uintptr_t dir_phys) {
    if (!dir_phys) return;
    
    sync::SpinlockIrqGuard guard(vmm_lock);
    
    current_dir_phys = dir_phys;
    
    // 通过 HAL 接口切换地址空间
    hal::Mmu::switch_space(dir_phys);
}

/**
 * @brief 在指定页目录中映射页面
 * @param dir_phys 页目录的物理地址
 * @param virt 虚拟地址
 * @param phys 物理地址
 * @param flags 页标志
 * @return 成功返回 true，失败返回 false
 * 
 * 使用 HAL MMU 接口实现跨架构页面映射
 */
bool mm::Vmm::map_page_in_directory(uintptr_t dir_phys, uintptr_t virt, 
                                uintptr_t phys, uint32_t flags) {
    // 检查页对齐
    if ((virt | phys) & (PAGE_SIZE-1)) return false;
    if (!user_mapping_allowed(virt, flags)) return false;
    
    bool result;
    {
        sync::SpinlockIrqGuard guard(vmm_lock);
        // 转换为 HAL 标志
        uint32_t hal_flags = vmm_flags_to_hal(flags);
    
        // 使用 HAL 接口映射页面
        hal_addr_space_t space = (dir_phys == current_dir_phys) 
                                 ? HAL_ADDR_SPACE_CURRENT 
                                 : (hal_addr_space_t)dir_phys;
    
        result = hal::Mmu::map(space, (vaddr_t)virt, (paddr_t)phys, hal_flags);
    
        // 如果是当前页目录，刷新 TLB
        if (result && dir_phys == current_dir_phys) {
            hal::Mmu::flush_tlb((vaddr_t)virt);
        }
    }
    return result;
}

uintptr_t mm::Vmm::unmap_page_in_directory(uintptr_t dir_phys, uintptr_t virt) {
    if (virt & (PAGE_SIZE - 1)) {
        return 0;
    }

    paddr_t old_phys;
    {
        sync::SpinlockIrqGuard guard(vmm_lock);
        hal_addr_space_t space = (dir_phys == current_dir_phys) 
                                 ? HAL_ADDR_SPACE_CURRENT 
                                 : (hal_addr_space_t)dir_phys;
    
        // 以 unmap 的结果为准：query 对大页/块映射也会成功，但 unmap 不会
        // 拆除它们，此时不能把物理地址交给调用者去释放
        old_phys = hal::Mmu::unmap(space, (vaddr_t)virt);
        if (old_phys == PADDR_INVALID) {
            return 0;
        }

        // 如果是当前页目录，刷新 TLB
        if (dir_phys == current_dir_phys) {
            hal::Mmu::flush_tlb((vaddr_t)virt);
        }
    }
    return (uintptr_t)old_phys;
}
