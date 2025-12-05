/**
 * @file sys/ioctl.h
 * @brief ioctl 定义
 */

#ifndef _SYS_IOCTL_H_
#define _SYS_IOCTL_H_

#include <types.h>

// ============================================================================
// ioctl 函数
// ============================================================================

/**
 * @brief 设备控制
 * @param fd 文件描述符
 * @param request 请求码
 * @param ... 可选参数
 * @return 成功返回 0 或正值，-1 失败
 */
int ioctl(int fd, unsigned long request, ...);

// ============================================================================
// 终端 ioctl
// ============================================================================

#define TCGETS      0x5401      // 获取终端属性
#define TCSETS      0x5402      // 设置终端属性
#define TIOCGWINSZ  0x5413      // 获取窗口大小
#define TIOCSWINSZ  0x5414      // 设置窗口大小

/**
 * @brief 窗口大小结构
 */
struct winsize {
    uint16_t ws_row;        // 行数
    uint16_t ws_col;        // 列数
    uint16_t ws_xpixel;     // 像素宽度（未使用）
    uint16_t ws_ypixel;     // 像素高度（未使用）
};

#endif // _SYS_IOCTL_H_

