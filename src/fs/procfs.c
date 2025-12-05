// ============================================================================
// procfs.c - 进程文件系统实现
// ============================================================================

#include <fs/procfs.h>
#include <fs/vfs.h>
#include <kernel/task.h>
#include <drivers/pci.h>
#include <drivers/usb/usb.h>
#include <lib/string.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <stdarg.h>

/* 前向声明 */
static struct dirent *procfs_root_readdir(fs_node_t *node, uint32_t index);
static fs_node_t *procfs_root_finddir(fs_node_t *node, const char *name);
static struct dirent *procfs_pid_readdir(fs_node_t *node, uint32_t index);
static fs_node_t *procfs_pid_finddir(fs_node_t *node, const char *name);
static uint32_t procfs_status_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static uint32_t procfs_meminfo_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static uint32_t procfs_pci_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static uint32_t procfs_usb_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);

/* ProcFS 私有数据结构 - 包含 readdir 缓冲区以避免静态变量 */
typedef struct procfs_private {
    struct dirent readdir_cache;  // readdir 结果缓冲区
} procfs_private_t;

/* /proc 根目录节点 */
static fs_node_t *procfs_root = NULL;
static procfs_private_t *procfs_root_private = NULL;  // 根目录私有数据
static fs_node_t *procfs_meminfo_file = NULL;
static fs_node_t *procfs_pci_file = NULL;
static fs_node_t *procfs_usb_file = NULL;

/* 注意：不再需要 procfs_lock，因为不再缓存节点 */
static uint32_t procfs_meminfo_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    
    if (!buffer || size == 0) {
        return 0;
    }
    
    pmm_info_t pmm_info = pmm_get_info();
    uint32_t total_kb = (pmm_info.total_frames * PAGE_SIZE) / 1024;
    uint32_t free_kb = (pmm_info.free_frames * PAGE_SIZE) / 1024;
    uint32_t used_kb = (pmm_info.used_frames * PAGE_SIZE) / 1024;
    uint32_t reserved_kb = (pmm_info.reserved_frames * PAGE_SIZE) / 1024;
    uint32_t kernel_kb = (pmm_info.kernel_frames * PAGE_SIZE) / 1024;
    uint32_t bitmap_kb = (pmm_info.bitmap_frames * PAGE_SIZE) / 1024;
    
    // 获取堆统计信息
    heap_info_t heap_info;
    int heap_ret = heap_get_info(&heap_info);
    uint32_t heap_total_kb = 0;
    uint32_t heap_used_kb = 0;
    uint32_t heap_free_kb = 0;
    uint32_t heap_max_kb = 0;
    uint32_t heap_blocks = 0;
    uint32_t heap_free_blocks = 0;
    uint32_t heap_used_blocks = 0;
    if (heap_ret == 0) {
        heap_total_kb = (uint32_t)(heap_info.total / 1024);
        heap_used_kb = (uint32_t)(heap_info.used / 1024);
        heap_free_kb = (uint32_t)(heap_info.free / 1024);
        heap_max_kb = (uint32_t)(heap_info.max / 1024);
        heap_blocks = heap_info.block_count;
        heap_free_blocks = heap_info.free_block_count;
        heap_used_blocks = heap_blocks - heap_free_blocks;
    }
    
    char meminfo_buf[1024];
    int len = ksnprintf(meminfo_buf, sizeof(meminfo_buf),
                        "MemTotal:\t%u kB\n"
                        "MemFree:\t%u kB\n"
                        "MemUsed:\t%u kB\n"
                        "MemReserved:\t%u kB\n"
                        "MemKernel:\t%u kB\n"
                        "MemBitmap:\t%u kB\n"
                        "PageSize:\t%u bytes\n"
                        "PageTotal:\t%u\n"
                        "PageFree:\t%u\n"
                        "PageUsed:\t%u\n"
                        "HeapTotal:\t%u kB\n"
                        "HeapUsed:\t%u kB\n"
                        "HeapFree:\t%u kB\n"
                        "HeapMax:\t%u kB\n"
                        "HeapBlocks:\t%u\n"
                        "HeapUsedBlocks:\t%u\n"
                        "HeapFreeBlocks:\t%u\n",
                        (unsigned int)total_kb,
                        (unsigned int)free_kb,
                        (unsigned int)used_kb,
                        (unsigned int)reserved_kb,
                        (unsigned int)kernel_kb,
                        (unsigned int)bitmap_kb,
                        (unsigned int)PAGE_SIZE,
                        (unsigned int)pmm_info.total_frames,
                        (unsigned int)pmm_info.free_frames,
                        (unsigned int)pmm_info.used_frames,
                        (unsigned int)heap_total_kb,
                        (unsigned int)heap_used_kb,
                        (unsigned int)heap_free_kb,
                        (unsigned int)heap_max_kb,
                        (unsigned int)heap_blocks,
                        (unsigned int)heap_used_blocks,
                        (unsigned int)heap_free_blocks);
    
    if (len < 0 || len >= (int)sizeof(meminfo_buf)) {
        len = sizeof(meminfo_buf) - 1;
    }
    
    uint32_t file_size = (uint32_t)len;
    if (offset >= file_size) {
        return 0;
    }
    
    uint32_t bytes_to_read = size;
    if (offset + bytes_to_read > file_size) {
        bytes_to_read = file_size - offset;
    }
    
    memcpy(buffer, meminfo_buf + offset, bytes_to_read);
    return bytes_to_read;
}

