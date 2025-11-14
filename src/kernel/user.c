// ============================================================================
// user.c - 用户模式支持
// ============================================================================

#include <kernel/user.h>
#include <kernel/task.h>
#include <kernel/tss.h>
#include <kernel/gdt.h>
#include <mm/vmm.h>
#include <lib/klog.h>

/**
 * 用户模式包装器 - 用于首次进入用户进程
 * 这个函数在内核态运行，负责切换到用户模式
 */
static void usermode_wrapper(void) {
    task_t *current = task_get_current();
    
    LOG_DEBUG_MSG("Usermode wrapper: PID=%u, entry=%x, stack=%x\n",
                 current->pid, current->user_entry, current->user_stack);
    
    /* 更新 TSS 的内核栈指针 */
    tss_set_kernel_stack(current->kernel_stack);
    
    /* 确保切换到正确的页目录（用户进程有独立地址空间） */
    if (current->page_dir_phys != vmm_get_page_directory()) {
        vmm_switch_page_directory(current->page_dir_phys);
    }
    
    /* 调用汇编代码切换到用户模式 */
    /* 注意：这个函数不会返回！ */
    enter_usermode(current->user_entry, current->user_stack);
    
    /* 永远不会执行到这里 */
    for(;;);
}

/**
 * 获取用户模式包装器的地址
 */
uint32_t get_usermode_wrapper(void) {
    return (uint32_t)usermode_wrapper;
}

/**
 * 进入用户模式（C 包装器）
 */
void task_enter_usermode(uint32_t entry_point, uint32_t user_stack) {
    task_t *current = task_get_current();
    
    LOG_DEBUG_MSG("Entering user mode: entry=%x, stack=%x\n", 
                 entry_point, user_stack);
    
    /* 更新 TSS 的内核栈指针 */
    /* 当用户程序发生中断/系统调用时，CPU 会从 TSS 加载内核栈 */
    tss_set_kernel_stack(current->kernel_stack);
    
    /* 确保切换到正确的页目录（用户进程有独立地址空间） */
    if (current->page_dir_phys != vmm_get_page_directory()) {
        vmm_switch_page_directory(current->page_dir_phys);
    }
    
    /* 调用汇编代码切换到用户模式 */
    /* 注意：这个函数不会返回！ */
    enter_usermode(entry_point, user_stack);
    
    /* 永远不会执行到这里 */
    for(;;);
}
