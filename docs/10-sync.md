# 阶段 10: 同步机制

## 概述

CastorOS 实现了三种核心同步原语：自旋锁（Spinlock）、互斥锁（Mutex）和信号量（Semaphore），用于在多任务环境中保护共享资源，防止竞态条件。

## 目标

- ✅ 实现自旋锁（Spinlock）- 基于原子操作的忙等待锁
- ✅ 实现互斥锁（Mutex）- 支持递归和任务阻塞的互斥锁
- ✅ 实现信号量（Semaphore）- 支持计数的同步原语
- ⏳ 在关键系统组件中应用同步保护

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

**使用场景**：
- ✅ 短临界区（几微秒内完成）
- ✅ 中断上下文中
- ✅ 保护简单的共享数据结构
- ❌ 长时间持有（会浪费 CPU 资源）
- ❌ 需要阻塞等待的场景

**实现细节**：
```c
// 原子交换实现
static inline uint32_t atomic_xchg(volatile uint32_t *addr, uint32_t new_value) {
    uint32_t old_value;
    __asm__ volatile("xchg %0, %1"
                     : "=r"(old_value), "+m"(*addr)
                     : "0"(new_value)
                     : "memory");
    return old_value;
}

// 自旋等待时使用 pause 指令
void spinlock_lock(spinlock_t *lock) {
    while (!spinlock_try_lock(lock)) {
        __asm__ volatile("pause");  // 优化忙等待
    }
}
```

---

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

**API 接口**：
```c
void mutex_init(mutex_t *mutex);
void mutex_lock(mutex_t *mutex);
bool mutex_try_lock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);
bool mutex_is_locked(const mutex_t *mutex);
```

**使用场景**：
- ✅ 长临界区（毫秒级或更长）
- ✅ 可能睡眠的代码段
- ✅ 需要递归锁定的场景
- ❌ 中断上下文（会导致死锁）
- ❌ 需要高性能的短临界区

**防止 Lost Wakeup 的关键代码**：
```c
void mutex_lock(mutex_t *mutex) {
    while (1) {
        bool irq_state = interrupts_disable();
        spinlock_lock(&mutex->lock);
        
        if (!mutex->locked) {
            // 获取成功
            mutex->locked = true;
            mutex->owner_pid = current->pid;
            mutex->recursion = 1;
            spinlock_unlock(&mutex->lock);
            interrupts_restore(irq_state);
            return;
        }
        
        // 关键：在持有 spinlock 时设置任务状态为 BLOCKED
        // 这样确保在调度前不会有其他线程唤醒我们
        current->state = TASK_BLOCKED;
        
        spinlock_unlock(&mutex->lock);
        task_schedule();  // 切换到其他任务
        interrupts_restore(irq_state);
    }
}
```

---

### 3. 信号量（Semaphore）

**位置**：`src/kernel/sync/semaphore.c`、`src/include/kernel/sync/semaphore.h`

**实现特点**：

```c
typedef struct {
    spinlock_t lock;  // 保护信号量内部状态的自旋锁
    int32_t count;    // 计数值
} semaphore_t;
```

**核心机制**：
- **基于 spinlock 实现**：使用自旋锁保护信号量内部状态
- **计数机制**：支持多个资源的并发访问控制
- **任务阻塞**：count ≤ 0 时，任务进入 BLOCKED 状态
- **防止溢出**：限制 count 最大值为 INT32_MAX

**API 接口**：
```c
void semaphore_init(semaphore_t *sem, int32_t initial_count);
void semaphore_wait(semaphore_t *sem);    // P 操作
bool semaphore_try_wait(semaphore_t *sem);
void semaphore_signal(semaphore_t *sem);  // V 操作
int32_t semaphore_get_value(semaphore_t *sem);
```

**使用场景**：
- ✅ 资源计数（如固定大小的缓冲池）
- ✅ 生产者-消费者模式
- ✅ 限制并发访问数量
- ✅ 事件通知机制

**实现细节**：
```c
void semaphore_wait(semaphore_t *sem) {
    while (1) {
        bool irq_state = interrupts_disable();
        spinlock_lock(&sem->lock);
        
        // 尝试获取资源
        if (sem->count > 0) {
            sem->count--;
            spinlock_unlock(&sem->lock);
            interrupts_restore(irq_state);
            return;
        }
        
        // 无法获取，阻塞任务
        current->state = TASK_BLOCKED;
        spinlock_unlock(&sem->lock);
        task_schedule();
        interrupts_restore(irq_state);
    }
}

void semaphore_signal(semaphore_t *sem) {
    bool irq_state = interrupts_disable();
    spinlock_lock(&sem->lock);
    
    // 防止整数溢出
    if (sem->count < INT32_MAX) {
        sem->count++;
    }
    
    spinlock_unlock(&sem->lock);
    task_wakeup(sem);  // 唤醒等待的任务
    interrupts_restore(irq_state);
}
```