/**
 * 获取 PCI 设备类别名称
 */
static const char *pci_get_class_name(uint8_t class_code, uint8_t subclass) {
    switch (class_code) {
        case 0x00:
            return "Unclassified";
        case 0x01:
            switch (subclass) {
                case 0x00: return "SCSI Controller";
                case 0x01: return "IDE Controller";
                case 0x05: return "ATA Controller";
                case 0x06: return "SATA Controller";
                case 0x08: return "NVMe Controller";
                default: return "Storage Controller";
            }
        case 0x02:
            switch (subclass) {
                case 0x00: return "Ethernet Controller";
                case 0x80: return "Network Controller";
                default: return "Network Controller";
            }
        case 0x03:
            switch (subclass) {
                case 0x00: return "VGA Controller";
                case 0x01: return "XGA Controller";
                case 0x02: return "3D Controller";
                default: return "Display Controller";
            }
        case 0x04:
            return "Multimedia Controller";
        case 0x05:
            return "Memory Controller";
        case 0x06:
            switch (subclass) {
                case 0x00: return "Host Bridge";
                case 0x01: return "ISA Bridge";
                case 0x04: return "PCI-to-PCI Bridge";
                case 0x80: return "Bridge Device";
                default: return "Bridge Device";
            }
        case 0x07:
            return "Communication Controller";
        case 0x08:
            return "System Peripheral";
        case 0x09:
            return "Input Device";
        case 0x0C:
            switch (subclass) {
                case 0x03: return "USB Controller";
                case 0x05: return "SMBus Controller";
                default: return "Serial Bus Controller";
            }
        default:
            return "Unknown Device";
    }
}

/**
 * 读取 /proc/pci 文件
 */
static uint32_t procfs_pci_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    
    if (!buffer || size == 0) {
        return 0;
    }
    
    // 使用足够大的缓冲区存储 PCI 设备信息（每设备约 100 字节，支持 32 个设备）
    static char pci_buf[8192];
    int len = 0;
    
    int device_count = pci_get_device_count();
    
    // 表头
    len += ksnprintf(pci_buf + len, sizeof(pci_buf) - (size_t)len,
                     "PCI Devices: %d\n"
                     "================================================================================\n"
                     "Bus:Slot.Func  Vendor:Device  Class       Description\n"
                     "--------------------------------------------------------------------------------\n",
                     device_count);
    
    // 遍历所有 PCI 设备
    for (int i = 0; i < device_count && len < (int)sizeof(pci_buf) - 128; i++) {
        pci_device_t *dev = pci_get_device(i);
        if (!dev) continue;
        
        const char *class_name = pci_get_class_name(dev->class_code, dev->subclass);
        
        len += ksnprintf(pci_buf + len, sizeof(pci_buf) - (size_t)len,
                         "%02x:%02x.%x     %04x:%04x      %02x:%02x       %s\n",
                         dev->bus, dev->slot, dev->func,
                         dev->vendor_id, dev->device_id,
                         dev->class_code, dev->subclass,
                         class_name);
    }
    
    // 分隔线
    if (len < (int)sizeof(pci_buf) - 80) {
        len += ksnprintf(pci_buf + len, sizeof(pci_buf) - (size_t)len,
                         "================================================================================\n");
    }
    
    if (len < 0 || len >= (int)sizeof(pci_buf)) {
        len = (int)sizeof(pci_buf) - 1;
    }
    
    uint32_t file_size = (uint32_t)len;
    if (offset >= file_size) {
        return 0;
    }
    
    uint32_t bytes_to_read = size;
    if (offset + bytes_to_read > file_size) {
        bytes_to_read = file_size - offset;
    }
    
    memcpy(buffer, pci_buf + offset, bytes_to_read);
    return bytes_to_read;
}

