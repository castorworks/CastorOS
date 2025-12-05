# 同步原语

## 概述

多任务系统中，多个任务可能同时访问共享资源，需要同步原语来保证数据一致性。CastorOS 提供三种主要的同步机制：

1. **自旋锁 (Spinlock)**: 忙等待，适用于短临界区
2. **互斥锁 (Mutex)**: 阻塞等待，适用于长临界区
3. **信号量 (Semaphore)**: 计数资源控制

## 自旋锁

### 原理

自旋锁通过原子操作保护临界区，等待时 CPU 空转（忙等待）。

### 实现

```c
typedef struct {
    volatile uint32_t locked;
} spinlock_t;

#define SPINLOCK_INIT { .locked = 0 }

void spinlock_init(spinlock_t *lock) {
    lock->locked = 0;
}

void spinlock_lock(spinlock_t *lock) {
    while (__sync_lock_test_and_set(&lock->locked, 1)) {
        // 忙等待
        __asm__ volatile ("pause");  // 降低功耗
    }
}

void spinlock_unlock(spinlock_t *lock) {
    __sync_lock_release(&lock->locked);
}
```

### 汇编实现（使用 xchg）

```asm
; bool spinlock_try_lock(spinlock_t *lock)
spinlock_try_lock:
    mov eax, 1
    xchg eax, [edi]      ; 原子交换
    test eax, eax        ; 检查旧值
    setz al              ; 如果旧值为 0，返回 true
    ret

; void spinlock_lock(spinlock_t *lock)
spinlock_lock:
.retry:
    mov eax, 1
    xchg eax, [edi]
    test eax, eax
    jnz .spin
    ret
.spin:
    pause                ; 减少总线流量
    cmp dword [edi], 0   ; 先读取检查
    jne .spin
    jmp .retry
```

### 中断安全自旋锁

在持有自旋锁时，需要禁用中断以防止死锁：

```c
void spinlock_lock_irqsave(spinlock_t *lock, bool *irq_state) {
    // 保存并禁用中断
    *irq_state = irq_enabled();
    cli();
    
    // 获取锁
    spinlock_lock(lock);
}

void spinlock_unlock_irqrestore(spinlock_t *lock, bool irq_state) {
    // 释放锁
    spinlock_unlock(lock);
    
    // 恢复中断状态
    if (irq_state) sti();
}
```

### 使用场景

```c
// 保护内核数据结构
static spinlock_t task_list_lock;

void add_task(task_t *task) {
    bool irq;
    spinlock_lock_irqsave(&task_list_lock, &irq);
    
    // 临界区：修改任务链表
    task->next = task_list;
    task_list = task;
    
    spinlock_unlock_irqrestore(&task_list_lock, irq);
}
```

## 互斥锁

### 原理

互斥锁在无法获取时让任务睡眠，释放 CPU 给其他任务。

### 实现

```c
typedef struct {
    volatile uint32_t locked;
    volatile task_t *owner;      // 持有者（支持递归）
    volatile uint32_t recursion; // 递归计数
    task_t *wait_queue;          // 等待队列
    spinlock_t wait_lock;        // 保护等待队列
} mutex_t;

void mutex_init(mutex_t *mutex) {
    mutex->locked = 0;
    mutex->owner = NULL;
    mutex->recursion = 0;
    mutex->wait_queue = NULL;
    spinlock_init(&mutex->wait_lock);
}

void mutex_lock(mutex_t *mutex) {
    task_t *current = get_current_task();
    
    // 检查递归获取
    if (mutex->owner == current) {
        mutex->recursion++;
        return;
    }
    
    // 尝试获取锁
    while (__sync_lock_test_and_set(&mutex->locked, 1)) {
        // 加入等待队列
        bool irq;
        spinlock_lock_irqsave(&mutex->wait_lock, &irq);
        
        current->next_waiting = mutex->wait_queue;
        mutex->wait_queue = current;
        current->state = TASK_BLOCKED;
        
        spinlock_unlock_irqrestore(&mutex->wait_lock, irq);
        
        // 让出 CPU
        schedule();
    }
    
    mutex->owner = current;
    mutex->recursion = 1;
}

void mutex_unlock(mutex_t *mutex) {
    task_t *current = get_current_task();
    
    if (mutex->owner != current) {
        panic("mutex_unlock: not owner");
    }
    
    // 处理递归
    if (--mutex->recursion > 0) {
        return;
    }
    
    mutex->owner = NULL;
    __sync_lock_release(&mutex->locked);
    
    // 唤醒等待者
    bool irq;
    spinlock_lock_irqsave(&mutex->wait_lock, &irq);
    
    task_t *waiter = mutex->wait_queue;
    if (waiter) {
        mutex->wait_queue = waiter->next_waiting;
        waiter->state = TASK_READY;
        enqueue_ready(waiter);
    }
    
    spinlock_unlock_irqrestore(&mutex->wait_lock, irq);
}
```

### 非阻塞尝试

```c
bool mutex_trylock(mutex_t *mutex) {
    task_t *current = get_current_task();
    
    if (mutex->owner == current) {
        mutex->recursion++;
        return true;
    }
    
    if (__sync_lock_test_and_set(&mutex->locked, 1) == 0) {
        mutex->owner = current;
        mutex->recursion = 1;
        return true;
    }
    
    return false;
}
```

## 信号量

### 原理

信号量维护一个计数器，表示可用资源数量。

```c
typedef struct {
    volatile int count;
    task_t *wait_queue;
    spinlock_t lock;
} semaphore_t;

void semaphore_init(semaphore_t *sem, int initial) {
    sem->count = initial;
    sem->wait_queue = NULL;
    spinlock_init(&sem->lock);
}
```

### P 操作（等待/减少）

