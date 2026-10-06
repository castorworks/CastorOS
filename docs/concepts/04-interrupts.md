# 中断与异常处理

## 概述

x86 处理器通过中断和异常机制处理硬件事件和错误情况。CastorOS 使用 IDT（中断描述符表）来管理这些事件。

## 中断类型

| 类型 | 向量范围 | 描述 |
|------|----------|------|
| 异常 (Exception) | 0-31 | CPU 产生的错误或事件 |
| IRQ (硬件中断) | 32-47 | 硬件设备触发 |
| 系统调用 | 128 (0x80) | 用户程序请求内核服务 |

## 异常列表 (0-31)

| 向量 | 名称 | 类型 | 错误码 | 描述 |
|------|------|------|--------|------|
| 0 | #DE | Fault | 无 | 除零错误 |
| 1 | #DB | Trap/Fault | 无 | 调试异常 |
| 2 | NMI | Interrupt | 无 | 不可屏蔽中断 |
| 3 | #BP | Trap | 无 | 断点 (INT3) |
| 4 | #OF | Trap | 无 | 溢出 (INTO) |
| 5 | #BR | Fault | 无 | 边界范围越界 |
| 6 | #UD | Fault | 无 | 无效操作码 |
| 7 | #NM | Fault | 无 | 设备不可用 (FPU) |
| 8 | #DF | Abort | 有(0) | 双重故障 |
| 10 | #TS | Fault | 有 | 无效 TSS |
| 11 | #NP | Fault | 有 | 段不存在 |
| 12 | #SS | Fault | 有 | 栈段故障 |
| 13 | #GP | Fault | 有 | 通用保护故障 |
| 14 | #PF | Fault | 有 | 页故障 |
| 16 | #MF | Fault | 无 | x87 FPU 错误 |
| 17 | #AC | Fault | 有(0) | 对齐检查 |
| 18 | #MC | Abort | 无 | 机器检查 |
| 19 | #XM | Fault | 无 | SIMD 浮点异常 |

## IDT 结构

### IDT 条目 (Gate Descriptor)

```c
typedef struct {
    uint16_t offset_low;   // 处理程序地址低 16 位
    uint16_t selector;     // 代码段选择子
    uint8_t  zero;         // 保留
    uint8_t  type_attr;    // 类型和属性
    uint16_t offset_high;  // 处理程序地址高 16 位
} __attribute__((packed)) idt_entry_t;

// type_attr 格式:
// +---+---+---+---+---+---+---+---+
// | P |  DPL  | S |    Type       |
// +---+---+---+---+---+---+---+---+
//   7   6   5   4   3   2   1   0
//
// P   = Present (1)
// DPL = 描述符特权级 (0=内核, 3=用户)
// S   = 0 (系统段)
// Type: 0xE = 32位中断门, 0xF = 32位陷阱门
```

### IDT 寄存器

```c
typedef struct {
    uint16_t limit;    // IDT 大小 - 1
    uint32_t base;     // IDT 基地址
} __attribute__((packed)) idt_ptr_t;

// 告诉 CPU 这张表在哪里：lidt 指令（idt_init() 的最后一步）
__asm__ volatile ("lidt %0" : : "m"(idt_ptr));
```

## 中断处理流程

### 1. 硬件保存上下文

当中断发生时，CPU 自动：
1. 保存 EFLAGS、CS、EIP 到栈
2. 如果特权级变化，还保存 SS、ESP
3. 如果有错误码，压入错误码
4. 加载新的 CS:EIP（从 IDT 获取）

### 2. 中断处理程序入口 (汇编)

```asm
; 无错误码的中断
isr_stub_0:
    push 0          ; 压入伪错误码（保持栈一致）
    push 0          ; 中断号
    jmp isr_common

; 有错误码的中断
isr_stub_14:
    ; 错误码已由 CPU 压入
    push 14         ; 中断号
    jmp isr_common

isr_common:
    ; 保存所有寄存器
    pusha           ; EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
    push ds
    push es
    push fs
    push gs
    
    ; 切换到内核数据段
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; 调用 C 处理程序
    push esp        ; 传递 registers_t 指针
    call isr_handler
    add esp, 4
    
    ; 恢复寄存器
    pop gs
    pop fs
    pop es
    pop ds
    popa
    
    add esp, 8      ; 跳过错误码和中断号
    iret            ; 中断返回
```