/**
 * 获取 USB 设备类名称
 */
static const char *usb_get_class_name(uint8_t class_code, uint8_t subclass) {
    switch (class_code) {
        case 0x00:
            return "Per-Interface";
        case 0x01:
            return "Audio";
        case 0x02:
            return "Communications";
        case 0x03:
            return "HID";
        case 0x05:
            return "Physical";
        case 0x06:
            return "Image";
        case 0x07:
            return "Printer";
        case 0x08:
            switch (subclass) {
                case 0x01: return "RBC Storage";
                case 0x02: return "ATAPI Storage";
                case 0x04: return "UFI Storage";
                case 0x06: return "SCSI Storage";
                default:   return "Mass Storage";
            }
        case 0x09:
            return "Hub";
        case 0x0A:
            return "CDC-Data";
        case 0x0B:
            return "Smart Card";
        case 0x0D:
            return "Content Security";
        case 0x0E:
            return "Video";
        case 0x0F:
            return "Personal Healthcare";
        case 0xDC:
            return "Diagnostic";
        case 0xE0:
            return "Wireless Controller";
        case 0xEF:
            return "Miscellaneous";
        case 0xFE:
            return "Application Specific";
        case 0xFF:
            return "Vendor Specific";
        default:
            return "Unknown";
    }
}

/**
 * 读取 /proc/usb 文件
 */
static uint32_t procfs_usb_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    
    if (!buffer || size == 0) {
        return 0;
    }
    
    // 使用缓冲区存储 USB 设备信息
    static char usb_buf[4096];
    int len = 0;
    
    int device_count = usb_get_device_count();
    
    // 表头
    len += ksnprintf(usb_buf + len, sizeof(usb_buf) - (size_t)len,
                     "USB Devices: %d\n"
                     "================================================================================\n"
                     "Bus Addr  VID:PID     Speed   Class       Description\n"
                     "--------------------------------------------------------------------------------\n",
                     device_count);
    
    // 遍历所有 USB 设备
    for (int i = 0; i < device_count && len < (int)sizeof(usb_buf) - 128; i++) {
        usb_device_t *dev = usb_get_device(i);
        if (!dev) continue;
        
        // 获取类别名称（优先使用设备类，否则使用接口类）
        uint8_t class_code = dev->device_desc.bDeviceClass;
        uint8_t subclass = dev->device_desc.bDeviceSubClass;
        
        // 如果设备类为 0（Per-Interface），使用第一个接口的类
        if (class_code == 0 && dev->num_interfaces > 0) {
            class_code = dev->interfaces[0].class_code;
            subclass = dev->interfaces[0].subclass_code;
        }
        
        const char *class_name = usb_get_class_name(class_code, subclass);
        const char *speed_str = (dev->speed == USB_SPEED_LOW) ? "Low" : "Full";
        
        len += ksnprintf(usb_buf + len, sizeof(usb_buf) - (size_t)len,
                         "%3d  %3d   %04x:%04x   %-6s  %02x:%02x       %s\n",
                         dev->port, dev->address,
                         dev->device_desc.idVendor, dev->device_desc.idProduct,
                         speed_str,
                         class_code, subclass,
                         class_name);
        
        // 显示接口信息
        for (uint8_t j = 0; j < dev->num_interfaces && len < (int)sizeof(usb_buf) - 80; j++) {
            usb_interface_t *iface = &dev->interfaces[j];
            len += ksnprintf(usb_buf + len, sizeof(usb_buf) - (size_t)len,
                             "          Interface %d: %02x:%02x:%02x  EPs: %d\n",
                             iface->interface_number,
                             iface->class_code, iface->subclass_code, iface->protocol,
                             iface->num_endpoints);
        }
    }
    
    // 分隔线
    if (len < (int)sizeof(usb_buf) - 80) {
        len += ksnprintf(usb_buf + len, sizeof(usb_buf) - (size_t)len,
                         "================================================================================\n");
    }
    
    if (len < 0 || len >= (int)sizeof(usb_buf)) {
        len = (int)sizeof(usb_buf) - 1;
    }
    
    uint32_t file_size = (uint32_t)len;
    if (offset >= file_size) {
        return 0;
    }
    
    uint32_t bytes_to_read = size;
    if (offset + bytes_to_read > file_size) {
        bytes_to_read = file_size - offset;
    }
    
    memcpy(buffer, usb_buf + offset, bytes_to_read);
    return bytes_to_read;
}


