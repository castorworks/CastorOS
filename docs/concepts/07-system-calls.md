# 系统调用

## 概述

系统调用是用户程序请求内核服务的接口。CastorOS 使用 `INT 0x80` 软中断实现系统调用，调用约定与 Linux i386 兼容。

## 调用约定

```
寄存器      用途
--------   ----------------------
EAX        系统调用号
EBX        参数 1
ECX        参数 2
EDX        参数 3
ESI        参数 4
EDI        参数 5
EBP        参数 6 (如需要)

返回值      
--------
EAX        返回值（负数表示错误）
```

## 系统调用流程

```
用户空间                          内核空间
--------                         --------
int 0x80 ─────────────────────→  syscall_handler
    │                                  │
    │                            保存用户上下文
    │                                  │
    │                            验证调用号
    │                                  │
    │                            调用实际处理函数
    │                                  │
    │                            设置返回值
    │                                  │
iret ←────────────────────────  恢复上下文并返回
```

## 系统调用入口

### 汇编入口 (syscall_asm.S)

```asm
.global syscall_entry
syscall_entry:
    ; 保存所有寄存器
    pusha
    push ds
    push es
    push fs
    push gs
    
    ; 切换到内核数据段
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    
    ; 调用 C 处理程序
    push esp            ; registers_t* 参数
    call syscall_handler
    add esp, 4
    
    ; 恢复寄存器
    pop gs
    pop fs
    pop es
    pop ds
    popa
    
    iret
```

### C 分发器 (syscall.c)

```c
// 系统调用表
typedef int (*syscall_fn_t)(registers_t *regs);

static syscall_fn_t syscall_table[] = {
    [SYS_EXIT]      = sys_exit,
    [SYS_FORK]      = sys_fork,
    [SYS_READ]      = sys_read,
    [SYS_WRITE]     = sys_write,
    [SYS_OPEN]      = sys_open,
    [SYS_CLOSE]     = sys_close,
    [SYS_WAITPID]   = sys_waitpid,
    [SYS_EXECVE]    = sys_execve,
    [SYS_GETPID]    = sys_getpid,
    // ... 更多系统调用
};

#define NUM_SYSCALLS (sizeof(syscall_table) / sizeof(syscall_table[0]))

int syscall_handler(registers_t *regs) {
    uint32_t syscall_num = regs->eax;
    
    // 验证调用号
    if (syscall_num >= NUM_SYSCALLS || !syscall_table[syscall_num]) {
        regs->eax = -ENOSYS;
        return -ENOSYS;
    }
    
    // 调用实际处理函数
    int result = syscall_table[syscall_num](regs);
    
    // 设置返回值
    regs->eax = result;
    return result;
}
```

## 系统调用分类

### 进程管理

| 调用号 | 名称 | 原型 | 描述 |
|--------|------|------|------|
| 1 | exit | `void exit(int status)` | 终止进程 |
| 2 | fork | `pid_t fork(void)` | 创建子进程 |
| 7 | waitpid | `pid_t waitpid(pid_t pid, int *status, int options)` | 等待子进程 |
| 11 | execve | `int execve(const char *path, char *argv[], char *envp[])` | 执行程序 |
| 20 | getpid | `pid_t getpid(void)` | 获取进程 ID |
| 64 | getppid | `pid_t getppid(void)` | 获取父进程 ID |

### 文件操作

| 调用号 | 名称 | 原型 | 描述 |
|--------|------|------|------|
| 3 | read | `ssize_t read(int fd, void *buf, size_t count)` | 读文件 |
| 4 | write | `ssize_t write(int fd, const void *buf, size_t count)` | 写文件 |
| 5 | open | `int open(const char *path, int flags, mode_t mode)` | 打开文件 |
| 6 | close | `int close(int fd)` | 关闭文件 |
| 19 | lseek | `off_t lseek(int fd, off_t offset, int whence)` | 移动文件指针 |
| 54 | ioctl | `int ioctl(int fd, unsigned long request, ...)` | 设备控制 |

### 内存管理

| 调用号 | 名称 | 原型 | 描述 |
|--------|------|------|------|
| 45 | brk | `int brk(void *addr)` | 设置数据段末尾 |
| 90 | mmap | `void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)` | 内存映射 |
| 91 | munmap | `int munmap(void *addr, size_t len)` | 取消映射 |

### 时间相关

| 调用号 | 名称 | 原型 | 描述 |
|--------|------|------|------|
| 13 | time | `time_t time(time_t *t)` | 获取时间戳 |
| 162 | nanosleep | `int nanosleep(const struct timespec *req, struct timespec *rem)` | 睡眠 |

### 系统控制

| 调用号 | 名称 | 原型 | 描述 |
|--------|------|------|------|
| 88 | reboot | `int reboot(int magic, int magic2, int cmd)` | 重启系统 |

## 典型实现

### sys_write

```c
ssize_t sys_write(registers_t *regs) {
    int fd = (int)regs->ebx;
    const void *buf = (const void *)regs->ecx;
    size_t count = (size_t)regs->edx;
    
    // 验证参数
    if (fd < 0 || fd >= MAX_FDS) {
        return -EBADF;
    }
    
    // 验证用户缓冲区
    if (!validate_user_buffer(buf, count, false)) {
        return -EFAULT;
    }
    
    // 获取文件对象
    task_t *task = current_task;
    fd_entry_t *entry = &task->fd_table[fd];
    if (!entry->valid) {
        return -EBADF;
    }
    
    // 调用 VFS
    return vfs_write(entry->file, buf, count);
}
```

### sys_fork

