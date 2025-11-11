# 阶段 2: 基础设施

## 概述

本阶段将建立操作系统的核心基础设施，包括段描述符表、中断处理框架和基本的库函数。这些是实现更高级功能（如内存管理、进程管理）的必要前提。

---

## 目标

- ✅ 实现全局描述符表（GDT）
- ✅ 实现中断描述符表（IDT）
- ✅ 建立中断服务例程（ISR）框架
- ✅ 实现硬件中断请求（IRQ）处理
- ✅ 初始化可编程中断控制器（PIC）
- ✅ 扩展标准库函数

---

## 技术背景

### 什么是 GDT（全局描述符表）？

**GDT（Global Descriptor Table）** 是 x86 保护模式下的核心数据结构，定义了内存段的属性和访问权限。

**为什么需要 GDT？**
- x86 架构从设计上就采用**分段内存管理**
- 即使使用分页（Paging），分段机制仍然存在
- CPU 必须在分段和分页模式下运行，不能单独使用分页
- GRUB 在启动时设置了一个临时 GDT，但不适合操作系统长期使用

**GDT 的作用**：
- 定义代码段和数据段的基址、界限和访问权限
- 实现特权级保护（Ring 0-3）
- 为任务切换提供支持（TSS）

**段描述符结构**（64位，8字节）：
```
63                                                              0
+-------+-------+-------+-------+-------+-------+-------+-------+
| Base  | Flags | Limit | Access| Base  | Base  | Base  | Limit |
| 31:24 | 3:0   | 19:16 | Byte  | 23:16 | 15:8  | 7:0   | 15:0  |
+-------+-------+-------+-------+-------+-------+-------+-------+

Access Byte (8位):
  P:  Present (1 bit)           - 段是否存在
  DPL: Descriptor Privilege Level (2 bits) - 特权级 (0-3)
  S:  Descriptor Type (1 bit)   - 0=系统段, 1=代码/数据段
  E:  Executable (1 bit)         - 1=代码段, 0=数据段
  DC: Direction/Conforming (1 bit)
  RW: Readable/Writable (1 bit)
  A:  Accessed (1 bit)           - CPU 自动设置

Flags (4位):
  G:  Granularity (1 bit)        - 0=字节, 1=4KB 页
  D/B: Size (1 bit)              - 0=16位, 1=32位
  L:  Long mode (1 bit)          - 64位代码段标志
  Reserved (1 bit)
```

**扁平内存模型**：
- 现代操作系统通常使用"扁平模型"（Flat Model）
- 所有段的基址都设为 0，界限设为 0xFFFFFFFF（4GB）
- 实际的内存保护由分页机制完成
- 分段变成一个"透明"的层次，不影响地址计算

---

### 什么是 IDT（中断描述符表）？

**IDT（Interrupt Descriptor Table）** 定义了 CPU 如何响应中断和异常。

**中断的分类**：

1. **异常（Exceptions）** - CPU 内部产生（0-31号中断）
   - 故障（Faults）：可恢复，如页错误
   - 陷阱（Traps）：用于调试，如断点
   - 终止（Aborts）：严重错误，如双重故障

2. **硬件中断（Hardware Interrupts）** - 外部设备产生（32-255号）
   - IRQ 0-15：传统硬件中断（定时器、键盘等）
   - 通过 PIC 或 APIC 管理

3. **软件中断（Software Interrupts）** - 程序主动触发
   - INT 指令：用于系统调用（如 INT 0x80）

**IDT 表项结构**（64位，8字节）：
```
63                                                              0
+-------+-------+-------+-------+-------+-------+-------+-------+
| Offset| Flags | Zero  | Sel   | Offset                        |
| 31:16 |       | (8)   | ector | 15:0                          |
+-------+-------+-------+-------+-------+-------+-------+-------+

Flags (8位):
  P:    Present (1 bit)          - 描述符是否有效
  DPL:  Privilege Level (2 bits) - 最低调用特权级
  Zero: (1 bit)                  - 必须为0
  Type: Gate Type (4 bits)       - 门类型
    - 0x5: 32位任务门
    - 0xE: 32位中断门（自动禁用中断）
    - 0xF: 32位陷阱门（不禁用中断）
```

**中断处理流程**：
1. 中断发生（硬件或软件）
2. CPU 查找 IDT 找到对应的中断门
3. 保存当前状态（CS、EIP、EFLAGS等）到栈
4. 跳转到中断处理程序
5. 中断处理程序执行
6. IRET 指令恢复状态并返回

---

### 什么是 PIC（可编程中断控制器）？

**PIC（Programmable Interrupt Controller）** 管理硬件中断请求。