---

## 需要同步保护的关键区域

根据业界操作系统的最佳实践（Linux、FreeBSD、xv6 等），以下是 CastorOS 中需要使用同步机制保护的关键区域：

### 1. 任务管理（Task Management）

**需要保护的数据结构**：
```c
// src/kernel/task.c
extern task_t task_pool[MAX_TASKS];     // 任务控制块池
static task_t *ready_queue_head;        // 就绪队列头
static task_t *ready_queue_tail;        // 就绪队列尾
static task_t *current_task;            // 当前运行任务
static uint32_t next_pid;               // 下一个 PID
```

**推荐方案**：
```c
// 全局任务管理锁
static spinlock_t task_lock;

// 保护的操作：
// - task_alloc() / task_free()
// - ready_queue_add() / ready_queue_remove()
// - task_get_by_pid()
// - PID 分配
```

**为什么使用 Spinlock**：
- 临界区很短（通常只是修改几个指针或状态）
- 任务调度本身就需要禁用中断
- 避免在调度器中使用会阻塞的锁（会导致死锁）

**实现示例**：
```c
task_t* task_alloc(void) {
    spinlock_lock(&task_lock);
    
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_pool[i].state == TASK_UNUSED) {
            task_pool[i].state = TASK_READY;
            task_pool[i].pid = next_pid++;
            spinlock_unlock(&task_lock);
            return &task_pool[i];
        }
    }
    
    spinlock_unlock(&task_lock);
    return NULL;
}
```

---

### 2. 内存管理（Memory Management）

#### 2.1 物理内存管理器（PMM）

**需要保护的数据结构**：
```c
// src/mm/pmm.c
static uint32_t *bitmap;           // 页帧位图
static uint32_t total_frames;      // 总页帧数
static uint32_t used_frames;       // 已使用页帧数
```

**推荐方案**：
```c
static spinlock_t pmm_lock;

// 保护的操作：
// - pmm_alloc_frame()
// - pmm_free_frame()
// - 位图操作
```

**为什么使用 Spinlock**：
- 页帧分配/释放操作很快（位图操作）
- 可能在中断上下文中调用（如页错误处理）
- 关键路径，需要高性能

#### 2.2 堆内存分配器（Heap）

**需要保护的数据结构**：
```c
// src/mm/heap.c
static heap_block_t *first_block;  // 第一个内存块
static heap_block_t *last_block;   // 最后一个内存块
static uint32_t heap_end;          // 堆当前结束地址
```

**推荐方案**：
```c
static mutex_t heap_mutex;

// 保护的操作：
// - kmalloc() / kfree()
// - expand()（可能需要分配物理页）
// - coalesce() / split()
```

**为什么使用 Mutex**：
- 堆分配可能涉及复杂操作（合并、分裂、扩展）
- expand() 可能需要调用 PMM 和 VMM（嵌套锁）
- 不会在中断上下文中调用
- 临界区可能较长

**注意事项**：
- 确保 kmalloc/kfree 不会在持有其他锁时调用（避免死锁）
- 或者使用分离的快速路径和慢路径（快路径用 spinlock，慢路径用 mutex）

#### 2.3 虚拟内存管理器（VMM）

**需要保护的数据结构**：
```c
// 每个进程的页目录
page_directory_t *page_dir;

// 内核页目录（全局共享）
extern page_directory_t *kernel_page_directory;
```

**推荐方案**：
```c
// 方案 1: 每个页目录一把锁
typedef struct {
    page_directory_t *dir;
    spinlock_t lock;
} protected_page_dir_t;

// 方案 2: 全局 VMM 锁（简单但性能较低）
static spinlock_t vmm_lock;

// 保护的操作：
// - vmm_map_page() / vmm_unmap_page()
// - 页表创建/销毁
// - TLB 刷新（需要协调多核）
```

**为什么使用 Spinlock**：
- 页表操作通常很快
- 可能在页错误处理中调用（中断上下文）
- 关键路径

---

### 3. 文件系统（File System）

#### 3.1 VFS 挂载表

**需要保护的数据结构**：
```c
// src/fs/vfs.c
static vfs_mount_entry_t mount_table[MAX_MOUNTS];
static uint32_t mount_count;
static fs_node_t *fs_root;
```

**推荐方案**：
```c
static mutex_t vfs_mount_mutex;

// 保护的操作：
// - vfs_mount() / vfs_unmount()
// - mount_table 访问
```

**为什么使用 Mutex**：
- 挂载/卸载操作不频繁
- 可能涉及文件系统初始化（耗时）
- 不会在中断上下文中调用

#### 3.2 文件节点（Inode）分配

**需要保护的数据结构**：
```c
// src/fs/ramfs.c
static uint32_t next_inode;  // 全局 inode 计数器
```

