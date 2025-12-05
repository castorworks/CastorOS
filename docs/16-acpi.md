# 阶段 16: ACPI 电源管理驱动

## 概述

本阶段实现了 ACPI (Advanced Configuration and Power Interface) 子系统的基础支持，使 CastorOS 能够在真实硬件（如 ThinkPad T41）上正确执行电源管理操作，特别是关机功能。

**📝 设计理念**：

之前的 `system_poweroff()` 使用硬编码的 QEMU/Bochs 特定 I/O 端口进行关机，这在模拟器上可以工作，但在真实硬件上无法正常关机。ACPI 是现代 PC 的标准电源管理接口，通过解析 ACPI 表可以获取硬件特定的电源管理端口，从而实现跨平台的关机支持。

**✅ 实现功能**：

✅ **RSDP 定位**
   - 在 EBDA 和 BIOS ROM 区域搜索 RSDP
   - 支持 ACPI 1.0 和 2.0+ 版本
   - 校验和验证

✅ **ACPI 表解析**
   - RSDT (Root System Description Table) 解析
   - FADT (Fixed ACPI Description Table) 解析
   - DSDT (Differentiated System Description Table) 解析

✅ **电源管理**
   - S5 (Soft Off) 状态支持
   - PM1a/PM1b 控制寄存器操作
   - _S5 对象的 AML 字节码解析
   - ACPI 模式启用

✅ **系统重置**
   - ACPI Reset Register 支持（如果 FADT 提供）

---

## 目标

- [x] 实现 RSDP 搜索功能
- [x] 实现 RSDT/FADT/DSDT 表解析
- [x] 实现 _S5 对象的 AML 解析以获取 SLP_TYP 值
- [x] 实现 ACPI 模式启用
- [x] 实现基于 ACPI 的关机功能
- [x] 实现基于 ACPI 的重启功能
- [x] 集成到内核初始化流程
- [x] 添加 `acpi` shell 命令用于调试

---

## 技术背景

### ACPI 概述

**ACPI (Advanced Configuration and Power Interface)** 是 1996 年由 Intel、Microsoft、Toshiba 等公司联合推出的开放标准，用于操作系统与硬件之间的电源管理和配置接口。

**ACPI 主要功能**：
- 电源管理（睡眠、休眠、关机）
- 热量管理（温度监控、风扇控制）
- 设备配置（即插即用）
- 中断路由

**ACPI 电源状态**：

| 状态 | 名称 | 描述 |
|------|------|------|
| S0 | Working | 工作状态（全功率） |
| S1 | Sleeping | CPU 停止，上下文保留 |
| S2 | Sleeping | CPU 断电 |
| S3 | STR | 挂起到内存 (Suspend to RAM) |
| S4 | Hibernate | 挂起到磁盘 |
| S5 | Soft Off | 软关机 |

### ACPI 表层次结构

```
RSDP (Root System Description Pointer)
  │
  └─→ RSDT (Root System Description Table)
        │
        ├─→ FADT (Fixed ACPI Description Table)
        │     │
        │     └─→ DSDT (Differentiated System Description Table)
        │           └─→ _S5 对象 (S5 状态的 SLP_TYP 值)
        │
        ├─→ MADT (Multiple APIC Description Table)
        │
        ├─→ SSDT (Secondary System Description Table)
        │
        └─→ ... 其他表
```

### RSDP (Root System Description Pointer)

RSDP 是 ACPI 表的入口点，位于以下内存区域之一：
1. **EBDA (Extended BIOS Data Area)** 的前 1KB
2. **BIOS ROM 区域** (0xE0000 - 0xFFFFF)

**RSDP 结构 (ACPI 1.0)**：
```c
typedef struct {
    char signature[8];      // "RSD PTR " (注意末尾空格)
    uint8_t checksum;       // 前 20 字节的校验和
    char oem_id[6];         // OEM 标识
    uint8_t revision;       // 0 = ACPI 1.0, 2 = ACPI 2.0+
    uint32_t rsdt_address;  // RSDT 物理地址
} __attribute__((packed)) acpi_rsdp_v1_t;
```

