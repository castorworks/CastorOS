# 阶段 9: 用户模式

## 概述

CastorOS 已实现完整的用户模式支持，建立了特权级分离机制，系统能够运行用户态程序。实现了从内核态到用户态的切换、ELF程序加载器、用户进程管理和完整的系统调用接口。

**🎯 实现状态**：

✅ **用户态程序执行**
   - 完整的 ELF 加载器支持
   - fork + exec 进程创建模式
   - 独立的用户地址空间
   
✅ **系统调用接口**（`userland/lib/syscall.h`）
   - 统一的系统调用接口
   - 支持文件 I/O、进程管理等
   - 用户程序无需直接写汇编
   
✅ **特权级切换机制**
   - Ring 0 ↔ Ring 3 切换
   - 中断/系统调用处理
   - TSS 内核栈管理

✅ **用户程序示例**
   - Hello World 程序（`userland/helloworld/`）
   - 用户态 Shell（`userland/shell/`）
   - 标准的 `_start()` 入口点

---

## 实现状态

- ✅ 实现从内核态（Ring 0）到用户态（Ring 3）的切换
- ✅ 创建用户进程结构和管理机制
- ✅ 实现完整的 ELF 程序加载器
- ✅ 扩展系统调用以支持用户程序
- ✅ 实现用户空间测试程序
- ✅ 验证特权级保护机制

---

## 技术背景

### 什么是特权级？

**特权级（Privilege Level）** 是 x86 架构的核心安全机制，将代码分为不同的权限等级。

**x86 的四个特权级（Ring）**：

```
┌─────────────────────────────┐
│      Ring 0 (Kernel)        │  最高权限
│  ┌───────────────────────┐  │  - 可执行所有指令
│  │     Ring 1            │  │  - 可访问所有资源
│  │  ┌─────────────────┐  │  │  - I/O 操作
│  │  │     Ring 2      │  │  │  - 页表管理
│  │  │  ┌───────────┐  │  │  │
│  │  │  │  Ring 3   │  │  │  │  最低权限
│  │  │  │  (User)   │  │  │  │  - 受限的指令集
│  │  │  └───────────┘  │  │  │  - 无法直接访问硬件
│  │  │                 │  │  │  - 需要通过系统调用
│  │  └─────────────────┘  │  │
│  └───────────────────────┘  │
└─────────────────────────────┘
```

**实际使用**：
- **Ring 0**：操作系统内核
- **Ring 1/2**：通常不使用（现代 OS 大多不用）
- **Ring 3**：用户应用程序

**为什么需要特权级分离？**

1. **安全性**：防止用户程序破坏内核或其他程序
2. **稳定性**：用户程序崩溃不会影响整个系统
3. **资源管理**：内核控制硬件资源的访问
4. **多任务**：实现进程隔离

---

### 特权级如何工作？

**1. 代码段描述符特权级（DPL）**

GDT 中的段描述符包含 DPL 字段：
- 内核代码段：DPL = 0（Ring 0）
- 用户代码段：DPL = 3（Ring 3）

**2. 当前特权级（CPL）**

CPU 的 CS 寄存器低 2 位表示当前特权级：
```
CS = 0x08  (0000 1000 binary) → RPL = 0 → Ring 0
CS = 0x1B  (0001 1011 binary) → RPL = 3 → Ring 3
```

**3. 特权级检查**

CPU 在以下情况下检查特权级：
- 访问段时：CPL 必须 ≤ DPL
- 执行指令时：某些指令只能在 Ring 0 执行
- 调用门/中断门：检查 DPL 和 CPL

**4. 特权级切换时机**

| 切换方向 | 机制 | 场景 |
|---------|------|------|
| Ring 3 → Ring 0 | 中断/异常/系统调用 | 用户程序请求内核服务 |
| Ring 0 → Ring 3 | IRET 指令 | 内核返回用户程序 |
| Ring 0 → Ring 3 | 手动设置 | 首次进入用户模式 |

---

### 特权级切换的硬件机制

**从 Ring 3 切换到 Ring 0（系统调用/中断）**：