```c
pid_t sys_fork(registers_t *regs) {
    task_t *parent = current_task;
    
    // 分配子进程
    task_t *child = task_alloc();
    if (!child) return -ENOMEM;
    
    // 复制地址空间 (COW)
    child->page_dir_phys = vmm_clone_page_directory(parent->page_dir_phys);
    
    // 复制上下文
    memcpy(&child->context, regs, sizeof(registers_t));
    child->context.eax = 0;  // 子进程返回 0
    
    // 复制文件描述符
    for (int i = 0; i < MAX_FDS; i++) {
        if (parent->fd_table[i].valid) {
            child->fd_table[i] = parent->fd_table[i];
            vfs_ref(child->fd_table[i].file);
        }
    }
    
    // 加入调度
    child->state = TASK_READY;
    enqueue_ready(child);
    
    return child->pid;  // 父进程返回子进程 PID
}
```

## 用户态库封装

### 系统调用宏

```c
// syscall.h (用户空间)

#define _syscall0(num) ({ \
    int __res; \
    __asm__ volatile ( \
        "int $0x80" \
        : "=a"(__res) \
        : "a"(num) \
    ); \
    __res; \
})

#define _syscall1(num, arg1) ({ \
    int __res; \
    __asm__ volatile ( \
        "int $0x80" \
        : "=a"(__res) \
        : "a"(num), "b"(arg1) \
    ); \
    __res; \
})

#define _syscall2(num, arg1, arg2) ({ \
    int __res; \
    __asm__ volatile ( \
        "int $0x80" \
        : "=a"(__res) \
        : "a"(num), "b"(arg1), "c"(arg2) \
    ); \
    __res; \
})

#define _syscall3(num, arg1, arg2, arg3) ({ \
    int __res; \
    __asm__ volatile ( \
        "int $0x80" \
        : "=a"(__res) \
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3) \
    ); \
    __res; \
})
```

### 库函数实现

```c
// unistd.c (用户空间)

pid_t fork(void) {
    return _syscall0(SYS_FORK);
}

ssize_t read(int fd, void *buf, size_t count) {
    return _syscall3(SYS_READ, fd, buf, count);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return _syscall3(SYS_WRITE, fd, buf, count);
}

int open(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }
    return _syscall3(SYS_OPEN, path, flags, mode);
}

int close(int fd) {
    return _syscall1(SYS_CLOSE, fd);
}

void _exit(int status) {
    _syscall1(SYS_EXIT, status);
    __builtin_unreachable();
}
```

## 参数验证

### 指针验证

```c
bool validate_user_buffer(const void *ptr, size_t size, bool write) {
    uint32_t addr = (uint32_t)ptr;
    uint32_t end = addr + size;
    
    // 检查溢出
    if (end < addr) return false;
    
    // 检查是否在用户空间
    if (end > KERNEL_VIRTUAL_BASE) return false;
    
    // 检查每一页是否映射且有正确权限
    for (uint32_t page = PAGE_ALIGN_DOWN(addr); 
         page < end; 
         page += PAGE_SIZE) {
        uint32_t pte = vmm_get_pte(current_task->page_dir, page);
        
        if (!(pte & PAGE_PRESENT)) return false;
        if (!(pte & PAGE_USER)) return false;
        if (write && !(pte & PAGE_WRITE)) return false;
    }
    
    return true;
}
```

### 字符串验证

```c
bool validate_user_string(const char *str, size_t max_len) {
    for (size_t i = 0; i < max_len; i++) {
        if (!validate_user_buffer(str + i, 1, false)) {
            return false;
        }
        if (str[i] == '\0') {
            return true;
        }
    }
    return false;  // 字符串太长
}

// 安全复制用户字符串
int copy_from_user_string(char *dst, const char *src, size_t max) {
    for (size_t i = 0; i < max; i++) {
        if (!validate_user_buffer(src + i, 1, false)) {
            return -EFAULT;
        }
        dst[i] = src[i];
        if (src[i] == '\0') {
            return i;
        }
    }
    return -ENAMETOOLONG;
}
```

## 错误码

```c
#define EPERM           1   // 操作不允许
#define ENOENT          2   // 文件不存在
#define ESRCH           3   // 进程不存在
#define EINTR           4   // 系统调用被中断
#define EIO             5   // I/O 错误
#define ENXIO           6   // 设备不存在
#define EBADF           9   // 无效文件描述符
#define ECHILD          10  // 无子进程
#define EAGAIN          11  // 资源暂时不可用
#define ENOMEM          12  // 内存不足
#define EACCES          13  // 权限拒绝
#define EFAULT          14  // 地址错误
#define EBUSY           16  // 资源忙
#define EEXIST          17  // 文件已存在
#define ENODEV          19  // 设备不存在
#define ENOTDIR         20  // 不是目录
#define EISDIR          21  // 是目录
#define EINVAL          22  // 无效参数
#define ENFILE          23  // 系统文件表满
#define EMFILE          24  // 进程打开文件数过多
#define ENOTTY          25  // 不是终端
#define ENOSPC          28  // 磁盘空间不足
#define ESPIPE          29  // 非法 seek
#define EROFS           30  // 只读文件系统
#define EPIPE           32  // 管道破裂
#define ENOSYS          38  // 系统调用不存在
#define ENOTEMPTY       39  // 目录非空
#define ENAMETOOLONG    36  // 文件名太长
```

## 调试

### 系统调用跟踪

```c
#ifdef SYSCALL_TRACE
int syscall_handler(registers_t *regs) {
    uint32_t num = regs->eax;
    
    kprintf("[%d] syscall %d(%x, %x, %x)\n",
            current_task->pid, num,
            regs->ebx, regs->ecx, regs->edx);
    
    int result = do_syscall(regs);
    
    kprintf("[%d] syscall %d = %d\n",
            current_task->pid, num, result);
    
    return result;
}
#endif
```