```c
void semaphore_wait(semaphore_t *sem) {
    bool irq;
    spinlock_lock_irqsave(&sem->lock, &irq);
    
    while (sem->count <= 0) {
        // 加入等待队列
        task_t *current = get_current_task();
        current->next_waiting = sem->wait_queue;
        sem->wait_queue = current;
        current->state = TASK_BLOCKED;
        
        spinlock_unlock_irqrestore(&sem->lock, irq);
        schedule();
        spinlock_lock_irqsave(&sem->lock, &irq);
    }
    
    sem->count--;
    spinlock_unlock_irqrestore(&sem->lock, irq);
}
```

### V 操作（信号/增加）

```c
void semaphore_signal(semaphore_t *sem) {
    bool irq;
    spinlock_lock_irqsave(&sem->lock, &irq);
    
    sem->count++;
    
    // 唤醒一个等待者
    task_t *waiter = sem->wait_queue;
    if (waiter) {
        sem->wait_queue = waiter->next_waiting;
        waiter->state = TASK_READY;
        enqueue_ready(waiter);
    }
    
    spinlock_unlock_irqrestore(&sem->lock, irq);
}
```

### 常见用途

```c
// 二值信号量（互斥）
semaphore_t mutex;
semaphore_init(&mutex, 1);

// 计数信号量（资源池）
#define BUFFER_SIZE 10
semaphore_t empty_slots;
semaphore_t full_slots;
semaphore_init(&empty_slots, BUFFER_SIZE);
semaphore_init(&full_slots, 0);

// 生产者
void producer(void) {
    semaphore_wait(&empty_slots);  // 等待空槽
    // ... 放入数据 ...
    semaphore_signal(&full_slots); // 增加满槽
}

// 消费者
void consumer(void) {
    semaphore_wait(&full_slots);   // 等待满槽
    // ... 取出数据 ...
    semaphore_signal(&empty_slots); // 增加空槽
}
```

## 读写锁

### 实现

```c
typedef struct {
    volatile int readers;        // 当前读者数
    volatile bool writer;        // 是否有写者
    spinlock_t lock;
    task_t *reader_queue;
    task_t *writer_queue;
} rwlock_t;

void rwlock_read_lock(rwlock_t *rw) {
    bool irq;
    spinlock_lock_irqsave(&rw->lock, &irq);
    
    while (rw->writer || rw->writer_queue) {
        // 等待写者
        block_on_queue(&rw->reader_queue);
        spinlock_lock_irqsave(&rw->lock, &irq);
    }
    
    rw->readers++;
    spinlock_unlock_irqrestore(&rw->lock, irq);
}

void rwlock_read_unlock(rwlock_t *rw) {
    bool irq;
    spinlock_lock_irqsave(&rw->lock, &irq);
    
    if (--rw->readers == 0 && rw->writer_queue) {
        wake_one(&rw->writer_queue);
    }
    
    spinlock_unlock_irqrestore(&rw->lock, irq);
}

void rwlock_write_lock(rwlock_t *rw) {
    bool irq;
    spinlock_lock_irqsave(&rw->lock, &irq);
    
    while (rw->writer || rw->readers > 0) {
        block_on_queue(&rw->writer_queue);
        spinlock_lock_irqsave(&rw->lock, &irq);
    }
    
    rw->writer = true;
    spinlock_unlock_irqrestore(&rw->lock, irq);
}

void rwlock_write_unlock(rwlock_t *rw) {
    bool irq;
    spinlock_lock_irqsave(&rw->lock, &irq);
    
    rw->writer = false;
    
    // 优先唤醒写者
    if (rw->writer_queue) {
        wake_one(&rw->writer_queue);
    } else {
        wake_all(&rw->reader_queue);
    }
    
    spinlock_unlock_irqrestore(&rw->lock, irq);
}
```

## 死锁预防

### 常见死锁原因

1. **循环等待**: A 等待 B，B 等待 A
2. **持有并等待**: 持有锁 A 时请求锁 B
3. **不可抢占**: 无法强制释放锁
4. **互斥**: 资源只能由一个任务使用

### 预防策略

```c
// 1. 锁排序：总是按照固定顺序获取锁
#define LOCK_ORDER_A  1
#define LOCK_ORDER_B  2

void safe_lock_both(mutex_t *a, mutex_t *b) {
    if (LOCK_ORDER_A < LOCK_ORDER_B) {
        mutex_lock(a);
        mutex_lock(b);
    } else {
        mutex_lock(b);
        mutex_lock(a);
    }
}

// 2. 尝试锁定：失败时释放已持有的锁
bool try_lock_both(mutex_t *a, mutex_t *b) {
    mutex_lock(a);
    if (!mutex_trylock(b)) {
        mutex_unlock(a);
        return false;
    }
    return true;
}

// 3. 超时锁定
bool mutex_lock_timeout(mutex_t *m, uint32_t timeout_ms);
```

## 选择指南

| 同步原语 | 等待方式 | 适用场景 | 开销 |
|---------|---------|---------|------|
| 自旋锁 | 忙等待 | 短临界区、中断上下文 | 低（无上下文切换）|
| 互斥锁 | 阻塞 | 长临界区、可能睡眠 | 中（可能切换）|
| 信号量 | 阻塞 | 资源计数、生产者消费者 | 中 |
| 读写锁 | 阻塞 | 读多写少的场景 | 较高 |

## 最佳实践

1. **最小化临界区**: 只保护必要的代码
2. **避免嵌套锁**: 如必须嵌套，确保顺序一致
3. **中断安全**: 在中断上下文只能用自旋锁
4. **不要在持锁时睡眠**: 除非使用互斥锁
5. **使用正确的同步原语**: 根据场景选择