/* 注意：不再缓存进程目录和状态文件节点
 * 每次请求时动态创建，由 VFS 的引用计数机制管理生命周期
 * 这避免了悬空指针问题（节点被释放后数组中仍有指针）
 */

/**
 * 获取进程状态字符串
 */
static const char *get_task_state_string(task_state_t state) {
    switch (state) {
        case TASK_READY:      return "R";  // Running/Ready
        case TASK_RUNNING:   return "R";  // Running
        case TASK_BLOCKED:   return "S";  // Sleeping
        case TASK_ZOMBIE:    return "Z";  // Zombie (waiting for parent)
        case TASK_TERMINATED: return "T"; // Terminated (orphan process)
        default:             return "?";  // Unknown
    }
}

/**
 * 读取 /proc/[pid]/status 文件
 */
static uint32_t procfs_status_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (!node || !buffer || size == 0) {
        return 0;
    }
    
    // 从 impl_data 中获取 PID
    uint32_t pid = node->impl_data;
    task_t *task = task_get_by_pid(pid);
    
    if (!task || task->state == TASK_UNUSED) {
        return 0;  // 进程不存在
    }
    
    // 生成状态信息字符串
    char status_buf[512];
    int len = ksnprintf(status_buf, sizeof(status_buf),
        "Name:\t%s\n"
        "State:\t%s\n"
        "Pid:\t%u\n"
        "PPid:\t%u\n"
        "Priority:\t%u\n"
        "Runtime:\t%llu ms\n",
        task->name,
        get_task_state_string(task->state),
        (unsigned int)task->pid,
        task->parent ? (unsigned int)task->parent->pid : 0,
        (unsigned int)task->priority,
        (unsigned long long)task->runtime_ms);  // 运行时间（毫秒）
    
    if (len < 0 || len >= (int)sizeof(status_buf)) {
        len = sizeof(status_buf) - 1;
    }
    
    uint32_t file_size = (uint32_t)len;
    
    // 检查偏移量
    if (offset >= file_size) {
        return 0;  // 超出文件范围
    }
    
    // 计算可读取的字节数
    uint32_t bytes_to_read = size;
    if (offset + bytes_to_read > file_size) {
        bytes_to_read = file_size - offset;
    }
    
    // 复制数据到缓冲区
    memcpy(buffer, status_buf + offset, bytes_to_read);
    
    return bytes_to_read;
}

/**
 * 读取 /proc/[pid] 目录
 */
static struct dirent *procfs_pid_readdir(fs_node_t *node, uint32_t index) {
    // 使用节点私有的 dirent 缓冲区，避免静态变量带来的并发问题
    procfs_private_t *priv = (procfs_private_t *)node->impl;
    if (!priv) {
        LOG_ERROR_MSG("procfs: pid_readdir called on node without private data\n");
        return NULL;
    }
    struct dirent *dirent = &priv->readdir_cache;
    
    /* 返回 . */
    if (index == 0) {
        strcpy(dirent->d_name, ".");
        dirent->d_ino = 0;
        dirent->d_reclen = sizeof(struct dirent);
        dirent->d_off = 1;
        dirent->d_type = DT_DIR;
        return dirent;
    }
    
    /* 返回 .. */
    if (index == 1) {
        strcpy(dirent->d_name, "..");
        dirent->d_ino = 0;
        dirent->d_reclen = sizeof(struct dirent);
        dirent->d_off = 2;
        dirent->d_type = DT_DIR;
        return dirent;
    }
    
    /* 返回 status 文件 */
    if (index == 2) {
        strcpy(dirent->d_name, "status");
        dirent->d_ino = 0;
        dirent->d_reclen = sizeof(struct dirent);
        dirent->d_off = 3;
        dirent->d_type = DT_REG;
        return dirent;
    }
    
    return NULL;
}