**搜索算法**：
```
1. 从 BDA (0x40E) 获取 EBDA 段地址
2. 在 EBDA 前 1KB 搜索 "RSD PTR " 签名
3. 如果未找到，在 0xE0000-0xFFFFF 搜索
4. 找到后验证校验和
```

### FADT (Fixed ACPI Description Table)

FADT 包含 ACPI 固定硬件的配置信息，最重要的是 **PM 控制寄存器地址**。

**关键字段**：

| 偏移 | 字段 | 说明 |
|------|------|------|
| 0x24 | pm1a_cnt_blk | PM1a 控制寄存器端口 ⭐ |
| 0x28 | pm1b_cnt_blk | PM1b 控制寄存器端口 ⭐ |
| 0x30 | smi_cmd | SMI 命令端口 |
| 0x34 | acpi_enable | 启用 ACPI 的 SMI 命令 |
| 0x14 | dsdt | DSDT 物理地址 |

### PM1 控制寄存器 (PM1_CNT)

PM1 控制寄存器用于触发电源状态转换。

**寄存器格式**：
```
 15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
|   |   |SLP|       SLP_TYP     |   |   |   |   |   |   |   |SCI|
|   |   |_EN|                   |   |   |   |   |   |   |   |_EN|
+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

Bit 13:    SLP_EN  - 睡眠使能（写 1 触发状态转换）
Bit 12-10: SLP_TYP - 睡眠类型（决定进入哪个 Sx 状态）
Bit 0:     SCI_EN  - SCI 中断使能（1 = ACPI 模式）
```

**关机流程**：
```
1. 从 DSDT 的 _S5 对象获取 SLP_TYPa/SLP_TYPb 值
2. 构造 PM1_CNT 值: (SLP_TYP << 10) | SLP_EN
3. 写入 PM1a_CNT_BLK 端口
4. 如果存在 PM1b_CNT_BLK，也写入
5. 系统关机
```

### _S5 对象与 AML 字节码

_S5 对象在 DSDT 中定义，包含 S5 状态的 SLP_TYP 值。

**_S5 对象的 AML 结构**：
```
Name(_S5_, Package() { slp_typa, slp_typb, 0, 0 })

AML 字节码示例：
08 5F 53 35 5F 12 06 04 0A 05 0A 05 00 00
^  ^  ^  ^  ^  ^  ^  ^  ^  ^  ^  ^  ^  ^
|  |  |  |  |  |  |  |  |  |  |  |  |  └─ 第 4 个元素 (0)
|  |  |  |  |  |  |  |  |  |  |  |  └──── 第 3 个元素 (0)
|  |  |  |  |  |  |  |  |  |  └──|──────── slp_typb (0x05)
|  |  |  |  |  |  |  |  └──|──────────── BytePrefix
|  |  |  |  |  |  |  |  └──|────────────── slp_typa (0x05)
|  |  |  |  |  |  |  └─────────────────── BytePrefix
|  |  |  |  |  |  └───────────────────── NumElements (4)
|  |  |  |  |  └──────────────────────── PkgLength
|  |  |  |  └─────────────────────────── PackageOp
|  └──|──|────────────────────────────── "_S5_"
└─────────────────────────────────────── NameOp
```

**常见 SLP_TYP 值**：
| 硬件 | SLP_TYP |
|------|---------|
| 大多数 PC | 5 或 7 |
| ThinkPad | 7 |
| QEMU | 5 |

---

## 实现设计

### 1. 文件结构

```
src/
├── include/
│   └── drivers/
│       └── acpi.h         # ACPI 驱动头文件
└── drivers/
    └── acpi.c             # ACPI 驱动实现
```

### 2. 核心数据结构

