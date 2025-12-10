#ifndef _DRIVERS_X86_ATA_H_
#define _DRIVERS_X86_ATA_H_

/**
 * ATA 驱动（PIO 模式）
 *
 * 提供对标准 IDE/ATA 硬盘的读写支持，并将其注册为 blockdev 设备。
 * 支持最多 4 个设备：
 *   - ata0: 主通道主设备 (primary master)
 *   - ata1: 主通道从设备 (primary slave)
 *   - ata2: 次通道主设备 (secondary master)
 *   - ata3: 次通道从设备 (secondary slave)
 */

/**
 * 初始化 ATA 控制器，检测并注册所有可用的 ATA 设备
 */
void ata_init(void);

#endif /* _DRIVERS_X86_ATA_H_ */