/**
 * 在 /proc/[pid] 目录中查找文件
 */
static fs_node_t *procfs_pid_finddir(fs_node_t *node, const char *name) {
    if (!node || !name) {
        return NULL;
    }
    
    /* 处理 . 和 .. */
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        // 增加引用计数
        vfs_ref_node(node);
        return node;  // 返回当前目录节点
    }
    
    /* 查找 status 文件 */
    if (strcmp(name, "status") == 0) {
        uint32_t pid = node->impl_data;
        
        // 验证进程仍然存在
        task_t *task = task_get_by_pid(pid);
        if (!task || task->state == TASK_UNUSED) {
            return NULL;
        }
        
        // 每次都创建新的 status 文件节点（由 VFS 引用计数管理生命周期）
        fs_node_t *status_file = (fs_node_t *)kmalloc(sizeof(fs_node_t));
        if (!status_file) {
            return NULL;
        }
        
        memset(status_file, 0, sizeof(fs_node_t));
        strcpy(status_file->name, "status");
        status_file->inode = 0;
        status_file->type = FS_FILE;
        status_file->size = 512;  // 估计大小
        status_file->permissions = FS_PERM_READ;
        status_file->impl_data = pid;  // 存储 PID
        status_file->impl = NULL;  // status 文件不需要私有数据
        status_file->ref_count = 1;  // 返回时引用计数为 1
        status_file->read = procfs_status_read;
        status_file->write = NULL;
        status_file->open = NULL;
        status_file->close = NULL;
        status_file->readdir = NULL;
        status_file->finddir = NULL;
        status_file->create = NULL;
        status_file->mkdir = NULL;
        status_file->unlink = NULL;
        status_file->ptr = NULL;
        status_file->flags = FS_NODE_FLAG_ALLOCATED;  // 标记为动态分配
        
        return status_file;
    }
    
    return NULL;
}

/**
 * 读取 /proc 根目录
 */
static struct dirent *procfs_root_readdir(fs_node_t *node, uint32_t index) {
    // 使用节点私有的 dirent 缓冲区，避免静态变量带来的并发问题
    procfs_private_t *priv = (procfs_private_t *)node->impl;
    if (!priv) {
        LOG_ERROR_MSG("procfs: readdir called on node without private data\n");
        return NULL;
    }
    struct dirent *dirent = &priv->readdir_cache;
    
    /* 返回 . */
    if (index == 0) {
        strcpy(dirent->d_name, ".");
        dirent->d_ino = 0;
        dirent->d_reclen = sizeof(struct dirent);
        dirent->d_off = 1;
        dirent->d_type = DT_DIR;
        return dirent;
    }
    
    /* 返回 .. */
    if (index == 1) {
        strcpy(dirent->d_name, "..");
        dirent->d_ino = 0;
        dirent->d_reclen = sizeof(struct dirent);
        dirent->d_off = 2;
        dirent->d_type = DT_DIR;
        return dirent;
    }
    
    /* 返回 meminfo 文件 */
    if (index == 2) {
        strcpy(dirent->d_name, "meminfo");
        dirent->d_ino = 0;
        dirent->d_reclen = sizeof(struct dirent);
        dirent->d_off = 3;
        dirent->d_type = DT_REG;
        return dirent;
    }
    
    /* 返回 pci 文件 */
    if (index == 3) {
        strcpy(dirent->d_name, "pci");
        dirent->d_ino = 0;
        dirent->d_reclen = sizeof(struct dirent);
        dirent->d_off = 4;
        dirent->d_type = DT_REG;
        return dirent;
    }
    
    /* 返回 usb 文件 */
    if (index == 4) {
        strcpy(dirent->d_name, "usb");
        dirent->d_ino = 0;
        dirent->d_reclen = sizeof(struct dirent);
        dirent->d_off = 5;
        dirent->d_type = DT_REG;
        return dirent;
    }
    
    /* 返回进程目录（PID 目录） */
    uint32_t pid_index = index - 5;
    
    // 遍历所有任务，找到第 pid_index 个有效进程
    uint32_t found_count = 0;
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        task_t *task = task_get_by_pid(i);
        if (task && task->state != TASK_UNUSED) {
            if (found_count == pid_index) {
                // 找到对应的进程，返回其 PID 目录名
                char pid_str[32];
                ksnprintf(pid_str, sizeof(pid_str), "%u", task->pid);
                strcpy(dirent->d_name, pid_str);
                dirent->d_ino = 0;
                dirent->d_reclen = sizeof(struct dirent);
                dirent->d_off = index + 1;
                dirent->d_type = DT_DIR;
                return dirent;
            }
            found_count++;
        }
    }
    
    return NULL;
}