### 3. 寄存器结构

```c
typedef struct {
    // 段寄存器（手动保存）
    uint32_t gs, fs, es, ds;
    
    // 通用寄存器（pusha 保存）
    uint32_t edi, esi, ebp, esp;
    uint32_t ebx, edx, ecx, eax;
    
    // 中断信息
    uint32_t int_no, err_code;
    
    // CPU 自动保存
    uint32_t eip, cs, eflags;
    uint32_t user_esp, user_ss;  // 仅特权级变化时
} registers_t;
```

### 4. C 处理程序

每个异常号可以登记一个处理函数（`isr_register_handler`）。汇编入口最后都调到同一个
`isr_handler`，由它按异常号分发：

```cpp
isr_register_handler(13, general_protection_fault_handler);
isr_register_handler(14, page_fault_handler);

void isr_handler(registers_t *regs) {
    if (interrupt_handlers[regs->int_no]) {
        interrupt_handlers[regs->int_no](regs);     // 有登记的处理函数
        return;
    }
    // 没有：用户程序引起的就杀掉它；内核自己引起的是内核的 bug，打印现场后停机
}
```

## 页故障处理

页故障 (#PF) 是最重要的异常之一，用于实现按需分页、COW 等功能。

### 错误码格式

```
+---+---+---+---+---+
| I | R | U | W | P |
+---+---+---+---+---+
  4   3   2   1   0

P = Present (0=页不存在, 1=保护违规)
W = Write (0=读访问, 1=写访问)
U = User (0=特权级访问, 1=用户级访问)
R = Reserved (1=保留位被设置)
I = Instruction (1=指令获取时发生)
```

### CR2 寄存器

发生页故障时，CR2 寄存器包含导致故障的线性地址：

```cpp
static void page_fault_handler(registers_t *regs) {
    uint32_t faulting_address = get_cr2();

    // 1. 内核态访问、页不存在：可能只是这个地址空间的内核页目录项还没同步
    if ((regs->err_code & 0x5) == 0 &&
        mm::Vmm::handle_kernel_page_fault(faulting_address)) {
        return;
    }

    // 2. 写一个只读的页：可能是写时复制
    if (mm::Vmm::handle_cow_page_fault(faulting_address, regs->err_code)) {
        return;
    }

    // 3. 真正的非法访问。用户程序干的就杀掉它；内核自己干的是内核的 bug，停机
    if ((regs->cs & 0x3) == 3) {
        kill_faulting_user_task(regs, "Page fault");
    }
    // ... 打印现场，panic ...
}
```

前两种情况处理完直接返回，CPU 重新执行出错的那条指令，这次能成功。

## 硬件中断 (IRQ)

### 8259 PIC

传统 PC 使用两个级联的 8259 PIC 管理 16 个 IRQ：

```
Master PIC (0x20-0x21)    Slave PIC (0xA0-0xA1)
  IRQ 0 - Timer             IRQ 8  - RTC
  IRQ 1 - Keyboard          IRQ 9  - Free
  IRQ 2 - Cascade           IRQ 10 - Free
  IRQ 3 - COM2              IRQ 11 - Free
  IRQ 4 - COM1              IRQ 12 - PS/2 Mouse
  IRQ 5 - LPT2              IRQ 13 - FPU
  IRQ 6 - Floppy            IRQ 14 - Primary ATA
  IRQ 7 - LPT1              IRQ 15 - Secondary ATA
```

### PIC 初始化（重映射）

```c
static void pic_remap(void) {
    // ICW1: 开始初始化
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    
    // ICW2: 设置中断向量偏移
    outb(0x21, 0x20);  // Master: IRQ 0-7 -> INT 32-39
    outb(0xA1, 0x28);  // Slave:  IRQ 8-15 -> INT 40-47
    
    // ICW3: 主从级联
    outb(0x21, 0x04);  // Master: IR2 连接从 PIC
    outb(0xA1, 0x02);  // Slave: 连接到主 PIC 的 IR2
    
    // ICW4: 8086 模式
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    
    // 屏蔽所有中断
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}
```

### IRQ 处理

```c
// IRQ 处理程序表
static irq_handler_t irq_handlers[16];

void irq_register_handler(int irq, irq_handler_t handler) {
    irq_handlers[irq] = handler;
}

void irq_handler(registers_t *regs) {
    int irq = regs->int_no - 32;
    
    // 调用注册的处理程序
    if (irq_handlers[irq]) {
        irq_handlers[irq](regs);
    }
    
    // 发送 EOI (End of Interrupt)
    if (irq >= 8) {
        outb(0xA0, 0x20);  // Slave PIC
    }
    outb(0x20, 0x20);      // Master PIC
}
```

### 启用/禁用 IRQ

```c
void irq_enable_line(int irq) {
    uint16_t port = (irq < 8) ? 0x21 : 0xA1;
    uint8_t irq_bit = (irq < 8) ? irq : (irq - 8);
    uint8_t mask = inb(port) & ~(1 << irq_bit);
    outb(port, mask);
}

void irq_disable_line(int irq) {
    uint16_t port = (irq < 8) ? 0x21 : 0xA1;
    uint8_t irq_bit = (irq < 8) ? irq : (irq - 8);
    uint8_t mask = inb(port) | (1 << irq_bit);
    outb(port, mask);
}
```

## 中断状态管理

内核里有些代码不能被打断（见 [同步](06-synchronization.md)）。x86 上关、开中断是 `cli` 和
`sti` 两条指令，但直接用它们有个问题：一段关了中断的代码调用另一段也要关中断的代码，
内层结束时如果无条件 `sti`，外层就在不知情的情况下被打开了中断。

所以内核里总是"保存原来的状态并关中断"，结束时"恢复原来的状态"：

```cpp
{
    kernel::InterruptGuard guard;       // 记下中断原来开没开，然后关掉
    // ...
}                                       // 恢复成原来的样子
```

## 定时器中断

定时器每 10ms 产生一次中断（x86 上是 PIT，把 1193182Hz 的基础频率分频到 100Hz；arm64 上是
Generic Timer）。它是内核里仅有的两个驱动之一（另一个是只做输出的串口），做三件事：

1. 累加开机以来的时间（`uptime_ms`、`nanosleep`、`timer_set` 都靠它）。
2. 唤醒睡眠时间到了的任务，给定时器到期的进程发 `IPC_LABEL_TIMER` 消息。
3. 当前任务的时间片用完了就调度（`Scheduler::timer_tick()`）——这就是抢占。

## 设备中断交给用户态驱动

除了定时器，内核不处理任何设备的中断：驱动是用户态进程。内核做的只是把"中断发生了"
这件事转成一条消息（`src/kernel/user_irq.cpp`）：

1. 驱动用 `irq_claim(irq)` 认领一条中断线（这条线得是 init 许可给它的）。同一条线可以被几个驱动认领
   （x86 上磁盘和网卡共用 11 号线）。
2. 中断发生时，内核先**屏蔽这条线**，再给每个认领者记一条待收的中断消息。
3. 驱动在 `ipc_recv` 上收到这条消息（发送者是内核，标签是 `IPC_LABEL_IRQ`），去读设备、
   让设备撤销中断请求，然后调用 `irq_ack(irq)`。
4. 所有认领者都 `irq_ack` 之后，内核重新打开这条线。

第 2 步的屏蔽是必须的。中断处理程序返回后中断就重新打开了，而设备的中断请求要等驱动
处理过才会撤销——驱动是个普通进程，这时还没轮到它运行。不屏蔽的话，同一个中断会立刻
再次触发，内核永远在处理中断，驱动永远得不到 CPU。

## 最佳实践

1. **保持中断处理程序简短**：只做必要的工作，复杂处理推迟到后台
2. **正确管理中断状态**：保存并恢复原来的状态，而不是无条件地 cli/sti
3. **避免在中断中睡眠**：中断上下文不能调用可能阻塞的函数
4. **EOI 时机**：在处理完成后再发送 EOI，避免中断嵌套问题
5. **栈溢出防护**：确保中断栈足够大

