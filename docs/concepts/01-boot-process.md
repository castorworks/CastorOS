# 引导过程

## 概述

x86 上的 CastorOS 遵循 Multiboot 规范：任何支持 Multiboot 的引导加载器都可以加载它。开发时用的是 QEMU 自带的加载器（`qemu -kernel`），真机上可以用 GRUB。引导过程从固件开始，经过引导加载器，最终将控制权交给内核。

## 引导流程

```
固件 (BIOS/UEFI)
    ↓
引导加载器 (QEMU -kernel / GRUB，Multiboot)
    ↓
boot.asm (_start)
    ↓
  设置页表
    ↓
  启用分页
    ↓
  跳转到高半核
    ↓
kernel_main()
```

## Multiboot 规范

### Multiboot Header

内核必须在前 8KB 包含 Multiboot 头，告诉引导加载器如何加载内核：

```c
// multiboot.asm
MULTIBOOT_MAGIC     equ 0x1BADB002
MULTIBOOT_FLAGS     equ 0x00000003  // 页对齐 | 提供内存信息
MULTIBOOT_CHECKSUM  equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

section .multiboot
    dd MULTIBOOT_MAGIC
    dd MULTIBOOT_FLAGS
    dd MULTIBOOT_CHECKSUM
```

### 引导加载器提供的信息

- **魔数**: 0x2BADB002（放在 EAX）
- **Multiboot 信息结构指针**（放在 EBX）
- **保护模式**: 32位保护模式，A20 已启用
- **分页**: 禁用
- **中断**: 禁用

## boot.asm 详解

### 1. 入口点 (_start)

```asm
_start:
    ; 此时状态：
    ; - EAX = 0x2BADB002 (魔数)
    ; - EBX = Multiboot 信息结构物理地址
    ; - 保护模式，禁用分页
    cli  ; 禁用中断
```

### 2. 设置页表

引导时需要两种映射：
1. **恒等映射**: 物理地址 = 虚拟地址（用于分页启用后的过渡）
2. **高半核映射**: 物理地址 + 0x80000000 = 虚拟地址

```asm
boot_page_directory:
    ; PDE 0: 恒等映射前 4MB
    dd (boot_page_table1 - KERNEL_VIRTUAL_BASE) + 0x003
    
    times (KERNEL_PAGE_NUMBER - 1) dd 0  ; PDE 1-511: 未映射
    
    ; PDE 512-515: 高半核映射（前 16MB）
    dd (boot_page_table1 - KERNEL_VIRTUAL_BASE) + 0x003
    dd (boot_page_table2 - KERNEL_VIRTUAL_BASE) + 0x003
    dd (boot_page_table3 - KERNEL_VIRTUAL_BASE) + 0x003
    dd (boot_page_table4 - KERNEL_VIRTUAL_BASE) + 0x003
```

### 3. 启用分页

```asm
    ; 加载页目录到 CR3
    mov ecx, (boot_page_directory - KERNEL_VIRTUAL_BASE)
    mov cr3, ecx
    
    ; 启用分页（CR0.PG）和写保护（CR0.WP）
    mov ecx, cr0
    or ecx, 0x80010000  ; PG | WP
    mov cr0, ecx
```

### 4. 跳转到高半核

```asm
    ; 跳转到高半核地址，刷新指令流水线
    lea ecx, [higher_half]
    jmp ecx

higher_half:
    ; 现在运行在高半核地址空间
    ; 可以移除恒等映射了
    mov dword [boot_page_directory], 0
    
    ; 设置栈指针
    mov esp, stack_top
    
    ; 调用 C 内核入口
    push ebx  ; Multiboot 信息结构
    push eax  ; 魔数
    call kernel_main
```

## kernel_main 初始化顺序

```cpp
void kernel_main(multiboot_info_t *mbi) {
    cxx_global_ctors_init();      // C++ 全局构造函数

    drivers::Serial::init();      // 串口：内核唯一的输出设备
    print_banner();

    hal::Cpu::init();             // GDT + TSS
    hal::Interrupt::init();       // IDT、异常、PIC
    syscall_init();               // 系统调用表和入口

    mm::Pmm::init(regions, n);    // 物理内存：regions 是从 Multiboot 内存映射里取出的可用区域
    mm::Vmm::init();              // 虚拟内存
    mm::Heap::init(...);          // 内核堆

    drivers::Timer::init(100);    // 时钟，100 Hz

    kernel_start();
}

static void kernel_start(void) {
    kernel::Scheduler::init();    // 任务管理
    hal::Interrupt::enable();
    // KTEST=1 时在这里运行内核测试
    load_init();                  // 加载内嵌的 init，成为 PID 1
    kernel::Scheduler::schedule();
}
```

