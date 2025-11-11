# 阶段 5: 进程管理

## 概述

本阶段将实现 CastorOS 的多任务支持，建立进程管理框架，使内核能够同时运行多个任务。我们将实现任务结构、上下文切换、进程调度器、内核线程支持和系统调用框架。

---

## 目标

- ✅ 定义进程控制块（PCB）结构
- ✅ 实现上下文切换机制
- ✅ 实现进程调度器（轮转调度）
- ✅ 支持创建内核线程

---

## 技术背景

### 什么是进程？

**进程（Process）** 是操作系统资源分配和调度的基本单位。

**进程的组成**：
- **代码段（Text Segment）**：程序的机器指令
- **数据段（Data Segment）**：全局变量、静态变量
- **堆（Heap）**：动态分配的内存
- **栈（Stack）**：函数调用、局部变量
- **进程控制块（PCB）**：进程的元数据

**进程状态**：
```
┌───────┐   Create   ┌────────┐   Schedule   ┌─────────┐
│ New   │ ─────────> │ Ready  │ ───────────> │ Running │
└───────┘            └────────┘              └─────────┘
                          ↑                       │
                          │                       │ Block
                          │                       ↓
                          │                  ┌─────────┐
                          └────────────────  │ Blocked │
                            Awake            └─────────┘
```

**五态模型**：
1. **新建（New）**：进程正在被创建
2. **就绪（Ready）**：进程等待 CPU 调度
3. **运行（Running）**：进程正在执行
4. **阻塞（Blocked）**：进程等待某个事件（如 I/O）
5. **退出（Terminated）**：进程执行完毕

---

### 什么是进程控制块（PCB）？

**PCB（Process Control Block）** 存储进程的所有信息，是操作系统管理进程的核心数据结构。

**PCB 的内容**：
- **进程标识符（PID）**：唯一标识进程
- **进程状态**：就绪、运行、阻塞等
- **CPU 寄存器**：上下文切换时保存/恢复
- **内存管理信息**：页目录基址、虚拟地址空间
- **调度信息**：优先级、时间片等
- **I/O 状态**：打开的文件、设备等
- **链表指针**：用于组织进程队列

**x86 上下文信息**：
```c
/* CPU 上下文（用于上下文切换） */
typedef struct {
    /* 通用寄存器 */
    uint32_t eax, ebx, ecx, edx;  // 通用寄存器
    uint32_t esi, edi, ebp;       // 索引和基址寄存器
    uint32_t esp;                 // 栈指针
    uint32_t eip;                 // 指令指针
    uint32_t eflags;              // 标志寄存器
    uint32_t cr3;                 // 页目录基址寄存器
    uint16_t cs, ds, es, fs, gs, ss;  // 段寄存器
} __attribute__((packed)) cpu_context_t;
```

---

### 什么是上下文切换？

**上下文切换（Context Switch）** 是保存当前进程状态，恢复另一个进程状态的过程。

**切换步骤**：
1. **保存当前进程上下文**
   - 保存 CPU 寄存器到当前进程的 PCB
   - 保存栈指针、程序计数器等
   
2. **选择下一个进程**
   - 调度器从就绪队列中选择进程
   
3. **恢复新进程上下文**
   - 从新进程的 PCB 恢复 CPU 寄存器
   - 切换页目录（如果需要）
   - 跳转到新进程的程序计数器

**性能考虑**：
- 上下文切换开销：约 1-10 微秒
- 包括：保存/恢复寄存器、切换页表、刷新 TLB
- 频繁切换会降低性能

---

### 什么是进程调度？

**进程调度（Process Scheduling）** 决定哪个进程应该获得 CPU 时间。

**调度算法分类**：

1. **非抢占式调度**：
   - 进程运行直到主动放弃 CPU
   - 简单但响应慢
   
2. **抢占式调度**：
   - 定时器中断可以打断进程
   - 响应快，适合交互式系统

**常见调度算法**：

| 算法 | 特点 | 优点 | 缺点 |
|------|------|------|------|
| **先来先服务（FCFS）** | 按到达顺序 | 简单 | 平均等待时间长 |
| **轮转调度（RR）** | 固定时间片轮流 | 公平、响应快 | 上下文切换开销 |
| **优先级调度** | 按优先级 | 重要任务优先 | 可能饥饿 |
| **多级反馈队列** | 动态优先级 | 灵活、高效 | 复杂 |