/**
 * 在 /proc 根目录中查找进程目录
 */
static fs_node_t *procfs_root_finddir(fs_node_t *node, const char *name) {
    if (!name) {
        return NULL;
    }
    
    /* 处理 . 和 .. */
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        // 增加引用计数（根节点是静态的，不会被释放，但需要计数管理）
        vfs_ref_node(node);
        return node;  // 返回当前目录节点（procfs_root）
    }
    
    /* meminfo 文件 */
    if (strcmp(name, "meminfo") == 0) {
        // 增加引用计数（静态节点也需要引用计数管理）
        vfs_ref_node(procfs_meminfo_file);
        return procfs_meminfo_file;
    }
    
    /* pci 文件 */
    if (strcmp(name, "pci") == 0) {
        vfs_ref_node(procfs_pci_file);
        return procfs_pci_file;
    }
    
    /* usb 文件 */
    if (strcmp(name, "usb") == 0) {
        vfs_ref_node(procfs_usb_file);
        return procfs_usb_file;
    }
    
    /* 尝试解析为 PID */
    uint32_t pid = 0;
    const char *p = name;
    while (*p >= '0' && *p <= '9') {
        pid = pid * 10 + (*p - '0');
        p++;
    }
    
    // 如果解析成功且进程存在
    if (*p == '\0' && pid > 0) {
        task_t *task = task_get_by_pid(pid);
        if (task && task->state != TASK_UNUSED) {
            // 每次都创建新的进程目录节点（由 VFS 引用计数管理生命周期）
            fs_node_t *pid_dir = (fs_node_t *)kmalloc(sizeof(fs_node_t));
            if (!pid_dir) {
                return NULL;
            }
            
            // 分配私有数据（包含 readdir 缓冲区）
            procfs_private_t *priv = (procfs_private_t *)kmalloc(sizeof(procfs_private_t));
            if (!priv) {
                kfree(pid_dir);
                return NULL;
            }
            memset(priv, 0, sizeof(procfs_private_t));
            
            memset(pid_dir, 0, sizeof(fs_node_t));
            ksnprintf(pid_dir->name, sizeof(pid_dir->name), "%u", pid);
            pid_dir->inode = 0;
            pid_dir->type = FS_DIRECTORY;
            pid_dir->size = 0;
            pid_dir->permissions = FS_PERM_READ | FS_PERM_EXEC;
            pid_dir->impl_data = pid;  // 存储 PID
            pid_dir->impl = priv;  // 设置私有数据（包含 readdir 缓冲区）
            pid_dir->ref_count = 1;  // 返回时引用计数为 1
            pid_dir->read = NULL;
            pid_dir->write = NULL;
            pid_dir->open = NULL;
            pid_dir->close = NULL;
            pid_dir->readdir = procfs_pid_readdir;
            pid_dir->finddir = procfs_pid_finddir;
            pid_dir->create = NULL;
            pid_dir->mkdir = NULL;
            pid_dir->unlink = NULL;
            pid_dir->ptr = NULL;
            pid_dir->flags = FS_NODE_FLAG_ALLOCATED;  // 标记为动态分配
            
            return pid_dir;
        }
    }
    
    return NULL;
}

/**
 * 初始化 procfs
 */