1. **CPU 自动操作**：
   ```
   1. 从 TSS 加载内核栈（SS0:ESP0）
   2. 切换到内核栈
   3. 压入用户栈信息：SS, ESP
   4. 压入用户状态：EFLAGS, CS, EIP
   5. 如果有错误码，压入错误码
   6. 跳转到中断处理程序（CS:EIP）
   ```

2. **栈结构变化**：
   ```
   用户栈（Ring 3）             内核栈（Ring 0）
   ┌──────────┐                ┌─────────────┐
   │   ...    │                │  User SS    │ ← CPU 自动压入
   │          │                │  User ESP   │
   │          │                │  EFLAGS     │
   └──────────┘                │  User CS    │
        ↑                      │  User EIP   │
       ESP                     │ (ErrorCode) │
                               └─────────────┘
                                    ↑
                                   ESP
   ```

**从 Ring 0 切换到 Ring 3（返回用户态）**：

1. **IRET 指令**：
   ```
   1. 从栈弹出：EIP, CS, EFLAGS
   2. 检查 CS 的 RPL（请求特权级）
   3. 如果 RPL = 3，继续弹出：ESP, SS
   4. 切换到用户栈
   5. 恢复执行
   ```

**TSS 的关键作用**：

TSS（任务状态段）存储内核栈信息：
- **ESP0**：Ring 0 栈指针
- **SS0**：Ring 0 栈段选择子

当从 Ring 3 进入 Ring 0 时，CPU 自动从 TSS 加载这两个值。

**重要**：每次任务切换时，必须更新 TSS 的 ESP0，确保每个任务使用正确的内核栈！

---

### 用户进程与内核线程的区别

| 特性 | 内核线程（阶段 5） | 用户进程（阶段 6） |
|------|-------------------|-------------------|
| **特权级** | Ring 0 | Ring 3 |
| **地址空间** | 共享内核地址空间 | 独立的用户地址空间 |
| **页目录** | 共享内核页目录 | 独立的页目录 |
| **栈** | 仅内核栈 | 内核栈 + 用户栈 |
| **段寄存器** | 内核段（0x08/0x10） | 用户段（0x1B/0x23） |
| **系统调用** | 直接调用内核函数 | 通过 INT 0x80 |
| **安全性** | 可以做任何事 | 受限制 |

**用户进程的内存布局**：

```
虚拟地址空间（4GB）

0xFFFFFFFF  ┌──────────────────┐
            │   Kernel Space   │  Ring 0
0x80000000  ├──────────────────┤  （所有进程共享）
            │   User Stack     │  ↓ 向下增长
            │                  │
            │   (Unmapped)     │
            │                  │
            │   Heap           │  ↑ 向上增长
            ├──────────────────┤
            │   Data Segment   │  全局变量
            ├──────────────────┤
            │   Code Segment   │  程序代码
0x00000000  └──────────────────┘
```

---

## 实现架构

### 核心组件

#### 1. 用户模式切换（`/src/kernel/user.c`）

```c
// 进入用户模式的 C 包装器
void task_enter_usermode(uint32_t entry_point, uint32_t user_stack);

// 用户模式包装器（首次进入用户进程时调用）
static void usermode_wrapper(void);
```

**关键特性**：
- 使用 IRET 指令实现 Ring 0 → Ring 3 切换
- 自动更新 TSS 内核栈指针
- 切换到用户进程的独立页目录

#### 2. 汇编实现（`/src/kernel/user_asm.asm`）

```asm
enter_usermode:
    ; 构造 IRET 栈帧
    push 0x23        ; SS (用户数据段)
    push ebx         ; ESP (用户栈指针)
    pushfd           ; EFLAGS
    or ecx, 0x200    ; 启用中断
    push ecx
    push 0x1B        ; CS (用户代码段)
    push eax         ; EIP (用户程序入口)
    
    ; 设置用户数据段
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    iret             ; 切换到用户模式
```

#### 3. 用户进程创建（`/src/kernel/task.c`）

```c
uint32_t task_create_user_process(const char *name, 
                                  uint32_t entry_point, 
                                  page_directory_t *page_dir);
```

**实现要点**：
- 创建独立的页目录（`vmm_create_page_directory()`）
- 分配内核栈和用户栈
- 映射用户栈到用户地址空间
- 初始化标准文件描述符（stdin/stdout/stderr）
- 设置初始上下文为 `usermode_wrapper`

