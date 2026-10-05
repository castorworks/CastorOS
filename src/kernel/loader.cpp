#include <kernel/loader.h>
#include <kernel/task.h>
#include <kernel/elf.h>

#include <mm/vmm.h>
#include <hal/hal.h>
#include <lib/klog.h>

/* init 的 ELF 映像，由 init_image.S 用 .incbin 嵌入内核 */
extern "C" const uint8_t init_image_start[];
extern "C" const uint8_t init_image_end[];

bool load_init(void) {
    uint32_t size = (uint32_t)(init_image_end - init_image_start);
    if (size == 0) {
        LOG_WARN_MSG("No init image embedded in the kernel\n");
        return false;
    }

    if (!kernel::Elf::validate_header(init_image_start, size)) {
        LOG_ERROR_MSG("init: invalid ELF image (set klog level to DEBUG for the reason)\n");
        return false;
    }

    uintptr_t page_dir_phys = mm::Vmm::create_page_directory();
    if (!page_dir_phys) {
        LOG_ERROR_MSG("init: failed to create address space\n");
        return false;
    }

    uintptr_t entry_point;
    uintptr_t program_end;
    if (!kernel::Elf::load(init_image_start, size, page_dir_phys, &entry_point, &program_end)) {
        LOG_ERROR_MSG("init: failed to load ELF\n");
        mm::Vmm::free_page_directory(page_dir_phys);
        return false;
    }

    uint32_t pid = kernel::Scheduler::create_user_process("init", entry_point, page_dir_phys, program_end);
    if (pid == 0) {
        LOG_ERROR_MSG("init: failed to create process\n");
        mm::Vmm::free_page_directory(page_dir_phys);
        return false;
    }

    // 用户态把 init 的 PID 当作名字服务的固定地址
    if (!kernel::Scheduler::make_init(pid)) {
        LOG_ERROR_MSG("init: cannot assign PID %d\n", INIT_PID);
        return false;
    }

    LOG_INFO_MSG("init loaded (PID %d, %u bytes, entry 0x%llx)\n",
                 INIT_PID, size, (unsigned long long)entry_point);
    return true;
}