**本阶段实现**：轮转调度（Round Robin）
- 每个进程获得固定时间片（如 10ms）
- 时间片用完后切换到下一个进程
- 简单且公平

---

## 实现步骤

### 步骤 1: 定义进程控制块（PCB）

**文件**: `src/include/kernel/task.h`
**文件**: `src/kernel/task.c`

### 步骤 2: 实现上下文切换（汇编）

**文件**: `src/kernel/task_asm.asm`

### 步骤 3: 增加 interrupt.h

**文件**: `src/include/kernel/interrupt.h`

### 步骤 4: 集成到内核

#### 4.1 更新定时器驱动

**修改文件**: `src/drivers/timer.c`

在定时器中断处理函数中调用任务调度：

```c
#include <kernel/task.h>  // 添加头文件

static void timer_callback(registers_t *regs) {
    (void)regs;
    timer_ticks++;
    
    /* 调用任务调度器 */
    task_timer_tick();
}
```

---

#### 4.2 更新内核主函数

**修改文件**: `src/kernel/kernel.c`

```c
// ============================================================================
// kernel.c - 内核主函数
// ============================================================================

#include <drivers/vga.h>
#include <drivers/serial.h>
#include <drivers/timer.h>
#include <drivers/keyboard.h>

#include <kernel/multiboot.h>
#include <kernel/version.h>

#include <lib/kprintf.h>
#include <lib/klog.h>

#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/irq.h>
#include <kernel/isr.h>
#include <kernel/task.h>

#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/heap.h>

#include <tests/test_runner.h>

void test_task1(void);
void test_task2(void);

// 内核主函数
void kernel_main(multiboot_info_t* mbi) {
    // ========================================================================
    // 阶段 0: 早期初始化
    // ========================================================================    
    vga_init(); // 初始化 VGA
    serial_init(); // 初始化串口

    // ========================================================================
    // 启动信息
    // ========================================================================
    kprintf("================================================================================\n");
    kprintf("Welcome to CastorOS!\n");
    kprintf("Version v%s\n", KERNEL_VERSION);
    kprintf("Compiled on: %s %s\n", __DATE__, __TIME__);
    kprintf("================================================================================\n");

    // ========================================================================
    // 阶段 1: CPU 基础架构（CPU Architecture）
    // ========================================================================
    LOG_INFO_MSG("[Stage 1] Initializing CPU architecture...\n");
    
    // 1.1 初始化 GDT（Global Descriptor Table）
    gdt_init();
    LOG_DEBUG_MSG("  [1.1] GDT initialized\n");
    
    // ========================================================================
    // 阶段 2: 中断系统（Interrupt System）
    // ========================================================================
    LOG_INFO_MSG("[Stage 2] Initializing interrupt system...\n");
    
    // 2.1 初始化 IDT（Interrupt Descriptor Table）
    idt_init();
    LOG_DEBUG_MSG("  [2.1] IDT initialized\n");
    
    // 2.2 初始化 ISR（Interrupt Service Routines - 异常处理）
    isr_init();
    LOG_DEBUG_MSG("  [2.2] ISR initialized (Exception handlers)\n");
    
    // 2.3 初始化 IRQ（Hardware Interrupt Requests - 硬件中断）
    irq_init();
    LOG_DEBUG_MSG("  [2.3] IRQ initialized (Hardware interrupts)\n");

    // ========================================================================
    // 阶段 3: 内存管理（Memory Management）
    // ========================================================================   
    LOG_INFO_MSG("[Stage 3] Initializing memory management...\n");
    
    // 3.0 显示 Multiboot 内存信息
    if (mbi && (mbi->flags & 0x01)) {
        kprintf("  Memory detected: %u KB (lower) + %u KB (upper) = %u MB\n",
                mbi->mem_lower, mbi->mem_upper,
                (mbi->mem_lower + mbi->mem_upper) / 1024);
    } else {
        LOG_WARN_MSG("  Warning: Memory info not available from bootloader\n");
    }
    
    // 3.1 初始化 PMM（Physical Memory Manager - 物理内存管理）阶段1
    //     解析内存映射，记录所有可用区域
    pmm_init(mbi);
    LOG_DEBUG_MSG("  [3.1] PMM phase 1 initialized\n");
    
    // 3.2 初始化 VMM（Virtual Memory Manager - 虚拟内存管理）
    vmm_init();
    LOG_DEBUG_MSG("  [3.2] VMM initialized\n");
    
    // 3.5 初始化 Heap（堆内存分配器）
    // 堆起始地址：PMM 位图之后（避免与位图重叠）
    uint32_t heap_start = pmm_get_bitmap_end();
    
    // 确保堆不会覆盖 multiboot 模块
    if (mbi->flags & MULTIBOOT_INFO_MODS && mbi->mods_count > 0) {
        multiboot_module_t *modules = (multiboot_module_t *)PHYS_TO_VIRT(mbi->mods_addr);
        
        // 检查模块列表结束位置
        uint32_t mods_list_end = PHYS_TO_VIRT(mbi->mods_addr + sizeof(multiboot_module_t) * mbi->mods_count);
        if (mods_list_end > heap_start) {
            heap_start = mods_list_end;
        }
        
        // 检查每个模块的结束位置
        for (uint32_t i = 0; i < mbi->mods_count; i++) {
            uint32_t mod_end_virt = PHYS_TO_VIRT(modules[i].mod_end);
            if (mod_end_virt > heap_start) {
                heap_start = mod_end_virt;
            }
        }
        
        heap_start = PAGE_ALIGN_UP(heap_start);
        LOG_DEBUG_MSG("  Heap start adjusted for multiboot modules: %x\n", heap_start);
    }
    
    heap_init(heap_start, 32 * 1024 * 1024);  // 32MB 堆
    heap_print_info();
    LOG_DEBUG_MSG("  [3.3] Heap initialized\n");

    // ========================================================================
    // 阶段 4: 设备驱动（Device Drivers）
    // ========================================================================
    
    LOG_INFO_MSG("[Stage 4] Initializing device drivers...\n");
    
    // 4.1 初始化 PIT（Programmable Interval Timer - 可编程定时器）
    timer_init(100);  // 100 Hz
    LOG_DEBUG_MSG("  [4.1] PIT initialized (100 Hz)\n");
    
    // 4.2 初始化键盘驱动
    keyboard_init();
    LOG_DEBUG_MSG("  [4.2] Keyboard initialized\n");

    // ========================================================================
    // 阶段 5: 高级子系统（Advanced Subsystems）
    // ========================================================================
    LOG_INFO_MSG("[Stage 5] Initializing advanced subsystems...\n");

    // 5.1 初始化进程管理
    task_init();
    LOG_DEBUG_MSG("  [5.1] Task management initialized\n");

    // ========================================================================
    // 单元测试
    // ========================================================================
    LOG_INFO_MSG("Running test suite...\n");
    run_all_tests();
    kprintf("\n");

    /* 创建测试任务 */
    LOG_INFO_MSG("Creating test tasks...\n");
    task_create_kernel_thread(test_task1, "test1");
    task_create_kernel_thread(test_task2, "test2");
    
    /* 进入空闲循环（idle task 会自动运行） */
    LOG_INFO_MSG("Kernel initialization complete\n");
    LOG_INFO_MSG("Entering scheduler...\n\n");
    
    // 主动调度（切换到第一个就绪任务）
    task_schedule();
    
    // 进入空闲循环
    while (1) {
        __asm__ volatile ("hlt");
    }
}

/* 测试任务 1 */
void test_task1(void) {
    LOG_INFO_MSG("Test task 1 started (PID %u)\n", task_get_current()->pid);
    
    for (int i = 0; i < 10; i++) {
        kprintf("[Task 1] Hello %d\n", i);
        task_sleep(500);  // 睡眠 500ms
    }
    
    LOG_INFO_MSG("Test task 1 exiting\n");
    task_exit(0);
}

/* 测试任务 2 */
void test_task2(void) {
    LOG_INFO_MSG("Test task 2 started (PID %u)\n", task_get_current()->pid);
    
    for (int i = 0; i < 10; i++) {
        kprintf("[Task 2] World %d\n", i);
        task_sleep(700);  // 睡眠 700ms
    }
    
    LOG_INFO_MSG("Test task 2 exiting\n");
    task_exit(0);
}

```