**推荐方案**：
```c
static spinlock_t inode_alloc_lock;

// 保护的操作：
// - inode 分配
// - next_inode 递增
```

**为什么使用 Spinlock**：
- 操作非常简单（递增计数器）
- 高频操作

#### 3.3 目录操作

**需要保护的数据结构**：
```c
// src/fs/ramfs.c
typedef struct ramfs_dir {
    ramfs_dirent_t *entries;  // 目录项链表
    uint32_t count;
} ramfs_dir_t;
```

**推荐方案**：
```c
// 每个目录一把锁（细粒度锁）
typedef struct ramfs_dir {
    ramfs_dirent_t *entries;
    uint32_t count;
    mutex_t lock;  // 目录锁
} ramfs_dir_t;

// 保护的操作：
// - create / mkdir / unlink
// - readdir / finddir
// - 目录项链表修改
```

**为什么使用 Mutex**：
- 目录操作可能涉及内存分配
- 可能需要递归查找路径
- 不会在中断上下文中调用

#### 3.4 文件数据读写

**需要保护的数据结构**：
```c
// src/fs/ramfs.c
typedef struct ramfs_file {
    uint8_t *data;
    uint32_t size;
    uint32_t capacity;
} ramfs_file_t;
```

**推荐方案**：
```c
// 每个文件一把锁（细粒度锁）
typedef struct ramfs_file {
    uint8_t *data;
    uint32_t size;
    uint32_t capacity;
    mutex_t lock;  // 文件数据锁
} ramfs_file_t;

// 保护的操作：
// - read / write
// - 文件扩展
```

**为什么使用 Mutex**：
- 读写操作可能耗时较长
- 可能需要分配/释放内存（扩展文件）
- 允许多个读者（可以升级为读写锁）

---

### 4. 文件描述符表（FD Table）

**需要保护的数据结构**：
```c
// src/kernel/fd_table.c
typedef struct {
    fd_entry_t entries[MAX_FDS];
} fd_table_t;
```

**推荐方案**：
```c
// 每个进程的 fd_table 一把锁
typedef struct {
    fd_entry_t entries[MAX_FDS];
    spinlock_t lock;
} fd_table_t;

// 保护的操作：
// - fd_table_alloc()
// - fd_table_free()
// - fd_table_get()
```

**为什么使用 Spinlock**：
- FD 分配/查找操作很快
- 高频操作
- 不涉及阻塞

**特殊情况**：
- `fd_table_copy()` 需要同时持有源表和目标表的锁
- 注意锁的获取顺序，避免死锁（如：总是先锁 PID 小的进程）

---

### 5. 中断处理（Interrupt Handling）

**需要保护的数据结构**：
```c
// src/kernel/irq.c
static irq_handler_t irq_handlers[16];  // IRQ 处理程序表
```

**推荐方案**：
```c
static spinlock_t irq_lock;

// 保护的操作：
// - irq_register_handler()
// - irq_unregister_handler()
```

**为什么使用 Spinlock**：
- 注册/注销操作很快
- 在中断上下文中可能需要访问（虽然不推荐）

**注意事项**：
- 中断处理程序本身通常不需要锁（硬件保证不会重入）
- 但 IRQ 处理程序可能访问共享数据结构，需要使用 `spinlock_lock_irqsave()`

---

### 6. 设备驱动（Device Drivers）

#### 6.1 串口驱动

**需要保护的数据结构**：
```c
// src/drivers/serial.c
// 串口发送缓冲区（如果实现了缓冲）
static char tx_buffer[SERIAL_BUFFER_SIZE];
static volatile uint32_t tx_head, tx_tail;
```

**推荐方案**：
```c
static spinlock_t serial_lock;

// 保护的操作：
// - serial_putc()
// - 缓冲区操作
```

**为什么使用 Spinlock**：
- 串口操作可能在中断上下文调用（如 klog）
- 操作很快
- 需要禁用中断（避免中断中打印日志导致死锁）

#### 6.2 键盘驱动

**需要保护的数据结构**：
```c
// src/drivers/keyboard.c
static char key_buffer[KEY_BUFFER_SIZE];
static volatile uint32_t key_head, key_tail;
```

**推荐方案**：
```c
static spinlock_t keyboard_lock;

// 保护的操作：
// - 键盘中断处理程序（写入缓冲区）
// - keyboard_getc()（读取缓冲区）
```

**为什么使用 Spinlock**：
- 中断上下文和用户上下文都会访问
- 操作很快（环形缓冲区）

#### 6.3 ATA/磁盘驱动

**需要保护的数据结构**：
```c
// src/drivers/ata.c
typedef struct {
    bool busy;           // 是否正在执行操作
    uint8_t *buffer;     // DMA 缓冲区
    bool irq_received;   // 是否收到中断
} ata_device_t;
```

