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

// 加载 IDT
void idt_load(idt_ptr_t *ptr) {
    __asm__ volatile ("lidt %0" : : "m"(*ptr));
}
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

```c
void isr_handler(registers_t *regs) {
    switch (regs->int_no) {
        case 0:   // 除零
            panic("Division by zero");
            break;
            
        case 13:  // GPF
            handle_gpf(regs);
            break;
            
        case 14:  // 页故障
            handle_page_fault(regs);
            break;
            
        default:
            kprintf("Unhandled exception: %d\n", regs->int_no);
            panic("Unhandled exception");
    }
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

```c
void handle_page_fault(registers_t *regs) {
    uint32_t fault_addr;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_addr));
    
    bool present = regs->err_code & 0x1;
    bool write = regs->err_code & 0x2;
    bool user = regs->err_code & 0x4;
    
    // 尝试处理 COW
    if (write && present && vmm_handle_cow_fault(fault_addr)) {
        return;  // COW 处理成功
    }
    
    // 无法处理的页故障
    kprintf("Page fault at 0x%x (err=%x)\n", fault_addr, regs->err_code);
    panic("Unhandled page fault");
}
```

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

### PIC 初始化

```c
void pic_init(void) {
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

### CLI/STI

```c
// 禁用中断
static inline void cli(void) {
    __asm__ volatile ("cli");
}

// 启用中断
static inline void sti(void) {
    __asm__ volatile ("sti");
}

// 保存并禁用中断
static inline bool irq_save(void) {
    uint32_t flags;
    __asm__ volatile (
        "pushf\n"
        "pop %0\n"
        "cli"
        : "=r"(flags)
    );
    return flags & 0x200;  // IF 标志
}

// 恢复中断状态
static inline void irq_restore(bool state) {
    if (state) sti();
}
```

### 临界区保护

```c
void spinlock_lock_irqsave(spinlock_t *lock, bool *irq_state) {
    *irq_state = irq_save();
    spinlock_lock(lock);
}

void spinlock_unlock_irqrestore(spinlock_t *lock, bool irq_state) {
    spinlock_unlock(lock);
    irq_restore(irq_state);
}
```

## 定时器中断

PIT (Programmable Interval Timer) 用于产生周期性中断：

```c
#define PIT_FREQ 1193182  // 基础频率
#define TARGET_HZ 100     // 目标频率

void pit_init(void) {
    uint16_t divisor = PIT_FREQ / TARGET_HZ;
    
    // 命令字: 通道0, 方式3, 16位计数
    outb(0x43, 0x36);
    
    // 设置分频值
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
    
    // 注册中断处理程序
    irq_register_handler(0, timer_handler);
    irq_enable_line(0);
}

static void timer_handler(registers_t *regs) {
    (void)regs;
    ticks++;
    
    // 触发调度
    if (should_schedule()) {
        schedule();
    }
}
```

## 最佳实践

1. **保持中断处理程序简短**：只做必要的工作，复杂处理推迟到后台
2. **正确管理中断状态**：使用 irqsave/irqrestore 而不是 cli/sti
3. **避免在中断中睡眠**：中断上下文不能调用可能阻塞的函数
4. **EOI 时机**：在处理完成后再发送 EOI，避免中断嵌套问题
5. **栈溢出防护**：确保中断栈足够大

