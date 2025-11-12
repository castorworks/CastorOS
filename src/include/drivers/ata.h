#ifndef _DRIVERS_ATA_H_
#define _DRIVERS_ATA_H_

/**
 * ATA 驱动（PIO 模式）
 *
 * 提供对标准 IDE/ATA 硬盘的读写支持，并将其注册为 blockdev 设备。
 */

/**
 * 初始化 ATA 控制器（当前仅支持主通道主设备）
 */
void ata_init(void);

#endif /* _DRIVERS_ATA_H_ */


