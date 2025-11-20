#include <kernel/loader.h>
#include <kernel/task.h>
#include <kernel/elf.h>

#include <mm/heap.h>
#include <mm/vmm.h>

#include <fs/vfs.h>
#include <lib/klog.h>

/**
 * 从文件系统加载并启动用户态 shell
 */
bool load_user_shell(void) {
    // 查找 shell.elf
    const char *shell_path = "/bin/shell.elf";
    fs_node_t *shell_file = vfs_path_to_node(shell_path);
    
    if (!shell_file) {
        LOG_WARN_MSG("Shell not found: %s\n", shell_path);
        LOG_WARN_MSG("Skipping shell load. System will run in kernel mode only.\n");
        return false;
    }
    
    LOG_INFO_MSG("Found shell: %s (size: %u bytes)\n", shell_path, shell_file->size);
    
    // 检查文件大小
    if (shell_file->size == 0 || shell_file->size > 16 * 1024 * 1024) {
        LOG_ERROR_MSG("Invalid shell file size: %u\n", shell_file->size);
        return false;
    }
    
    // 读取 ELF 文件到内存
    uint8_t *elf_data = (uint8_t *)kmalloc(shell_file->size);
    if (!elf_data) {
        LOG_ERROR_MSG("Failed to allocate memory for shell\n");
        return false;
    }
    
    uint32_t read_bytes = vfs_read(shell_file, 0, shell_file->size, elf_data);
    if (read_bytes != shell_file->size) {
        LOG_ERROR_MSG("Failed to read shell file (got %u/%u bytes)\n", 
                     read_bytes, shell_file->size);
        kfree(elf_data);
        return false;
    }
    
    // 验证 ELF 头
    if (!elf_validate_header(elf_data)) {
        LOG_ERROR_MSG("Invalid ELF file\n");
        kfree(elf_data);
        return false;
    }
    
    // 创建页目录
    uint32_t page_dir_phys = vmm_create_page_directory();
    if (!page_dir_phys) {
        LOG_ERROR_MSG("Failed to create page directory\n");
        kfree(elf_data);
        return false;
    }
    
    page_directory_t *page_dir = (page_directory_t*)PHYS_TO_VIRT(page_dir_phys);
    
    // 加载 ELF
    uint32_t entry_point;
    if (!elf_load(elf_data, shell_file->size, page_dir, &entry_point)) {
        LOG_ERROR_MSG("Failed to load ELF\n");
        vmm_free_page_directory(page_dir_phys);
        kfree(elf_data);
        return false;
    }
    
    kfree(elf_data);
    
    // 创建用户进程
    uint32_t pid = task_create_user_process("shell", entry_point, page_dir);
    if (pid == 0) {
        LOG_ERROR_MSG("Failed to create shell process\n");
        vmm_free_page_directory(page_dir_phys);
        return false;
    }
    
    LOG_INFO_MSG("Shell loaded successfully!\n");
    LOG_INFO_MSG("  Process: shell (PID %u)\n", pid);
    LOG_INFO_MSG("  Entry point: %x\n", entry_point);
    LOG_INFO_MSG("================================ User Shell Ready ==============================\n");

    return true;
}
