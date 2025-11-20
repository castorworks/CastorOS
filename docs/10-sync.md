# 阶段 10: 同步机制

## 概述

CastorOS 实现了三种核心同步原语：自旋锁（Spinlock）、互斥锁（Mutex）和信号量（Semaphore），用于在多任务环境中保护共享资源，防止竞态条件。

## 目标

- ✅ **实现自旋锁（Spinlock）** - 基于原子操作的忙等待锁
- ✅ **实现互斥锁（Mutex）** - 支持递归和任务阻塞的互斥锁
- ✅ **实现信号量（Semaphore）** - 支持计数的同步原语
- 🔄 **在关键系统组件中应用同步保护**
  - ✅ 任务管理 (Task Management)
  - ✅ 内存管理 (PMM, VMM, Heap)
  - ✅ 文件系统 (VFS Mount, RamFS Inode/Dir/File)
  - ❌ 文件描述符表 (FD Table)
  - ❌ 设别驱动 (Serial, Keyboard, ATA)
  - ❌ 中断管理 (IRQ Registry)

---

## 当前实现

### 1. 自旋锁（Spinlock）

**位置**：`src/kernel/sync/spinlock.c`、`src/include/kernel/sync/spinlock.h`

**实现特点**：

```c
typedef struct {
    volatile uint32_t value;  // 0=未锁定, 1=已锁定
} spinlock_t;
```

**核心机制**：
- **原子操作**：使用 x86 `xchg` 指令实现原子交换，保证多核环境下的互斥访问
- **忙等待**：使用 `pause` 指令优化自旋等待，降低 CPU 功耗并提高性能
- **中断保护**：提供 `spinlock_lock_irqsave()` 和 `spinlock_unlock_irqrestore()` 防止中断死锁

**API 接口**：
```c
void spinlock_init(spinlock_t *lock);
void spinlock_lock(spinlock_t *lock);
bool spinlock_try_lock(spinlock_t *lock);
void spinlock_unlock(spinlock_t *lock);
void spinlock_lock_irqsave(spinlock_t *lock, bool *irq_state);
void spinlock_unlock_irqrestore(spinlock_t *lock, bool irq_state);
```

### 2. 互斥锁（Mutex）

**位置**：`src/kernel/sync/mutex.c`、`src/include/kernel/sync/mutex.h`

**实现特点**：

```c
typedef struct {
    spinlock_t lock;      // 保护 mutex 内部状态的自旋锁
    bool locked;          // 是否已锁定
    uint32_t owner_pid;   // 拥有者进程 ID
    uint32_t recursion;   // 递归计数
} mutex_t;
```

**核心机制**：
- **基于 spinlock 实现**：使用自旋锁保护 mutex 内部状态
- **递归支持**：同一任务可以多次获取同一互斥锁（递归计数）
- **任务阻塞**：无法获取锁时，任务进入 BLOCKED 状态并主动调度
- **防止 Lost Wakeup**：在持有自旋锁的情况下设置任务状态，确保不会丢失唤醒信号

### 3. 信号量（Semaphore）

**位置**：`src/kernel/sync/semaphore.c`、`src/include/kernel/sync/semaphore.h`

**实现特点**：

```c
typedef struct {
    spinlock_t lock;  // 保护信号量内部状态的自旋锁
    int32_t count;    // 计数值
} semaphore_t;
```

---

## 关键区域保护状态

### 1. 任务管理（Task Management）✅

**状态**：已实现
**锁类型**：`spinlock_t task_lock`
**位置**：`src/kernel/task.c`

保护了任务池分配、就绪队列操作和 PID 分配。使用自旋锁是因为调度器本身需要禁用中断，且临界区非常短。

### 2. 内存管理（Memory Management）✅

#### 2.1 物理内存管理器（PMM）✅
**状态**：已实现
**锁类型**：`spinlock_t pmm_lock`
**位置**：`src/mm/pmm.c`

保护页帧位图的访问。

#### 2.2 堆内存分配器（Heap）✅ (有变更)
**状态**：已实现
**锁类型**：`spinlock_t heap_lock` (文档原建议 Mutex)
**位置**：`src/mm/heap.c`

**变更说明**：当前实现使用了 `spinlock` 而不是 `mutex`。这意味着 `kmalloc` 不能在持有锁期间睡眠（例如等待物理页），且必须在中断禁用状态下运行。这对于内核堆来说通常是可以接受的，只要 `expand` 操作足够快。

#### 2.3 虚拟内存管理器（VMM）✅
**状态**：已实现
**锁类型**：`spinlock_t vmm_lock`
**位置**：`src/mm/vmm.c`

使用全局自旋锁保护页表映射操作。

### 3. 文件系统（File System）🔄

#### 3.1 VFS 挂载表 ✅
**状态**：已实现
**锁类型**：`mutex_t vfs_mount_mutex`
**位置**：`src/fs/vfs.c`

#### 3.2 RamFS 实现 ✅
- **Inode 分配** ✅：`spinlock_t inode_alloc_lock`
- **目录操作** ✅：`mutex_t lock` (在 `ramfs_dir_t` 中)
- **文件数据读写** ✅：`mutex_t lock` (在 `ramfs_file_t` 中)，保护文件内容读写和扩容操作。

### 4. 待实现/完善区域 ❌

以下区域尚未添加同步保护，需在后续开发中补充：

#### 4.1 文件描述符表 (FD Table)
- **现状**：无锁保护
- **风险**：多线程同时打开/关闭文件可能导致 FD 泄漏或冲突。

#### 4.2 设备驱动 (Drivers)
- **Serial**：`serial_putchar` 无锁，多核/中断并发输出可能导致字符交错。
- **Keyboard**：缓冲区读写存在竞态条件（代码注释已确认），需添加 Spinlock。
- **ATA**：无锁，并发磁盘操作可能导致指令冲突。

#### 4.3 中断管理 (IRQ)
- **IRQ 注册**：`irq_register_handler` 无锁。

---

## 同步机制使用指南

### 选择正确的同步原语

| 场景 | 推荐原语 | 原因 |
|------|----------|------|
| 短临界区（< 1μs） | Spinlock | 避免上下文切换开销 |
| 长临界区（> 1ms） | Mutex | 避免浪费 CPU |
| 中断上下文 | Spinlock | Mutex 不能在中断中使用 |
| 可能睡眠的代码 | Mutex | Spinlock 不能睡眠 |
| 资源计数 | Semaphore | 支持多个资源 |
| 事件通知 | Semaphore | 初始化为 0 |

### 调试技巧

在开发过程中，如果遇到死锁或竞争问题，可以检查：
1. **锁顺序**：是否总是以相同的顺序获取多个锁？
2. **中断状态**：在中断处理程序中是否使用了 Mutex（错误）？
3. **持锁睡眠**：是否在持有 Spinlock 时调用了可能睡眠的函数（如 `kmalloc` 触发 `task_schedule`，虽然目前 `kmalloc` 是自旋锁版本，但在其他 OS 中要注意）？