**ACPI 信息结构**：
```c
typedef struct {
    bool initialized;           // ACPI 是否已初始化
    uint8_t revision;           // ACPI 版本
    
    // 表指针
    acpi_rsdp_v1_t *rsdp;       // RSDP 指针
    acpi_rsdt_t *rsdt;          // RSDT 指针
    acpi_fadt_t *fadt;          // FADT 指针
    acpi_sdt_header_t *dsdt;    // DSDT 指针
    
    // PM 控制端口
    uint32_t pm1a_cnt_blk;      // PM1a 控制块端口
    uint32_t pm1b_cnt_blk;      // PM1b 控制块端口
    uint16_t pm1_cnt_len;       // PM1 控制块长度
    
    // S5 (软关机) 状态值
    uint16_t slp_typa;          // SLP_TYPa 值
    uint16_t slp_typb;          // SLP_TYPb 值
    bool s5_valid;              // S5 值是否有效
    
    // SCI 中断
    uint16_t sci_int;           // SCI 中断号
} acpi_info_t;
```

### 3. 初始化流程

```c
int acpi_init(void) {
    // 1. 搜索 RSDP
    acpi_info.rsdp = acpi_find_rsdp();
    
    // 2. 获取并验证 RSDT
    acpi_info.rsdt = (acpi_rsdt_t *)PHYS_TO_VIRT(rsdp->rsdt_address);
    
    // 3. 查找 FADT
    acpi_info.fadt = acpi_find_table("FACP");
    
    // 4. 提取 PM 控制端口
    acpi_info.pm1a_cnt_blk = fadt->pm1a_cnt_blk;
    acpi_info.pm1b_cnt_blk = fadt->pm1b_cnt_blk;
    
    // 5. 获取 DSDT 并解析 _S5 对象
    acpi_info.dsdt = (acpi_sdt_header_t *)PHYS_TO_VIRT(fadt->dsdt);
    acpi_parse_s5();
    
    // 6. 启用 ACPI 模式
    acpi_enable();
    
    return 0;
}
```

### 4. 关机实现

```c
int acpi_poweroff(void) {
    // 禁用中断
    __asm__ volatile ("cli");
    
    // 构建 PM1_CNT 值: (SLP_TYP << 10) | SLP_EN
    uint16_t slp_typa_value = (acpi_info.slp_typa << 10) | ACPI_SLP_EN;
    uint16_t slp_typb_value = (acpi_info.slp_typb << 10) | ACPI_SLP_EN;
    
    // 写入 PM1a_CNT_BLK
    outw(acpi_info.pm1a_cnt_blk, slp_typa_value);
    
    // 如果存在 PM1b_CNT_BLK，也写入
    if (acpi_info.pm1b_cnt_blk) {
        outw(acpi_info.pm1b_cnt_blk, slp_typb_value);
    }
    
    // 等待关机（正常情况下不会到达这里）
    return -1;
}
```

---

## 内核集成

### 在 kernel.c 中初始化

```c
// kernel.c
#include <drivers/acpi.h>

void kernel_main(multiboot_info_t *mbi) {
    // ... 其他初始化 ...
    
    // 4.6 初始化 ACPI 子系统
    int acpi_result = acpi_init();
    if (acpi_result == 0) {
        LOG_INFO_MSG("  [4.6] ACPI initialized\n");
        acpi_print_info();
    } else {
        LOG_WARN_MSG("  [4.6] ACPI initialization failed\n");
    }
    
    // ... 继续其他初始化 ...
}
```

### 更新 system_poweroff()

```c
// system.c
#include <drivers/acpi.h>

void system_poweroff(void) {
    __asm__ volatile ("cli");
    
    // 尝试 ACPI 关机
    if (acpi_poweroff() == 0) {
        system_halt_forever();
    }
    
    // 回退到 QEMU/Bochs 特定端口
    outw(0xB004, 0x2000);  // QEMU
    outw(0x604, 0x2000);   // QEMU (新版)
    outw(0x4004, 0x3400);  // VirtualBox
    
    system_halt_forever();
}
```

---

## Shell 命令

添加 `acpi` 命令用于调试：

```
CastorOS> acpi

=============================== ACPI Info ==================================
ACPI Revision:    0 (1.0)
OEM ID:           'PTLTD '
PM1a_CNT_BLK:     0x1004
PM1b_CNT_BLK:     0x0000
PM1_CNT_LEN:      2 bytes
SCI Interrupt:    IRQ 9
S5 SLP_TYPa:      0x7
S5 SLP_TYPb:      0x7
SMI_CMD:          0x00b2
ACPI_ENABLE:      0xf0
ACPI_DISABLE:     0xf1
================================================================================
```

