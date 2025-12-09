/**
 * @file acpi.c
 * @brief ACPI (Advanced Configuration and Power Interface) 驱动实现
 * 
 * 实现 ACPI 表解析和电源管理功能
 * 支持 ThinkPad T41 等老旧 PC 硬件的关机操作
 */

#include <drivers/acpi.h>
#include <kernel/io.h>
#include <mm/vmm.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>

/* ============================================================================
 * 私有变量
 * ============================================================================ */

static acpi_info_t acpi_info = {0};

/* ============================================================================
 * 辅助函数
 * ============================================================================ */

/**
 * @brief 验证 ACPI 表校验和
 * 
 * @param data 表数据指针
 * @param length 表长度
 * @return true 校验和有效，false 无效
 */
static bool acpi_validate_checksum(void *data, uint32_t length) {
    uint8_t sum = 0;
    uint8_t *ptr = (uint8_t *)data;
    
    for (uint32_t i = 0; i < length; i++) {
        sum += ptr[i];
    }
    
    return (sum == 0);
}

/**
 * @brief 比较表签名
 * 
 * @param sig1 签名 1
 * @param sig2 签名 2
 * @param len 比较长度
 * @return true 相等，false 不等
 */
static bool acpi_sig_match(const char *sig1, const char *sig2, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (sig1[i] != sig2[i]) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 在指定内存范围内搜索 RSDP
 * 
 * @param start 起始地址
 * @param end 结束地址
 * @return RSDP 指针，未找到返回 NULL
 */
static acpi_rsdp_v1_t *acpi_find_rsdp_in_range(uint32_t start, uint32_t end) {
    // RSDP 必须 16 字节对齐
    start = (start + 15) & ~15;
    
    for (uint32_t addr = start; addr < end; addr += 16) {
        acpi_rsdp_v1_t *rsdp = (acpi_rsdp_v1_t *)PHYS_TO_VIRT(addr);
        
        // 检查签名 "RSD PTR "
        if (acpi_sig_match(rsdp->signature, ACPI_SIG_RSDP, 8)) {
            // 验证 ACPI 1.0 校验和（前 20 字节）
            if (acpi_validate_checksum(rsdp, 20)) {
                LOG_DEBUG_MSG("ACPI: Found RSDP at 0x%x\n", addr);
                return rsdp;
            }
        }
    }
    
    return NULL;
}

/**
 * @brief 搜索 RSDP (Root System Description Pointer)
 * 
 * RSDP 位于以下位置之一：
 * 1. EBDA (Extended BIOS Data Area) 的前 1KB
 * 2. BIOS ROM 区域 (0xE0000 - 0xFFFFF)
 * 
 * @return RSDP 指针，未找到返回 NULL
 */
static acpi_rsdp_v1_t *acpi_find_rsdp(void) {
    acpi_rsdp_v1_t *rsdp = NULL;
    
    // 方法 1: 从 BDA (BIOS Data Area) 获取 EBDA 地址
    // EBDA 段地址存储在物理地址 0x40E 处（2 字节）
    uint16_t ebda_seg = *(uint16_t *)PHYS_TO_VIRT(0x40E);
    uint32_t ebda_addr = (uint32_t)ebda_seg << 4;
    
    if (ebda_addr >= 0x80000 && ebda_addr < 0xA0000) {
        LOG_DEBUG_MSG("ACPI: Searching EBDA at 0x%x\n", ebda_addr);
        rsdp = acpi_find_rsdp_in_range(ebda_addr, ebda_addr + 1024);
        if (rsdp) {
            return rsdp;
        }
    }
    
    // 方法 2: 搜索 BIOS ROM 区域
    LOG_DEBUG_MSG("ACPI: Searching BIOS ROM area (0xE0000 - 0xFFFFF)\n");
    rsdp = acpi_find_rsdp_in_range(0xE0000, 0x100000);
    
    return rsdp;
}

/**
 * @brief 查找指定签名的 ACPI 表
 * 
 * @param signature 表签名（4字节）
 * @return 表头指针，未找到返回 NULL
 */
static acpi_sdt_header_t *acpi_find_table(const char *signature) {
    if (!acpi_info.rsdt) {
        return NULL;
    }
    
    // 计算 RSDT 中表指针的数量
    uint32_t entries = (acpi_info.rsdt->header.length - sizeof(acpi_sdt_header_t)) / 4;
    uint32_t *table_ptrs = (uint32_t *)((uintptr_t)acpi_info.rsdt + sizeof(acpi_sdt_header_t));
    
    LOG_DEBUG_MSG("ACPI: RSDT has %u entries\n", entries);
    
    for (uint32_t i = 0; i < entries; i++) {
        uint32_t table_phys = table_ptrs[i];
        if (table_phys == 0) {
            continue;
        }
        
        acpi_sdt_header_t *header = (acpi_sdt_header_t *)PHYS_TO_VIRT(table_phys);
        
        LOG_DEBUG_MSG("ACPI: Table %u: '%.4s' at 0x%x\n", i, header->signature, table_phys);
        
        if (acpi_sig_match(header->signature, signature, 4)) {
            // 验证校验和
            if (acpi_validate_checksum(header, header->length)) {
                return header;
            } else {
                LOG_WARN_MSG("ACPI: Table '%.4s' checksum invalid\n", signature);
            }
        }
    }
    
    return NULL;
}

/**
 * @brief 解析 DSDT 中的 _S5 对象以获取 SLP_TYP 值
 * 
 * _S5 对象定义了 S5（软关机）状态的 SLP_TYPa 和 SLP_TYPb 值
 * 这些值需要写入 PM1a_CNT_BLK 和 PM1b_CNT_BLK 来触发关机
 * 
 * _S5 对象的 AML 结构通常是：
 * Name(_S5_, Package() { slp_typa, slp_typb, 0, 0 })
 * 
 * 我们需要在 DSDT 中搜索 "_S5_" 字符串，然后解析其值
 */
static bool acpi_parse_s5(void) {
    if (!acpi_info.dsdt) {
        LOG_ERROR_MSG("ACPI: DSDT not found, cannot parse _S5\n");
        return false;
    }
    
    uint8_t *dsdt = (uint8_t *)acpi_info.dsdt;
    uint32_t dsdt_length = acpi_info.dsdt->length;
    
    LOG_DEBUG_MSG("ACPI: Searching _S5 in DSDT (length=%u)\n", dsdt_length);
    
    // 在 DSDT 中搜索 "_S5_" 字符串
    // AML 字节码: 0x08 (NameOp) + "_S5_" + Package
    for (uint32_t i = 0; i < dsdt_length - 10; i++) {
        // 查找 "_S5_" 字符串
        if (dsdt[i] == '_' && dsdt[i+1] == 'S' && 
            dsdt[i+2] == '5' && dsdt[i+3] == '_') {
            
            LOG_DEBUG_MSG("ACPI: Found _S5_ at offset %u\n", i);
            
            // 检查是否是 NameOp (0x08) 定义
            // 格式: 08 5F 53 35 5F 12 [PkgLength] [NumElements] [BytePrefix] [Value] ...
            //       ^  ^  ^  ^  ^  ^                            ^
            //      Nam _  S  5  _  Pkg                          0A = BytePrefix
            
            // 跳过 "_S5_" (4 bytes)
            uint32_t j = i + 4;
            
            // 跳过可能的 AML 对象类型指示符
            // 检查是否是 Package (0x12)
            if (j < dsdt_length && dsdt[j] == 0x12) {
                j++; // 跳过 PackageOp (0x12)
                
                // 解析 PkgLength
                // PkgLength 可以是 1-4 字节，取决于最高两位
                uint8_t pkg_lead = dsdt[j];
                uint32_t pkg_len_bytes = (pkg_lead >> 6) & 0x03;
                
                if (pkg_len_bytes == 0) {
                    // 单字节长度
                    j++;
                } else {
                    // 多字节长度
                    j += pkg_len_bytes + 1;
                }
                
                // 跳过 NumElements
                if (j < dsdt_length) {
                    j++;
                }
                
                // 现在 j 应该指向第一个元素
                // 元素可以是：
                // - 0x00-0xFF: 直接值（如果是小整数）
                // - 0x0A: BytePrefix + 1字节值
                // - 0x0B: WordPrefix + 2字节值
                // - 0x0C: DWordPrefix + 4字节值
                
                uint16_t slp_typa = 0, slp_typb = 0;
                
                // 解析 SLP_TYPa
                if (j < dsdt_length) {
                    if (dsdt[j] == 0x0A && j + 1 < dsdt_length) {
                        // BytePrefix
                        slp_typa = dsdt[j + 1];
                        j += 2;
                    } else if (dsdt[j] == 0x0B && j + 2 < dsdt_length) {
                        // WordPrefix
                        slp_typa = dsdt[j + 1] | (dsdt[j + 2] << 8);
                        j += 3;
                    } else if (dsdt[j] == 0x0C && j + 4 < dsdt_length) {
                        // DWordPrefix
                        slp_typa = dsdt[j + 1] | (dsdt[j + 2] << 8);
                        j += 5;
                    } else if (dsdt[j] <= 0x09) {
                        // 直接小整数 (0-9 的简写)
                        slp_typa = dsdt[j];
                        j++;
                    } else {
                        // 尝试作为直接字节值
                        slp_typa = dsdt[j];
                        j++;
                    }
                }
                
                // 解析 SLP_TYPb
                if (j < dsdt_length) {
                    if (dsdt[j] == 0x0A && j + 1 < dsdt_length) {
                        slp_typb = dsdt[j + 1];
                        j += 2;
                    } else if (dsdt[j] == 0x0B && j + 2 < dsdt_length) {
                        slp_typb = dsdt[j + 1] | (dsdt[j + 2] << 8);
                        j += 3;
                    } else if (dsdt[j] == 0x0C && j + 4 < dsdt_length) {
                        slp_typb = dsdt[j + 1] | (dsdt[j + 2] << 8);
                        j += 5;
                    } else if (dsdt[j] <= 0x09) {
                        slp_typb = dsdt[j];
                        j++;
                    } else {
                        slp_typb = dsdt[j];
                        j++;
                    }
                }
                
                LOG_INFO_MSG("ACPI: _S5 parsed: SLP_TYPa=0x%x, SLP_TYPb=0x%x\n", 
                            slp_typa, slp_typb);
                
                // SLP_TYP 值应该在 0-7 范围内（3位）
                acpi_info.slp_typa = slp_typa & 0x07;
                acpi_info.slp_typb = slp_typb & 0x07;
                acpi_info.s5_valid = true;
                
                return true;
            }
            
            // 可能是其他格式，继续搜索
        }
    }
    
    LOG_WARN_MSG("ACPI: _S5 object not found in DSDT\n");
    
    // 尝试使用默认值
    // 很多系统使用 SLP_TYP=5 或 SLP_TYP=7 作为 S5 状态
    // ThinkPad 通常使用 SLP_TYP=7
    LOG_INFO_MSG("ACPI: Using default S5 SLP_TYP values (0x05)\n");
    acpi_info.slp_typa = 0x05;
    acpi_info.slp_typb = 0x05;
    acpi_info.s5_valid = true;
    
    return true;
}

/* ============================================================================
 * 公共函数实现
 * ============================================================================ */

int acpi_init(void) {
    LOG_INFO_MSG("ACPI: Initializing ACPI subsystem...\n");
    
    memset(&acpi_info, 0, sizeof(acpi_info));
    
    // 步骤 1: 搜索 RSDP
    acpi_info.rsdp = acpi_find_rsdp();
    if (!acpi_info.rsdp) {
        LOG_ERROR_MSG("ACPI: RSDP not found\n");
        return -1;
    }
    
    acpi_info.revision = acpi_info.rsdp->revision;
    LOG_INFO_MSG("ACPI: RSDP found, revision=%u (ACPI %s)\n", 
                 acpi_info.revision, 
                 acpi_info.revision >= 2 ? "2.0+" : "1.0");
    
    // 打印 OEM ID
    char oem_id[7] = {0};
    memcpy(oem_id, acpi_info.rsdp->oem_id, 6);
    LOG_INFO_MSG("ACPI: OEM ID: '%s'\n", oem_id);
    
    // 步骤 2: 获取 RSDT
    uint32_t rsdt_phys = acpi_info.rsdp->rsdt_address;
    if (rsdt_phys == 0) {
        LOG_ERROR_MSG("ACPI: RSDT address is NULL\n");
        return -1;
    }
    
    acpi_info.rsdt = (acpi_rsdt_t *)PHYS_TO_VIRT(rsdt_phys);
    
    // 验证 RSDT 签名
    if (!acpi_sig_match(acpi_info.rsdt->header.signature, ACPI_SIG_RSDT, 4)) {
        LOG_ERROR_MSG("ACPI: Invalid RSDT signature\n");
        return -1;
    }
    
    // 验证 RSDT 校验和
    if (!acpi_validate_checksum(acpi_info.rsdt, acpi_info.rsdt->header.length)) {
        LOG_WARN_MSG("ACPI: RSDT checksum invalid (may still work)\n");
    }
    
    LOG_INFO_MSG("ACPI: RSDT at 0x%x, length=%u\n", 
                 rsdt_phys, acpi_info.rsdt->header.length);
    
    // 步骤 3: 获取 FADT
    acpi_info.fadt = (acpi_fadt_t *)acpi_find_table(ACPI_SIG_FADT);
    if (!acpi_info.fadt) {
        LOG_ERROR_MSG("ACPI: FADT not found\n");
        return -1;
    }
    
    LOG_INFO_MSG("ACPI: FADT found, revision=%u\n", acpi_info.fadt->header.revision);
    
    // 提取 PM 控制端口
    acpi_info.pm1a_cnt_blk = acpi_info.fadt->pm1a_cnt_blk;
    acpi_info.pm1b_cnt_blk = acpi_info.fadt->pm1b_cnt_blk;
    acpi_info.pm1_cnt_len = acpi_info.fadt->pm1_cnt_len;
    acpi_info.sci_int = acpi_info.fadt->sci_int;
    
    LOG_INFO_MSG("ACPI: PM1a_CNT_BLK=0x%x, PM1b_CNT_BLK=0x%x, PM1_CNT_LEN=%u\n",
                 acpi_info.pm1a_cnt_blk, acpi_info.pm1b_cnt_blk, acpi_info.pm1_cnt_len);
    LOG_INFO_MSG("ACPI: SCI_INT=%u, SMI_CMD=0x%x\n", 
                 acpi_info.sci_int, acpi_info.fadt->smi_cmd);
    
    // 步骤 4: 获取 DSDT
    uint32_t dsdt_phys = acpi_info.fadt->dsdt;
    if (dsdt_phys) {
        acpi_info.dsdt = (acpi_sdt_header_t *)PHYS_TO_VIRT(dsdt_phys);
        
        if (acpi_sig_match(acpi_info.dsdt->signature, ACPI_SIG_DSDT, 4)) {
            LOG_INFO_MSG("ACPI: DSDT at 0x%x, length=%u\n", 
                         dsdt_phys, acpi_info.dsdt->length);
        } else {
            LOG_WARN_MSG("ACPI: Invalid DSDT signature\n");
            acpi_info.dsdt = NULL;
        }
    }
    
    // 步骤 5: 解析 _S5 对象
    acpi_parse_s5();
    
    // 步骤 6: 启用 ACPI（如果需要）
    acpi_enable();
    
    acpi_info.initialized = true;
    LOG_INFO_MSG("ACPI: Initialization complete\n");
    
    return 0;
}

bool acpi_is_initialized(void) {
    return acpi_info.initialized;
}

acpi_info_t *acpi_get_info(void) {
    return &acpi_info;
}

int acpi_enable(void) {
    if (!acpi_info.fadt) {
        return -1;
    }
    
    // 检查是否需要启用 ACPI
    // 如果 SMI_CMD 为 0 或 ACPI_ENABLE 为 0，则 ACPI 可能已经启用
    // 或者硬件不支持通过 SMI 切换
    if (acpi_info.fadt->smi_cmd == 0 || acpi_info.fadt->acpi_enable == 0) {
        LOG_INFO_MSG("ACPI: SMI command port is 0, ACPI may already be enabled\n");
        return 0;
    }
    
    // 检查 PM1_CNT 中的 SCI_EN 位（bit 0）
    // 如果 SCI_EN=1，则 ACPI 已经启用
    if (acpi_info.pm1a_cnt_blk) {
        uint16_t pm1_cnt = inw(acpi_info.pm1a_cnt_blk);
        if (pm1_cnt & 0x01) {
            LOG_INFO_MSG("ACPI: SCI_EN is set, ACPI already enabled\n");
            return 0;
        }
    }
    
    // 发送 ACPI_ENABLE 命令到 SMI_CMD 端口
    LOG_INFO_MSG("ACPI: Enabling ACPI mode (SMI_CMD=0x%x, ACPI_ENABLE=0x%x)\n",
                 acpi_info.fadt->smi_cmd, acpi_info.fadt->acpi_enable);
    
    outb(acpi_info.fadt->smi_cmd, acpi_info.fadt->acpi_enable);
    
    // 等待 SCI_EN 位被设置（最多等待 3 秒）
    for (int i = 0; i < 300; i++) {
        if (acpi_info.pm1a_cnt_blk) {
            uint16_t pm1_cnt = inw(acpi_info.pm1a_cnt_blk);
            if (pm1_cnt & 0x01) {
                LOG_INFO_MSG("ACPI: ACPI mode enabled successfully\n");
                return 0;
            }
        }
        
        // 简单延时（大约 10ms）
        for (volatile int j = 0; j < 100000; j++) {
            __asm__ volatile ("nop");
        }
    }
    
    LOG_WARN_MSG("ACPI: Timeout waiting for ACPI enable\n");
    return -1;
}

int acpi_poweroff(void) {
    if (!acpi_info.initialized) {
        LOG_ERROR_MSG("ACPI: Not initialized, cannot power off\n");
        return -1;
    }
    
    if (!acpi_info.pm1a_cnt_blk) {
        LOG_ERROR_MSG("ACPI: PM1a_CNT_BLK is 0, cannot power off\n");
        return -1;
    }
    
    if (!acpi_info.s5_valid) {
        LOG_WARN_MSG("ACPI: S5 values not valid, trying default\n");
        acpi_info.slp_typa = 0x05;
        acpi_info.slp_typb = 0x05;
    }
    
    LOG_INFO_MSG("ACPI: Initiating S5 (soft off) shutdown...\n");
    LOG_INFO_MSG("ACPI: SLP_TYPa=0x%x, SLP_TYPb=0x%x\n", 
                 acpi_info.slp_typa, acpi_info.slp_typb);
    
    // 禁用中断
    __asm__ volatile ("cli");
    
    // 构建 PM1_CNT 值
    // PM1_CNT 寄存器格式：
    // Bit 12-10: SLP_TYP (睡眠类型)
    // Bit 13: SLP_EN (睡眠使能)
    uint16_t slp_typa_value = (acpi_info.slp_typa << 10) | ACPI_SLP_EN;
    uint16_t slp_typb_value = (acpi_info.slp_typb << 10) | ACPI_SLP_EN;
    
    LOG_DEBUG_MSG("ACPI: Writing 0x%x to PM1a_CNT (0x%x)\n", 
                  slp_typa_value, acpi_info.pm1a_cnt_blk);
    
    // 写入 PM1a_CNT_BLK
    outw(acpi_info.pm1a_cnt_blk, slp_typa_value);
    
    // 如果存在 PM1b_CNT_BLK，也写入
    if (acpi_info.pm1b_cnt_blk) {
        LOG_DEBUG_MSG("ACPI: Writing 0x%x to PM1b_CNT (0x%x)\n", 
                      slp_typb_value, acpi_info.pm1b_cnt_blk);
        outw(acpi_info.pm1b_cnt_blk, slp_typb_value);
    }
    
    // 等待关机（不应该到达这里）
    LOG_INFO_MSG("ACPI: Waiting for power off...\n");
    
    // 如果还没关机，尝试其他 SLP_TYP 值
    for (uint16_t slp_typ = 0; slp_typ < 8; slp_typ++) {
        uint16_t value = (slp_typ << 10) | ACPI_SLP_EN;
        outw(acpi_info.pm1a_cnt_blk, value);
        
        if (acpi_info.pm1b_cnt_blk) {
            outw(acpi_info.pm1b_cnt_blk, value);
        }
        
        // 短暂等待
        for (volatile int i = 0; i < 1000000; i++) {
            __asm__ volatile ("nop");
        }
    }
    
    // 如果所有尝试都失败，返回错误
    LOG_ERROR_MSG("ACPI: Power off failed\n");
    return -1;
}

int acpi_reset(void) {
    if (!acpi_info.initialized || !acpi_info.fadt) {
        return -1;
    }
    
    // 检查 FADT 版本是否支持 reset_reg
    if (acpi_info.fadt->header.length < 129) {
        LOG_WARN_MSG("ACPI: FADT too short for reset register\n");
        return -1;
    }
    
    // 检查 RESET_REG 是否有效
    acpi_generic_address_t *reset_reg = &acpi_info.fadt->reset_reg;
    if (reset_reg->address == 0) {
        LOG_WARN_MSG("ACPI: Reset register address is 0\n");
        return -1;
    }
    
    LOG_INFO_MSG("ACPI: Initiating reset via ACPI...\n");
    LOG_INFO_MSG("ACPI: Reset register: space=%u, addr=0x%x, value=0x%x\n",
                 reset_reg->address_space, (uint32_t)reset_reg->address,
                 acpi_info.fadt->reset_value);
    
    __asm__ volatile ("cli");
    
    // 根据地址空间类型执行重置
    switch (reset_reg->address_space) {
        case 0x00:  // System Memory
            *(volatile uint8_t *)PHYS_TO_VIRT((uint32_t)reset_reg->address) = 
                acpi_info.fadt->reset_value;
            break;
        
        case 0x01:  // System I/O
            outb((uint16_t)reset_reg->address, acpi_info.fadt->reset_value);
            break;
        
        case 0x02:  // PCI Configuration Space
            // 需要 PCI 配置空间访问，暂不实现
            LOG_WARN_MSG("ACPI: PCI config space reset not implemented\n");
            return -1;
        
        default:
            LOG_WARN_MSG("ACPI: Unknown reset register address space\n");
            return -1;
    }
    
    // 等待重置
    for (volatile int i = 0; i < 10000000; i++) {
        __asm__ volatile ("nop");
    }
    
    return -1;  // 如果到这里说明重置失败
}

void acpi_print_info(void) {
    kprintf("\n=============================== ACPI Info ==================================\n");
    
    if (!acpi_info.initialized) {
        kprintf("ACPI: Not initialized\n");
        kprintf("================================================================================\n\n");
        return;
    }
    
    kprintf("ACPI Revision:    %u (%s)\n", 
            acpi_info.revision,
            acpi_info.revision >= 2 ? "2.0+" : "1.0");
    
    if (acpi_info.rsdp) {
        char oem_id[7] = {0};
        memcpy(oem_id, acpi_info.rsdp->oem_id, 6);
        kprintf("OEM ID:           '%s'\n", oem_id);
    }
    
    kprintf("PM1a_CNT_BLK:     0x%04x\n", acpi_info.pm1a_cnt_blk);
    kprintf("PM1b_CNT_BLK:     0x%04x\n", acpi_info.pm1b_cnt_blk);
    kprintf("PM1_CNT_LEN:      %u bytes\n", acpi_info.pm1_cnt_len);
    kprintf("SCI Interrupt:    IRQ %u\n", acpi_info.sci_int);
    
    if (acpi_info.s5_valid) {
        kprintf("S5 SLP_TYPa:      0x%x\n", acpi_info.slp_typa);
        kprintf("S5 SLP_TYPb:      0x%x\n", acpi_info.slp_typb);
    } else {
        kprintf("S5 State:         Not available\n");
    }
    
    if (acpi_info.fadt) {
        kprintf("SMI_CMD:          0x%04x\n", acpi_info.fadt->smi_cmd);
        kprintf("ACPI_ENABLE:      0x%02x\n", acpi_info.fadt->acpi_enable);
        kprintf("ACPI_DISABLE:     0x%02x\n", acpi_info.fadt->acpi_disable);
    }
    
    kprintf("================================================================================\n\n");
}