**推荐方案**：
```c
typedef struct {
    bool busy;
    uint8_t *buffer;
    bool irq_received;
    mutex_t lock;        // 设备锁
    semaphore_t irq_sem; // 中断信号量（用于等待中断）
} ata_device_t;

// 使用模式：
void ata_read_sector(uint32_t lba, uint8_t *buffer) {
    mutex_lock(&device->lock);
    
    // 发送读命令
    ata_send_command(ATA_CMD_READ);
    
    // 等待中断（P 操作）
    semaphore_wait(&device->irq_sem);
    
    // 拷贝数据
    memcpy(buffer, device->buffer, 512);
    
    mutex_unlock(&device->lock);
}

// 中断处理程序
void ata_irq_handler() {
    // 通知等待的任务（V 操作）
    semaphore_signal(&device->irq_sem);
}
```

**为什么使用 Mutex + Semaphore**：
- Mutex 保护设备状态（一次只能有一个操作）
- Semaphore 用于等待中断（同步机制）
- 磁盘操作可能耗时较长

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

### 避免死锁的规则

1. **锁顺序规则**：如果需要获取多个锁，总是按相同的顺序获取
   ```c
   // 正确：总是先锁 PID 小的任务
   if (task1->pid < task2->pid) {
       mutex_lock(&task1->lock);
       mutex_lock(&task2->lock);
   } else {
       mutex_lock(&task2->lock);
       mutex_lock(&task1->lock);
   }
   ```

2. **持有时间最小化**：尽快释放锁
   ```c
   // 正确：在锁外进行耗时操作
   mutex_lock(&lock);
   data = shared_data;  // 快速拷贝
   mutex_unlock(&lock);
   
   process_data(data);  // 耗时操作在锁外
   ```

3. **避免嵌套中断锁**：使用 `spinlock_lock_irqsave()` 避免中断死锁
   ```c
   bool irq_state;
   spinlock_lock_irqsave(&lock, &irq_state);
   // 临界区
   spinlock_unlock_irqrestore(&lock, irq_state);
   ```

4. **不要在持有 Spinlock 时调用可能阻塞的函数**
   ```c
   // 错误示例
   spinlock_lock(&lock);
   kmalloc(1024);  // 可能会阻塞！
   spinlock_unlock(&lock);
   
   // 正确示例
   void *ptr = kmalloc(1024);  // 先分配
   spinlock_lock(&lock);
   // 使用 ptr
   spinlock_unlock(&lock);
   ```

5. **使用 try_lock 避免死锁**
   ```c
   if (!mutex_try_lock(&lock)) {
       // 无法获取锁，返回错误而不是等待
       return -EBUSY;
   }
   ```

### 性能优化建议

1. **细粒度锁**：每个对象一把锁，而不是全局大锁
   - ✅ 每个文件一把锁 → 允许并发访问不同文件
   - ❌ 整个文件系统一把锁 → 串行化所有文件操作

2. **读写锁**：如果读操作远多于写操作，考虑实现 rwlock
   ```c
   // 未来可以实现
   rwlock_t lock;
   rwlock_read_lock(&lock);   // 多个读者可以同时持有
   rwlock_read_unlock(&lock);
   rwlock_write_lock(&lock);  // 写者独占
   rwlock_write_unlock(&lock);
   ```

3. **无锁数据结构**：对于特定场景，考虑使用无锁算法
   - 环形缓冲区（单生产者单消费者）
   - 原子计数器
   - RCU（Read-Copy-Update）

4. **锁分离**：将一个大锁拆分成多个小锁
   ```c
   // 优化前：一把大锁
   static spinlock_t task_lock;
   
   // 优化后：分离的锁
   static spinlock_t task_alloc_lock;  // 保护任务分配
   static spinlock_t ready_queue_lock; // 保护就绪队列
   static spinlock_t pid_lock;         // 保护 PID 分配
   ```

---

## 调试技巧

### 1. 死锁检测

添加锁持有者信息：
```c
typedef struct {
    spinlock_t lock;
    uint32_t owner_cpu;   // 哪个 CPU 持有锁
    uint32_t owner_pid;   // 哪个任务持有锁
    const char *file;     // 获取锁的文件
    int line;             // 获取锁的行号
} debug_spinlock_t;

#define spinlock_lock_debug(lock) \
    _spinlock_lock_debug(lock, __FILE__, __LINE__)
```

### 2. 锁统计

记录锁的使用情况：
```c
typedef struct {
    uint64_t acquire_count;    // 获取次数
    uint64_t contention_count; // 竞争次数
    uint64_t max_hold_time_us; // 最长持有时间
} lock_stats_t;
```

### 3. 锁验证

在调试模式下检测常见错误：
- 重复获取（非递归锁）
- 错误的解锁顺序
- 持有锁时睡眠
- 中断上下文中使用 Mutex