---

## 测试方法

### 1. QEMU 测试

```bash
# 启动 QEMU 并测试关机
qemu-system-i386 -kernel build/castor.bin -serial stdio

# 在 shell 中执行
CastorOS> acpi      # 查看 ACPI 信息
CastorOS> poweroff  # 测试关机
```

### 2. ThinkPad T41 真机测试

1. 将 `build/bootable.img` 写入 U 盘或 CF 卡
2. 从 U 盘/CF 卡启动 ThinkPad T41
3. 执行 `acpi` 命令查看 ACPI 信息
4. 执行 `poweroff` 命令测试关机

**预期结果**：
- QEMU: 窗口关闭或显示 "Machine halted"
- ThinkPad T41: 电源灯熄灭，系统完全关机

---

## 故障排除

### ACPI 初始化失败

**问题**: "ACPI: RSDP not found"

**原因**: 
- BIOS 不支持 ACPI
- RSDP 签名被破坏

**解决方案**:
- 检查 BIOS 设置中是否启用 ACPI
- 确保内核正确映射了低端内存

### 关机无效

**问题**: 执行 `poweroff` 后系统没有关机

**原因**:
- SLP_TYP 值不正确
- PM 控制端口地址错误
- ACPI 模式未启用

**调试步骤**:
```
1. 执行 `acpi` 查看配置
2. 检查 PM1a_CNT_BLK 是否非零
3. 检查 SLP_TYP 值是否有效
4. 尝试修改默认 SLP_TYP 值
```

### DSDT 中找不到 _S5

**问题**: "ACPI: _S5 object not found in DSDT"

**解决方案**:
代码会自动使用默认值 (SLP_TYP=5)。如果不工作，可以尝试手动指定：

```c
// 在 acpi.c 中修改默认值
acpi_info.slp_typa = 0x07;  // 尝试 7 (ThinkPad 常用)
acpi_info.slp_typb = 0x07;
```

---

## 依赖关系

### 前置依赖

| 依赖模块 | 用途 |
|----------|------|
| I/O 端口操作 (io.h) | inb/outb/inw/outw |
| 虚拟内存管理 (vmm.h) | PHYS_TO_VIRT 宏 |
| 内核日志 (klog.h) | 日志输出 |

### 提供功能

| 功能 | 使用者 |
|------|--------|
| acpi_poweroff() | system.c 关机功能 |
| acpi_reset() | system.c 重启功能（可选） |
| acpi_print_info() | kernel_shell.c acpi 命令 |

---

## 已知限制

1. **仅支持 ACPI 1.0 表解析**
   - 暂不支持 XSDT (64 位表指针)
   - 暂不支持 ACPI 2.0+ 的扩展字段

2. **AML 解析有限**
   - 仅实现简单的 _S5 对象解析
   - 不支持复杂的 AML 字节码（方法调用等）

3. **不支持睡眠状态**
   - 仅实现 S5 (关机)
   - S3 (挂起到内存) 需要额外的内存保存/恢复逻辑

---

## 未来改进

- [ ] 添加 XSDT 支持 (ACPI 2.0+)
- [ ] 完善 AML 解释器
- [ ] 添加 S3 睡眠状态支持
- [ ] 添加 ACPI 事件处理（电源按钮、盖子开关等）
- [ ] 添加 ACPI 热量管理支持

---

## 参考资料

1. **ACPI 规范**
   - [ACPI Specification 6.4](https://uefi.org/specs/ACPI/6.4/)
   - [ACPI 1.0 Specification](https://www.acpi.info)

2. **OSDev Wiki**
   - [ACPI](https://wiki.osdev.org/ACPI)
   - [RSDP](https://wiki.osdev.org/RSDP)
   - [FADT](https://wiki.osdev.org/FADT)

3. **开源实现参考**
   - Linux kernel: `drivers/acpi/`
   - SerenityOS: `Kernel/ACPI/`
   - Haiku: `src/system/boot/platform/bios_ia32/acpi.cpp`

