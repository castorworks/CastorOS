# 阶段 1: 引导过程

## 概述

本阶段将实现 CastorOS 的引导过程，使内核能够从 GRUB 启动并显示基本信息。

---

## 目标

- ✅ 实现符合 Multiboot 规范的内核头部
- ✅ 实现高半核的早期页表设置
- ✅ 编写引导汇编代码，初始化执行环境
- ✅ 实现基本的 VGA 文本模式驱动
- ✅ 实现基本的串口驱动
- ✅ 创建 C 语言内核入口函数
- ✅ 成功从 GRUB 启动并显示 "Hello, CastorOS!"

---

## 技术背景

### 什么是 Multiboot？

Multiboot 是一个开放的引导规范，定义了引导加载器（如 GRUB）与内核之间的接口。符合 Multiboot 规范的内核可以被任何支持该规范的引导加载器加载。

**Multiboot 规范要求**：
- 内核文件前 8KB 必须包含一个特殊的头部
- 头部包含三个关键字段：魔数、标志、校验和
- 魔数必须是 `0x1BADB002`
- 校验和 = `-(魔数 + 标志)`

### 什么是高半核（Higher Half Kernel）？

高半核是一种内核设计模式，将内核映射到虚拟地址空间的高地址区域（我们选择 0x80000000，即 2GB，为了简化后面的内存映射）。

**优势**：
- 用户程序可以使用完整的低地址空间（0x00000000 - 0x7FFFFFFF）
- 内核和用户空间分离更清晰
- 符合现代操作系统的设计模式（Linux、Windows 都使用类似设计）

**挑战**：
- 需要在启用分页之前建立临时页表
- 需要同时映射物理地址和虚拟地址（恒等映射）
- 启用分页后需要跳转到高地址
- 需要确保 Multiboot 信息结构在映射范围内（我们映射前 8MB 以保证安全）

### x86 分页机制简介

x86 架构使用两级页表结构：
- **页目录（Page Directory）**：包含 1024 个页目录项（PDE），每个 4 字节
- **页表（Page Table）**：每个页表包含 1024 个页表项（PTE），每个 4 字节
- **页大小**：4KB（4096 字节）

**地址转换过程**：
```
虚拟地址（32位）= [页目录索引(10位)][页表索引(10位)][页内偏移(12位)]
                    ↓               ↓              ↓
                  PDE → 页表基址 → PTE → 物理页帧 → 最终物理地址
```

---

## 实现步骤

### 步骤 1: 创建 Multiboot 头部

Multiboot 头部必须在内核文件的前 8KB，我们将其放在单独的汇编文件中。

**文件**: `src/boot/multiboot.asm`

```nasm
; ============================================================================
; multiboot.asm - Multiboot 头部
; ============================================================================
; 这个文件定义了符合 Multiboot 规范的内核头部
; 必须在内核文件的前 8KB 内

section .multiboot
align 4

; Multiboot 常量定义
MULTIBOOT_MAGIC        equ 0x1BADB002  ; Multiboot 魔数
MULTIBOOT_PAGE_ALIGN   equ 1 << 0      ; 页对齐标志
MULTIBOOT_MEMORY_INFO  equ 1 << 1      ; 请求内存信息
MULTIBOOT_FLAGS        equ MULTIBOOT_PAGE_ALIGN | MULTIBOOT_MEMORY_INFO
MULTIBOOT_CHECKSUM     equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

; Multiboot 头部结构
multiboot_header:
    dd MULTIBOOT_MAGIC              ; 魔数
    dd MULTIBOOT_FLAGS              ; 标志
    dd MULTIBOOT_CHECKSUM           ; 校验和
```

**说明**：
- `section .multiboot`: 定义一个特殊的段，链接器脚本会将其放在内核文件最前面
- `align 4`: 头部必须 4 字节对齐
- `MULTIBOOT_PAGE_ALIGN`: 请求引导加载器按页边界对齐所有模块
- `MULTIBOOT_MEMORY_INFO`: 请求引导加载器提供内存映射信息
- `dd`: 定义双字（4 字节）

---

### 步骤 2: 创建引导汇编代码

引导代码负责设置执行环境并跳转到 C 内核入口。对于高半核，这个过程相对复杂，但是对后续的开发过程，帮助较大。

**文件**: `src/boot/boot.asm`