内核初始化到此为止。驱动、文件系统、命令行都由 init 在用户态启动
（见 [../microkernel.md](../microkernel.md)）。

arm64 的流程相同，只是硬件的描述来自设备树而不是 Multiboot。

## 设备树（arm64）

x86 上内核从 Multiboot 信息里得知内存有多少；arm64 上固件（这里是 QEMU）交给内核的是一份
**设备树**：一棵描述硬件的树，每个节点是一个设备或一条总线，带着若干属性。

```
/ {
    #address-cells = <2>;           子节点的 reg 里，地址占 2 个 32 位单元
    #size-cells = <2>;              长度占 2 个
    memory@40000000 {
        device_type = "memory";
        reg = <0x0 0x40000000 0x0 0x08000000>;      从 1GB 开始，128MB
    };
    pl011@9000000 {
        compatible = "arm,pl011", "arm,primecell";   从最具体到最一般
        reg = <0x0 0x09000000 0x0 0x1000>;
        interrupts = <0 1 4>;                        SPI 1 → GIC 中断号 33
    };
    ...
};
```

`dtb_parse()`（`src/arch/arm64/dtb/dtb.cpp`）走一遍这棵树，整理出物理内存的范围、
中断控制器、定时器、串口，以及其余设备的列表。几条容易弄错的规则：

- 一个节点是什么设备由 `compatible` 决定，而它的 `reg`、`interrupts` 可能排在 `compatible`
  前面。所以要等节点的属性都读完（节点结束时）再归类，不能读到一个属性就下结论。
- `reg` 里地址和长度各占几个单元，由**父节点**的 `#address-cells` / `#size-cells` 决定，
  不是节点自己的，也不从祖先继承。
- `compatible` 是一个字符串列表，要找的那一项不一定是第一项。
- 中断号不是直接写在 `interrupts` 里的：GIC 的格式是三个单元（类型、编号、触发方式），
  共享外设中断 (SPI) 的中断号是编号加 32，每 CPU 私有的 (PPI) 是编号加 16。

目前内核只用了其中的内存范围，交给物理内存分配器。串口、GIC、定时器的地址在 QEMU virt
上是固定的，驱动里仍然是写死的；内核测试会核对设备树里读出来的值和这些写死的值一致。

## Multiboot 信息结构

```c
typedef struct {
    uint32_t flags;           // 标志位，指示哪些字段有效
    
    // 内存信息 (flags bit 0)
    uint32_t mem_lower;       // 低端内存 (KB)
    uint32_t mem_upper;       // 高端内存 (KB)
    
    // 引导设备 (flags bit 1)
    uint32_t boot_device;
    
    // 命令行 (flags bit 2)
    uint32_t cmdline;
    
    // 模块 (flags bit 3)
    uint32_t mods_count;
    uint32_t mods_addr;
    
    // 符号表 (flags bit 4 or 5)
    // ...
    
    // 内存映射 (flags bit 6)
    uint32_t mmap_length;
    uint32_t mmap_addr;
    
    // VBE 信息 (flags bit 11)
    // Framebuffer 信息 (flags bit 12)
    // ...
} multiboot_info_t;
```

## 内存映射

引导加载器提供详细的内存映射，指示哪些区域可用：

```c
typedef struct {
    uint32_t size;      // 此条目大小（不含 size 字段本身）
    uint64_t addr;      // 起始地址
    uint64_t len;       // 长度
    uint32_t type;      // 类型
    // 1 = 可用
    // 2 = 保留
    // 3 = ACPI 可回收
    // 4 = ACPI NVS
    // 5 = 坏内存
} multiboot_memory_map_t;
```

## 关键注意事项

### 1. 地址转换
在分页启用前，所有地址都是物理地址。Multiboot 信息结构中的地址也是物理地址，需要转换：

```c
#define PHYS_TO_VIRT(addr) ((addr) + 0x80000000)
```

### 2. 栈设置
引导代码必须设置栈才能调用 C 函数：

```asm
section .bss
align 16
stack_bottom:
    resb 16384  ; 16KB 内核栈
stack_top:
```

### 3. 恒等映射移除
跳转到高半核后应移除恒等映射，防止意外访问低地址。

### 4. 浮点单元
引导后 FPU 处于未初始化状态，如需使用浮点运算需要先初始化。

