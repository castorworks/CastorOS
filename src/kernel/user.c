// ============================================================================
// user.c - 用户模式支持
// ============================================================================

#include <kernel/user.h>
#include <kernel/task.h>
#include <kernel/gdt.h>
#include <mm/vmm.h>
#include <lib/klog.h>

extern void enter_usermode(uint32_t entry_point, uint32_t user_stack);

void task_enter_usermode(uint32_t entry_point, uint32_t user_stack)
{
    task_t *current = task_get_current();

    tss_set_kernel_stack(current->kernel_stack);

    if (current->page_dir_phys != vmm_get_page_directory()) {
        vmm_switch_page_directory(current->page_dir_phys);
    }

    enter_usermode(entry_point, user_stack);

    for (;;) ;
}