```nasm
; ============================================================================
; boot.asm - 内核引导代码
; ============================================================================
; 这个文件包含内核的第一段执行代码
; 主要任务：
;   1. 建立临时页表（支持高半核）
;   2. 启用分页
;   3. 设置栈
;   4. 跳转到 C 内核入口

[BITS 32]

; 常量定义
KERNEL_VIRTUAL_BASE equ 0x80000000                   ; 内核虚拟地址基址（2GB）
KERNEL_PAGE_NUMBER  equ (KERNEL_VIRTUAL_BASE >> 22)  ; 页目录索引 = 512

section .data
align 0x1000  ; 页表必须 4KB 对齐

; 引导页目录
; 需要映射的区域：
;   1. 0x00000000 - 0x00400000 (恒等映射，用于启用分页过渡) - 4MB
;   2. 0x80000000 - 0x80800000 (高半核映射，映射前 8MB 物理内存)
boot_page_directory:
    ; 页目录第 0 项：映射前 4MB（恒等映射）
    dd (boot_page_table1 - KERNEL_VIRTUAL_BASE) + 0x003
    
    ; 页目录第 1-511 项：未映射
    times (KERNEL_PAGE_NUMBER - 1) dd 0
    
    ; 页目录第 512-513 项：映射 0x80000000-0x80800000（高半核映射 8MB）
    dd (boot_page_table1 - KERNEL_VIRTUAL_BASE) + 0x003  ; 0-4MB
    dd (boot_page_table2 - KERNEL_VIRTUAL_BASE) + 0x003  ; 4-8MB
    
    ; 页目录第 514-1023 项：未映射
    times (1024 - KERNEL_PAGE_NUMBER - 2) dd 0

; 引导页表 1：映射物理内存 0-4MB
; 每个页表项映射一个 4KB 页
; 标志位 0x003 = Present(1) | Read/Write(1) | User/Supervisor(0)
; 注：此阶段仅需 P 和 RW 位，未设置 US/PCD/PWT/Global 等（简化设计）
boot_page_table1:
    %assign i 0
    %rep 1024
        dd (i << 12) | 0x003  ; 地址 | 标志位 (Present | Read/Write)
        %assign i i+1
    %endrep

; 引导页表 2：映射物理内存 4MB-8MB
; 确保 Multiboot 信息结构和其他引导数据在映射范围内
boot_page_table2:
    %assign i 1024
    %rep 1024
        dd (i << 12) | 0x003  ; 地址 | 标志位 (Present | Read/Write)
        %assign i i+1
    %endrep

section .text
align 4

global _start
global boot_page_directory
global boot_page_table1
global boot_page_table2
global stack_bottom
global stack_top
extern kernel_main

_start:
    ; 此时，GRUB 已经：
    ;   - 将内核加载到物理地址 0x100000
    ;   - 将 Multiboot 信息结构地址放在 ebx
    ;   - 将魔数 0x2BADB002 放在 eax
    ;   - 处于保护模式，禁用分页
    ;   - A20 线已启用
    
    ; 0. 禁用中断（防止在设置分页期间被中断打断）
    cli
    
    ; 1. 加载页目录地址到 CR3
    mov ecx, (boot_page_directory - KERNEL_VIRTUAL_BASE)
    mov cr3, ecx
    
    ; 2. 启用分页：设置 CR0 的 PG 位
    mov ecx, cr0
    or ecx, 0x80000000
    mov cr0, ecx
    
    ; 3. 跳转到高半核地址
    ; 此跳转会刷新指令流水线，确保后续指令在分页模式下正确执行
    lea ecx, [higher_half]
    jmp ecx

higher_half:
    ; 现在我们运行在高半核地址空间
    
    ; 4. 取消恒等映射（不再需要）
    mov dword [boot_page_directory], 0
    
    ; 5. 刷新 TLB (Translation Lookaside Buffer)
    mov ecx, cr3
    mov cr3, ecx
    
    ; 6. 设置栈指针
    mov esp, stack_top
    ; 注：栈已在 BSS 段 16 字节对齐
    ; 若需严格 16 字节对齐（SSE/System V ABI），可添加：
    ; and esp, 0xFFFFFFF0
    
    ; 7. 重置 EFLAGS
    push 0
    popf
    
    ; 8. 保存 Multiboot 信息
    ; ebx 包含 Multiboot 信息结构的物理地址
    ; 需要加上虚拟地址偏移
    ; 注意：我们已映射前 8MB 物理内存到高半核，确保 MBI 在可访问范围内
    add ebx, KERNEL_VIRTUAL_BASE
    push ebx  ; 传递给 kernel_main
    
    ; 9. 调用 C 内核入口
    call kernel_main
    
    ; 10. 如果 kernel_main 返回，进入死循环
    cli         ; 禁用中断
.hang:
    hlt         ; 停机
    jmp .hang

; 内核栈（16KB）
section .bss
align 16
stack_bottom:
    resb 16384  ; 16KB 栈空间
stack_top:
```