#### 4. ELF 程序加载器（`/src/kernel/elf.c`）

```c
// 验证 ELF 文件头
bool elf_validate_header(const void *elf_data);

// 加载 ELF 文件到内存
uint32_t elf_load(const char *path, page_directory_t *page_dir);

// 获取程序入口点
uint32_t elf_get_entry(const void *elf_data);
```

**支持特性**：
- 32位 ELF 可执行文件
- 多个 LOAD 段支持
- 自动页面分配和映射
- 从文件系统加载程序

### 系统调用架构

#### 1. 系统调用入口（`/src/kernel/syscall_asm.asm`）

```asm
syscall_handler:
    ; 保存用户态寄存器
    pushad
    push ds
    push es
    push fs
    push gs
    
    ; 切换到内核数据段
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    
    ; 调用 C 处理函数
    call syscall_dispatcher
    
    ; 恢复用户态寄存器
    pop gs
    pop fs
    pop es
    pop ds
    popad
    
    iret
```

#### 2. 系统调用分发器（`/src/kernel/syscall.c`）

支持的系统调用类别：
- **进程管理**：fork, exec, exit, getpid, yield
- **文件 I/O**：open, close, read, write, lseek
- **目录操作**：mkdir, chdir, getcwd, readdir
- **内存管理**：brk, mmap（预留）

#### 3. 用户态封装（`/userland/lib/syscall.h`）

```c
// 系统调用内联函数
static inline void exit(int code) {
    syscall1(SYS_EXIT, (uint32_t)code);
}

static inline int fork(void) {
    return (int)syscall0(SYS_FORK);
}

static inline int exec(const char *path) {
    return (int)syscall1(SYS_EXECVE, (uint32_t)path);
}

// 便利函数
static inline void print(const char *msg) {
    size_t len = strlen_simple(msg);
    write(STDOUT_FILENO, msg, (uint32_t)len);
}
```

### 进程执行流程

#### 1. 用户程序加载和执行

```
Shell 输入 "exec /path/to/program.elf"
    ↓
fork() 创建子进程
    ↓
子进程调用 sys_exec()
    ↓
elf_load() 加载 ELF 文件
    ↓
设置用户态上下文（CS=0x1B, SS=0x23）
    ↓
task_enter_usermode() 切换到用户态
    ↓
用户程序开始执行
```

#### 2. 系统调用处理

```
用户程序调用 print("Hello")
    ↓
INT 0x80 触发系统调用
    ↓
CPU 自动切换到 Ring 0，加载内核栈
    ↓
syscall_handler 保存用户态上下文
    ↓
syscall_dispatcher 分发到具体实现
    ↓
sys_write() 处理写操作
    ↓
IRET 返回用户态
```

### 用户程序示例

#### Hello World 程序（`/userland/helloworld/hello.c`）

```c
#include <syscall.h>

void _start(void) {
    print("Hello, World!\n");
    exit(0);
}
```

**编译和链接**：
```bash
i686-elf-gcc -c hello.c -o hello.o
i686-elf-ld hello.o -T user.ld -o hello.elf
```

#### 执行方式

在内核 Shell 中：
```bash
CastorOS> exec /userland/helloworld/hello.elf
```

### 内存布局

#### 用户进程内存映射

| 地址范围 | 用途 | 权限 |
|---------|------|------|
| 0x00000000-0x7FFFFFFF | 用户空间 | Ring 3 |
| 0x80000000-0xFFFFFFFF | 内核空间 | Ring 0 |

#### 用户栈配置

- **位置**：用户空间顶部（0x7FFFE000 - 0x80000000）
- **大小**：16KB（4页）
- **权限**：PAGE_PRESENT \| PAGE_WRITE \| PAGE_USER
- **增长方向**：向下增长

### 特权级保护验证

#### 测试场景

1. **用户程序无法执行特权指令**
   - CLI/STI 指令会触发 #GP 异常
   - 直接访问硬件端口会失败

2. **内存保护**
   - 用户程序无法访问内核内存
   - 页表权限位正确设置

3. **系统调用安全**
   - 参数验证和边界检查
   - 用户指针安全性检查
