# 引导过程

## 概述

CastorOS 使用 GRUB 作为引导加载器，遵循 Multiboot 规范。引导过程从 BIOS/UEFI 开始，经过 GRUB，最终将控制权交给内核。

## 引导流程

```
BIOS/UEFI
    ↓
GRUB (Multiboot)
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

内核必须在前 8KB 包含 Multiboot 头，告诉 GRUB 如何加载内核：

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

### GRUB 提供的信息

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

```c
void kernel_main(multiboot_info_t* mbi) {
    // 阶段 1: 基础输出
    vga_init();           // VGA 文本模式
    serial_init();        // 串口（调试）
    
    // 阶段 2: 中断系统
    gdt_init();           // 全局描述符表
    idt_init();           // 中断描述符表
    isr_init();           // 异常处理
    irq_init();           // 硬件中断
    syscall_init();       // 系统调用
    
    // 阶段 3: 内存管理
    pmm_init(mbi);        // 物理内存管理
    vmm_init();           // 虚拟内存管理
    heap_init();          // 堆分配器
    
    // 阶段 4: 设备驱动
    pit_init();           // 定时器
    keyboard_init();      // 键盘
    ata_init();           // 磁盘
    rtc_init();           // 实时时钟
    pci_init();           // PCI 总线
    acpi_init();          // ACPI
    
    // 阶段 5: 高级子系统
    task_init();          // 任务管理
    vfs_init();           // 文件系统
    
    // 阶段 6: 启动用户空间
    load_shell();         // 加载用户 shell
    scheduler_start();    // 启动调度器
}
```

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

GRUB 提供详细的内存映射，指示哪些区域可用：

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