**代码详解**：

#### 1. 数据段 - 页表结构设置

**常量定义**：
```nasm
KERNEL_VIRTUAL_BASE equ 0x80000000                   ; 内核虚拟地址基址（2GB）
KERNEL_PAGE_NUMBER  equ (KERNEL_VIRTUAL_BASE >> 22)  ; 页目录索引 = 512
```
- `KERNEL_VIRTUAL_BASE`: 内核映射到虚拟地址 0x80000000（2GB 处）
- `KERNEL_PAGE_NUMBER`: 计算虚拟地址在页目录中的索引
  - 32位虚拟地址 = [页目录索引(10位)][页表索引(10位)][页内偏移(12位)]
  - 右移22位得到页目录索引：0x80000000 >> 22 = 512

**页目录结构**：
```nasm
boot_page_directory:
    ; 页目录第 0 项：映射前 4MB（恒等映射）
    dd (boot_page_table1 - KERNEL_VIRTUAL_BASE) + 0x003
    
    ; 页目录第 1-511 项：未映射
    times (KERNEL_PAGE_NUMBER - 1) dd 0
    
    ; 页目录第 512-513 项：映射 0x80000000-0x80800000（高半核映射 8MB）
    dd (boot_page_table1 - KERNEL_VIRTUAL_BASE) + 0x003  ; 0-4MB
    dd (boot_page_table2 - KERNEL_VIRTUAL_BASE) + 0x003  ; 4-8MB
    
    ; 页目录第 514-1023 项：未映射
    times (1024 - KERNEL_PAGE_NUMBER - 2) dd 0
```

**关键点解析**：
1. **物理地址计算**：`boot_page_table1 - KERNEL_VIRTUAL_BASE`
   - 链接器将 `boot_page_table1` 赋予高地址（如 0x80001000）
   - 减去 `KERNEL_VIRTUAL_BASE` 得到物理地址（如 0x00001000）
   - 这是因为链接器脚本中使用了 `AT(ADDR(.data) - 0x80000000)`

2. **页表标志位 0x003**：
   - Bit 0 (Present) = 1：页表存在
   - Bit 1 (Read/Write) = 1：可读写
   - Bit 2 (User/Supervisor) = 0：仅内核可访问

3. **对齐要求**：
   - `align 0x1000` 确保页目录和页表 4KB 对齐（x86 硬件要求）

**页表结构**：
```nasm
boot_page_table1:
    %assign i 0
    %rep 1024
        dd (i << 12) | 0x003  ; 地址 | 标志位
        %assign i i+1
    %endrep
```
- 使用 NASM 预处理器生成 1024 个页表项
- 每项映射一个 4KB 页面：`i << 12` 得到物理地址（i * 4096）
- 映射范围：0x00000000 - 0x003FFFFF（4MB）

**为什么需要两个页表？**
```nasm
boot_page_table2:
    %assign i 1024
    %rep 1024
        dd (i << 12) | 0x003
        %assign i i+1
    %endrep
```
- `boot_page_table2` 映射物理内存 4MB-8MB
- 确保 Multiboot 信息结构（GRUB 传递的）在映射范围内
- Multiboot 信息可能被放置在内核镜像之后的任何位置

#### 2. 恒等映射和高半核映射

我们建立了两种映射：

**映射布局**：
- **页目录[0]** → `boot_page_table1` → 物理 0x00000000-0x003FFFFF（4MB 恒等映射）
  - 虚拟 0x00000000 映射到物理 0x00000000
  - 用于启用分页时的过渡（因为代码当前运行在低地址）
  
- **页目录[512]** → `boot_page_table1` → 物理 0x00000000-0x003FFFFF（高半核映射）
  - 虚拟 0x80000000 映射到物理 0x00000000
  - 内核代码和数据的实际运行地址
  
