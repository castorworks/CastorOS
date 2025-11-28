# 阶段 4: 基本驱动程序

## 概述

本阶段将实现 CastorOS 的基本硬件驱动程序，使内核能够与外部设备交互。我们将实现定时器驱动、键盘驱动，并改进现有的 VGA 驱动。这些驱动是实现更高级功能（如进程调度、用户交互）的基础。

---

## 目标

- ✅ VGA 文本模式驱动
- ✅ 串口驱动
- ✅ 实现可编程间隔定时器（PIT）驱动
- ✅ 实现键盘驱动（PS/2）

---

## 技术背景

### 什么是设备驱动？

**设备驱动（Device Driver）** 是操作系统与硬件设备之间的桥梁，负责：
- 初始化硬件设备
- 处理设备中断
- 提供统一的操作接口
- 管理设备状态和缓冲区

**驱动分类**：
1. **字符设备**：按字符流传输数据（如键盘、串口）
2. **块设备**：按数据块传输数据（如硬盘、软盘）
3. **网络设备**：传输网络数据包（如网卡）

---

### 可编程间隔定时器（PIT）

**PIT（Programmable Interval Timer）** 是 x86 架构的标准定时器芯片（Intel 8253/8254）。

**功能**：
- 产生周期性的硬件中断（IRQ 0）
- 为操作系统提供时钟基准
- 支持进程调度和超时管理

**工作原理**：
1. PIT 接收输入时钟频率（1.193182 MHz）
2. 通过除数器产生目标频率
3. 每次计数器归零时触发 IRQ 0

**寄存器和端口**：
```
端口 0x40: 通道 0 数据端口（系统定时器）
端口 0x41: 通道 1 数据端口（DRAM 刷新，已废弃）
端口 0x42: 通道 2 数据端口（PC 扬声器）
端口 0x43: 命令/模式寄存器
```

**频率计算**：
```
输入频率 = 1193182 Hz (约 1.19 MHz)
除数 = 输入频率 / 目标频率
例如：100 Hz = 1193182 / 100 ≈ 11932
```

**命令字节格式**：
```
Bit 7-6: 通道选择
  00 = 通道 0
  01 = 通道 1
  10 = 通道 2
  11 = Read-back 命令

Bit 5-4: 访问模式
  00 = 锁存计数值
  01 = 只访问低字节
  10 = 只访问高字节
  11 = 先低后高

Bit 3-1: 操作模式
  000 = 模式 0（中断结束时）
  001 = 模式 1（硬件可重触发单稳）
  010 = 模式 2（频率发生器）
  011 = 模式 3（方波发生器）
  100 = 模式 4（软件触发选通）
  101 = 模式 5（硬件触发选通）

Bit 0: BCD/二进制模式
  0 = 16位二进制
  1 = 4位 BCD
```

---

### PS/2 键盘控制器

**PS/2 键盘** 是传统的 PC 键盘接口标准。

**工作原理**：
1. 键盘按下/释放时发送扫描码（Scan Code）
2. 键盘控制器将扫描码放入输出缓冲区
3. 触发 IRQ 1 中断
4. CPU 读取扫描码并转换为 ASCII

**端口**：
```
端口 0x60: 数据端口（读取扫描码）
端口 0x64: 命令/状态寄存器
```

**状态寄存器（0x64 读取）**：
```
Bit 0: 输出缓冲区状态（0=空，1=满，可读取）
Bit 1: 输入缓冲区状态（0=空，1=满，不可写入）
Bit 2: 系统标志（POST 通过后置 1）
Bit 3: 命令/数据标志（0=数据，1=命令）
Bit 4: 键盘锁定
Bit 5: 辅助输出缓冲区满（鼠标数据）
Bit 6: 超时错误
Bit 7: 奇偶校验错误
```

**扫描码**：
- **Set 1**（默认）：按下产生一个码，释放产生 0xF0 + 按下码
- **Set 2**：IBM 兼容模式
- **Set 3**：现代键盘模式

**特殊扫描码**：
- `0xE0`：扩展键前缀（如方向键、Home、End）
- `0xF0`：释放键前缀（Set 2）
- `0xE1`：Pause 键特殊前缀

---

## 实现步骤

### 步骤 1: 实现定时器（PIT）驱动

定时器是操作系统最重要的驱动之一，为进程调度、超时管理提供时间基准。

**文件**: `src/include/drivers/timer.h`
**文件**: `src/drivers/timer.c`

**核心功能**：
1. **PIT 初始化**：配置 Intel 8253/8254 定时器芯片，设置目标中断频率
2. **时间追踪**：维护系统运行时间（滴答数、毫秒、秒）
3. **延迟函数**：提供忙等待延迟和高精度微秒延迟
4. **定时器回调**：支持注册周期性或一次性定时器回调函数

### 步骤 2: 实现键盘驱动

键盘是用户与操作系统交互的主要输入设备。

**文件**: `src/include/drivers/keyboard.h`
**文件**: `src/drivers/keyboard.c`

**核心功能**：
1. **扫描码处理**：读取键盘扫描码并转换为 ASCII 字符
2. **修饰键管理**：跟踪 Shift、Ctrl、Alt、Caps Lock 等状态
3. **输入缓冲**：使用环形缓冲区存储键盘输入
4. **事件系统**：支持按键按下/释放事件通知
5. **LED 控制**：更新键盘 LED 指示灯状态

### 步骤 3: 集成到内核

> 到这里，暂时还无法测试，因为没有支持 keyboard 驱动键盘按键的 vga 回显功能。

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

#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/heap.h>

#include <tests/test_runner.h>

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
        LOG_DEBUG_MSG("  Heap start adjusted for multiboot modules: 0x%x\n", heap_start);
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
    // 单元测试
    // ========================================================================
    LOG_INFO_MSG("Running test suite...\n");
    run_all_tests();
    kprintf("\n");
    
    // 进入空闲循环
    while (1) {
        __asm__ volatile ("hlt");
    }
}
```