**8259 PIC 芯片**：
- 主 PIC（Master）：管理 IRQ 0-7
- 从 PIC（Slave）：管理 IRQ 8-15，级联到主 PIC 的 IRQ 2
- 总共可以处理 15 个中断（IRQ 2 被级联占用）

**IRQ 默认映射**：
```
IRQ 0:  定时器（PIT）
IRQ 1:  键盘
IRQ 2:  级联（从 PIC）
IRQ 3:  COM2/COM4
IRQ 4:  COM1/COM3
IRQ 5:  LPT2
IRQ 6:  软盘
IRQ 7:  LPT1
IRQ 8:  实时时钟（RTC）
IRQ 9:  自由
IRQ 10: 自由
IRQ 11: 自由
IRQ 12: PS/2 鼠标
IRQ 13: 数学协处理器
IRQ 14: 主 IDE
IRQ 15: 副 IDE
```

**为什么需要重映射 PIC？**
- BIOS 将 IRQ 0-7 映射到中断 8-15
- 与 CPU 异常（0-31）冲突！
- 必须重映射到 32-47（或其他范围）

**PIC 端口**：
- 主 PIC：命令端口 0x20，数据端口 0x21
- 从 PIC：命令端口 0xA0，数据端口 0xA1

**PIC 控制命令**：
- ICW (Initialization Command Words)：初始化
- OCW (Operation Command Words)：操作控制
- EOI (End Of Interrupt)：通知中断处理完成

---

## 实现步骤

注意（高半核）：GDT/IDT 指针中的 base 字段在分页开启后使用的是内核虚拟地址，阶段 1 的早期页表已将相关区域映射到物理地址，可直接使用虚拟地址进行 LGDT/LIDT。

### 步骤 1: 实现 GDT

GDT 的实现分为三个部分：数据结构定义（C）、GDT 加载（汇编）、初始化函数（C）。

**文件**: `src/include/kernel/gdt.h`
**文件**: `src/kernel/gdt.c`

**代码说明**：

1. **扁平内存模型**：
   - 所有段的基址设为 0
   - 界限设为 0xFFFFFFFF（4GB）
   - 实际的内存保护由分页完成

2. **5 个 GDT 表项**：
   - 表项 0：空描述符（CPU 要求）
   - 表项 1：内核代码段（Ring 0）
   - 表项 2：内核数据段（Ring 0）
   - 表项 3：用户代码段（Ring 3）
   - 表项 4：用户数据段（Ring 3）

3. **段选择子计算**：
   - 选择子 = 索引 × 8 + RPL + TI
   - 例如：内核代码段 = 1 × 8 + 0 + 0 = 0x08

**文件**: `src/kernel/gdt_asm.asm`

**代码说明**：

1. **LGDT 指令**：
   - 加载 GDT 表的基址和界限
   - 参数是 GDT 指针结构的地址

2. **刷新数据段寄存器**：
   - 将所有数据段寄存器设为内核数据段（0x10）
   - 包括 DS、ES、FS、GS、SS

3. **远跳转刷新代码段**：
   - `jmp 0x08:.flush` 执行段间跳转
   - 刷新 CS 寄存器为内核代码段（0x08）
   - 这是刷新 CS 的唯一方法

---

### 步骤 2: 实现 IDT

IDT 的实现同样分为 C 代码和汇编代码两部分。

**文件**: `src/include/kernel/idt.h`
**文件**: `src/kernel/idt.c`
**文件**: `src/kernel/idt_asm.asm`

### 步骤 3: 实现 ISR（中断服务例程）

ISR 处理 CPU 异常（0-31号中断）。

**文件**: `src/include/kernel/isr.h`
**文件**: `src/kernel/isr.c`
**文件**: `src/kernel/isr_asm.asm`

### 步骤 4: 实现 IRQ（硬件中断）处理

IRQ 处理硬件中断（32-47号中断）。

**文件**: `src/include/kernel/irq.h`
**文件**: `src/kernel/irq.c`
**文件**: `src/kernel/irq_asm.asm`

### 步骤 5: 更新内核主函数

更新 `kernel_main` 以初始化新的基础设施。

**更新文件**: `src/kernel/kernel.c`


```c
// ============================================================================
// kernel.c - 内核主函数
// ============================================================================

#include <drivers/vga.h>
#include <drivers/serial.h>
#include <kernel/multiboot.h>
#include <kernel/version.h>

#include <lib/klog.h>

#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/irq.h>
#include <kernel/isr.h>

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
    kprintf("Welcome to CastorOS!\n");
    kprintf("Version v%s\n", KERNEL_VERSION);
    kprintf("Compiled on: %s %s\n", __DATE__, __TIME__);

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