- **页目录[513]** → `boot_page_table2` → 物理 0x00400000-0x007FFFFF（高半核映射）
  - 虚拟 0x80400000 映射到物理 0x00400000
  - 扩展映射，确保 Multiboot 信息和引导数据可访问

**总映射范围**：
- 恒等映射：虚拟 0x00000000-0x003FFFFF → 物理 0x00000000-0x003FFFFF（4MB）
- 高半核映射：虚拟 0x80000000-0x807FFFFF → 物理 0x00000000-0x007FFFFF（8MB）

**为什么需要恒等映射？**
当我们执行 `mov cr0, ecx` 启用分页时，CPU 立即开始使用虚拟地址。但此时：
- EIP（指令指针）指向低地址（如 0x00100000）
- 如果没有恒等映射，CPU 将无法找到下一条指令
- 恒等映射确保 0x00100000（虚拟）→ 0x00100000（物理）仍然可用

之后，我们跳转到高地址并移除恒等映射。

#### 3. 文本段 - 启动代码

**步骤 0-2: 启用分页**
```nasm
_start:
    ; 0. 禁用中断
    cli
    
    ; 1. 加载页目录地址到 CR3
    mov ecx, (boot_page_directory - KERNEL_VIRTUAL_BASE)
    mov cr3, ecx
    
    ; 2. 启用分页：设置 CR0 的 PG 位
    mov ecx, cr0
    or ecx, 0x80000000
    mov cr0, ecx
```
- **CLI**：禁用中断，防止在设置分页期间被中断打扰
- **CR3**：存放页目录的物理地址（必须是物理地址，不能是虚拟地址）
- **CR0.PG（第31位）**：设置此位启用分页机制

**步骤 3: 跳转到高半核**
```nasm
    ; 3. 跳转到高半核地址
    lea ecx, [higher_half]
    jmp ecx
```
- `lea ecx, [higher_half]`：加载 `higher_half` 的虚拟地址（约 0x80100xxx）
- `jmp ecx`：绝对跳转，刷新指令流水线
- 此后所有代码运行在高半核地址空间

**步骤 4-5: 清理恒等映射**
```nasm
higher_half:
    ; 4. 取消恒等映射
    mov dword [boot_page_directory], 0
    
    ; 5. 刷新 TLB
    mov ecx, cr3
    mov cr3, ecx
```
- 将页目录第0项清零，移除恒等映射
- 重新加载 CR3 刷新 TLB（Translation Lookaside Buffer）
- TLB 是 CPU 缓存页表转换的硬件，必须刷新才能生效

**步骤 6-7: 设置栈和 EFLAGS**
```nasm
    ; 6. 设置栈指针
    mov esp, stack_top
    
    ; 7. 重置 EFLAGS
    push 0
    popf
```
- `stack_top` 是在 BSS 段定义的栈顶（16KB 栈空间）
- 重置 EFLAGS 清除所有标志位（方向标志、进位标志等）

**步骤 8: 传递 Multiboot 信息**
```nasm
    ; 8. 保存 Multiboot 信息
    add ebx, KERNEL_VIRTUAL_BASE
    push ebx
```
- GRUB 在 EBX 寄存器中传递 Multiboot 信息结构的**物理地址**
- 加上 `KERNEL_VIRTUAL_BASE` 转换为虚拟地址
- 通过栈传递给 C 函数 `kernel_main`

**步骤 9-10: 调用内核入口**
```nasm
    ; 9. 调用 C 内核入口
    call kernel_main
    
    ; 10. 如果返回，进入死循环
    cli
.hang:
    hlt
    jmp .hang
```
- 调用 C 函数 `kernel_main(multiboot_info_t* mbi)`
- 如果 `kernel_main` 返回（不应该发生），进入死循环
- `HLT` 指令暂停 CPU 直到下一个中断

#### 4. BSS 段 - 内核栈

```nasm
section .bss
align 16
stack_bottom:
    resb 16384  ; 16KB 栈空间
stack_top:
```
- BSS 段包含未初始化的数据（由引导加载器清零）
- 栈从高地址向低地址增长，所以 `stack_top` 是栈指针的初始值
- 16KB 栈空间对于早期内核已足够

#### 5. 控制寄存器总结

**CR0（控制寄存器 0）**：
- Bit 31 (PG)：启用分页
- Bit 0 (PE)：保护模式（GRUB 已设置）

**CR3（页目录基址寄存器）**：
- 存放页目录的物理地址（必须 4KB 对齐）
- 重新加载 CR3 会刷新 TLB

