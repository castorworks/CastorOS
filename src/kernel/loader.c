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
    
    uint32_t shell_size = shell_file->size;  // 保存大小，避免释放后访问
    
    LOG_INFO_MSG("Found shell: %s (size: %u bytes)\n", shell_path, shell_size);
    
    // 检查文件大小
    if (shell_size == 0 || shell_size > 16 * 1024 * 1024) {
        LOG_ERROR_MSG("Invalid shell file size: %u\n", shell_size);
        vfs_release_node(shell_file);  // 释放节点
        return false;
    }
    
    // 读取 ELF 文件到内存
    uint8_t *elf_data = (uint8_t *)kmalloc(shell_size);
    if (!elf_data) {
        LOG_ERROR_MSG("Failed to allocate memory for shell\n");
        vfs_release_node(shell_file);  // 释放节点
        return false;
    }
    
    uint32_t read_bytes = vfs_read(shell_file, 0, shell_size, elf_data);
    if (read_bytes != shell_size) {
        LOG_ERROR_MSG("Failed to read shell file (got %u/%u bytes)\n", 
                     read_bytes, shell_size);
        kfree(elf_data);
        vfs_release_node(shell_file);  // 释放节点
        return false;
    }
    
    // 文件已读取，立即释放节点
    vfs_release_node(shell_file);
    
    LOG_DEBUG_MSG("Shell: ELF data loaded at %p, size=%u\n", elf_data, shell_size);
    
    // 验证 ELF 头
    if (!elf_validate_header(elf_data)) {
        LOG_ERROR_MSG("Invalid ELF file\n");
        kfree(elf_data);
        return false;
    }
    
    LOG_DEBUG_MSG("Shell: ELF header validated\n");
    
    // 创建页目录
    LOG_DEBUG_MSG("Shell: Creating page directory...\n");
    uint32_t page_dir_phys = vmm_create_page_directory();
    if (!page_dir_phys) {
        LOG_ERROR_MSG("Failed to create page directory\n");
        kfree(elf_data);
        return false;
    }
    
    LOG_INFO_MSG("Shell: Created page directory at phys 0x%x\n", page_dir_phys);
    
    page_directory_t *page_dir = (page_directory_t*)PHYS_TO_VIRT(page_dir_phys);
    LOG_DEBUG_MSG("Shell: Page directory virt address: %p\n", page_dir);
    
    // 加载 ELF
    LOG_DEBUG_MSG("Shell: Loading ELF (size=%u)...\n", shell_size);
    uint32_t entry_point;
    uint32_t program_end;
    if (!elf_load(elf_data, shell_size, page_dir, &entry_point, &program_end)) {
        LOG_ERROR_MSG("Failed to load ELF\n");
        vmm_free_page_directory(page_dir_phys);
        kfree(elf_data);
        return false;
    }
    
    LOG_DEBUG_MSG("Shell: ELF loaded, entry=%x, program_end=%x\n", entry_point, program_end);
    kfree(elf_data);
    
    // 创建用户进程
    LOG_DEBUG_MSG("Shell: Creating user process...\n");
    uint32_t pid = task_create_user_process("shell", entry_point, page_dir, program_end);
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