fs_node_t *procfs_init(void) {
    LOG_INFO_MSG("procfs: Initializing process filesystem...\n");
    
    // 创建 /proc 根目录节点
    procfs_root = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!procfs_root) {
        LOG_ERROR_MSG("procfs: Failed to allocate root node\n");
        return NULL;
    }
    
    // 分配根目录私有数据
    procfs_root_private = (procfs_private_t *)kmalloc(sizeof(procfs_private_t));
    if (!procfs_root_private) {
        LOG_ERROR_MSG("procfs: Failed to allocate root private data\n");
        kfree(procfs_root);
        procfs_root = NULL;
        return NULL;
    }
    memset(procfs_root_private, 0, sizeof(procfs_private_t));
    
    memset(procfs_root, 0, sizeof(fs_node_t));
    strcpy(procfs_root->name, "proc");
    procfs_root->inode = 0;
    procfs_root->type = FS_DIRECTORY;
    procfs_root->size = 0;
    procfs_root->permissions = FS_PERM_READ | FS_PERM_EXEC;
    procfs_root->uid = 0;
    procfs_root->gid = 0;
    procfs_root->flags = 0;
    procfs_root->ref_count = 0;  // 初始化引用计数
    procfs_root->read = NULL;
    procfs_root->write = NULL;
    procfs_root->open = NULL;
    procfs_root->close = NULL;
    procfs_root->readdir = procfs_root_readdir;
    procfs_root->finddir = procfs_root_finddir;
    procfs_root->create = NULL;  // 不支持创建文件
    procfs_root->mkdir = NULL;   // 不支持创建目录
    procfs_root->unlink = NULL;  // 不支持删除
    procfs_root->ptr = NULL;
    procfs_root->impl = procfs_root_private;  // 设置私有数据
    
    /* 创建 meminfo 文件节点 */
    procfs_meminfo_file = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!procfs_meminfo_file) {
        LOG_ERROR_MSG("procfs: Failed to allocate meminfo node\n");
        return procfs_root;
    }
    
    memset(procfs_meminfo_file, 0, sizeof(fs_node_t));
    strcpy(procfs_meminfo_file->name, "meminfo");
    procfs_meminfo_file->inode = 0;
    procfs_meminfo_file->type = FS_FILE;
    procfs_meminfo_file->size = 512;
    procfs_meminfo_file->permissions = FS_PERM_READ;
    procfs_meminfo_file->ref_count = 0;  // 初始化引用计数
    procfs_meminfo_file->read = procfs_meminfo_read;
    procfs_meminfo_file->write = NULL;
    procfs_meminfo_file->open = NULL;
    procfs_meminfo_file->close = NULL;
    procfs_meminfo_file->readdir = NULL;
    procfs_meminfo_file->finddir = NULL;
    procfs_meminfo_file->create = NULL;
    procfs_meminfo_file->mkdir = NULL;
    procfs_meminfo_file->unlink = NULL;
    procfs_meminfo_file->ptr = NULL;
    
    /* 创建 pci 文件节点 */
    procfs_pci_file = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!procfs_pci_file) {
        LOG_ERROR_MSG("procfs: Failed to allocate pci node\n");
        return procfs_root;
    }
    
    memset(procfs_pci_file, 0, sizeof(fs_node_t));
    strcpy(procfs_pci_file->name, "pci");
    procfs_pci_file->inode = 0;
    procfs_pci_file->type = FS_FILE;
    procfs_pci_file->size = 4096;
    procfs_pci_file->permissions = FS_PERM_READ;
    procfs_pci_file->ref_count = 0;
    procfs_pci_file->read = procfs_pci_read;
    procfs_pci_file->write = NULL;
    procfs_pci_file->open = NULL;
    procfs_pci_file->close = NULL;
    procfs_pci_file->readdir = NULL;
    procfs_pci_file->finddir = NULL;
    procfs_pci_file->create = NULL;
    procfs_pci_file->mkdir = NULL;
    procfs_pci_file->unlink = NULL;
    procfs_pci_file->ptr = NULL;
    
    /* 创建 usb 文件节点 */
    procfs_usb_file = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!procfs_usb_file) {
        LOG_ERROR_MSG("procfs: Failed to allocate usb node\n");
        return procfs_root;
    }
    
    memset(procfs_usb_file, 0, sizeof(fs_node_t));
    strcpy(procfs_usb_file->name, "usb");
    procfs_usb_file->inode = 0;
    procfs_usb_file->type = FS_FILE;
    procfs_usb_file->size = 4096;
    procfs_usb_file->permissions = FS_PERM_READ;
    procfs_usb_file->ref_count = 0;
    procfs_usb_file->read = procfs_usb_read;
    procfs_usb_file->write = NULL;
    procfs_usb_file->open = NULL;
    procfs_usb_file->close = NULL;
    procfs_usb_file->readdir = NULL;
    procfs_usb_file->finddir = NULL;
    procfs_usb_file->create = NULL;
    procfs_usb_file->mkdir = NULL;
    procfs_usb_file->unlink = NULL;
    procfs_usb_file->ptr = NULL;
    
    LOG_INFO_MSG("procfs: Initialized\n");
    
    return procfs_root;
}