**CR4（控制寄存器 4）**：
- Bit 4 (PSE)：页大小扩展（支持 4MB 大页）
- 本实现使用 4KB 页，不需要 PSE

---

### 步骤 3: 通用类型定义

**文件**: `src/include/types.h`

### 步骤 4: 实现 VGA 文本模式驱动

VGA 文本模式是最简单的屏幕输出方式，无需复杂的图形驱动。

**文件**: `src/drivers/vga.c` 以及 `src/include/drivers/vga.h`

---

### 步骤 5: 实现串口驱动

串口（Serial Port）是调试操作系统的重要工具，可以输出调试信息而不影响 VGA 屏幕显示。

**文件**: `src/include/kernel/io.h`
**文件**: `src/drivers/serial.c` 以及 `src/include/drivers/serial.h`

---

### 步骤 6: 定义 Multiboot 信息结构

在内核能够正确解析引导加载器传递的信息之前，我们需要定义完整的 Multiboot 信息结构。这个结构包含了内存映射、模块、命令行参数等重要信息。

**文件**: `src/include/kernel/multiboot.h`

---

### 步骤 7: 创建内核入口函数

**版本**: `src/include/kernel/version.h`

```c
#ifndef _KERNEL_VERSION_H_
#define _KERNEL_VERSION_H_

#define KERNEL_VERSION "0.0.1"

#endif /* _KERNEL_VERSION_H_ */
```

**文件**: `src/kernel/kernel.c`

```c
// ============================================================================
// kernel.c - 内核主函数
// ============================================================================

#include <drivers/vga.h>
#include <drivers/serial.h>
#include <kernel/multiboot.h>
#include <kernel/version.h>

// 内核主函数
void kernel_main(multiboot_info_t* mbi) {
    // ========================================================================
    // 基础初始化
    // ========================================================================
    
    // 初始化 VGA
    vga_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    // 初始化串口
    serial_init();

    // ========================================================================
    // 启动信息
    // ========================================================================
    
    vga_print("Hello CastorOS!\n");
    serial_print("Hello CastorOS!\n");
    vga_print("Version: "KERNEL_VERSION"\n");
    serial_print("Version: "KERNEL_VERSION"\n");
    
    // 进入空闲循环
    while (1) {
        __asm__ volatile ("hlt");
    }
}
```

### 步骤 8: 编写 Makefile 和 linker.ld

**文件**: `Makefile` 以及 `linker.ld`

### 步骤 9: 编写 GRUB 配置文件

**文件**: `grub.cfg`

---

## 构建和测试

### 0. 确保工具已安装

在开始构建之前，确保 00-environment.md 中的环境已经准备好

### 1. 构建内核

```bash
cd /root/CastorOS
make clean
make
```

**注意**：
- 编译标志使用 `-O0 -g` 以便调试（无优化，包含调试信息）
- 汇编器使用 `-g -F dwarf` 生成 DWARF 调试信息
- 如需发布版本，可将 `-O0` 改为 `-O2`，并移除 `-g` 标志

### 2. 验证内核文件

```bash
# 检查文件大小
ls -lh build/castor.bin

# 验证 Multiboot 头部
grub-file --is-x86-multiboot build/castor.bin
echo $?  # 应该输出 0（成功）
```

### 3. 在 QEMU 中运行

```bash
# 需要在 Ubuntu GUI 中，Ctrl + Alt + T 打开终端，然后执行
make run
```

**预期结果**：
- QEMU 窗口打开
- 屏幕显示欢迎信息
- 显示系统信息
- 系统进入空闲循环

### 4. 调试技巧

**使用 QEMU 监视器**：

在 QEMU 窗口中按 `Ctrl+Alt+2` 进入 QEMU 监视器，常用命令：
- `info registers` - 查看寄存器状态
- `info mem` - 查看内存映射
- `info tlb` - 查看 TLB 状态
- 按 `Ctrl+Alt+1` 返回虚拟机屏幕

**使用 Cursor / VSCode 调试**：

1. 终端1：启动 QEMU 调试模式 `make debug`

2. 创建 `.vscode/launch.json`

3. 在 kernel.c 中设置断点

4. 按 F5 启动调试

**使用 QEMU 串口输出**：

如果实现了串口驱动，可以在终端看到输出（`make run` 已包含 `-serial stdio`）。
