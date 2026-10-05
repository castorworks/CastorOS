/**
 * @file hal_irq.h
 * @brief HAL 逻辑中断号抽象接口
 * 
 * 提供架构无关的逻辑中断号接口，使驱动代码可以使用统一的中断类型
 * 而不需要知道底层中断控制器的具体实现（PIC、APIC、GIC 等）。
 * 
 * @see Requirements 5.1, 5.4
 */

#ifndef _HAL_HAL_IRQ_H_
#define _HAL_HAL_IRQ_H_

#include <types.h>
#include <hal/hal.h>
#include <hal/hal_error.h>

/* ============================================================================
 * Logical IRQ Types
 * ========================================================================== */

/**
 * @brief 逻辑中断类型枚举
 * 
 * 定义架构无关的逻辑中断类型，驱动程序使用这些类型
 * 而不是直接使用架构特定的 IRQ 号。
 * 
 * @see Requirements 5.1
 */
typedef enum hal_irq_type {
    HAL_IRQ_TIMER = 0,          /**< 系统定时器中断 */
    HAL_IRQ_KEYBOARD,           /**< 键盘中断 */
    HAL_IRQ_SERIAL0,            /**< 串口 0 (COM1) 中断 */
    HAL_IRQ_SERIAL1,            /**< 串口 1 (COM2) 中断 */
    HAL_IRQ_DISK_PRIMARY,       /**< 主磁盘控制器中断 */
    HAL_IRQ_DISK_SECONDARY,     /**< 从磁盘控制器中断 */
    HAL_IRQ_NETWORK,            /**< 网络设备中断 */
    HAL_IRQ_USB,                /**< USB 控制器中断 */
    HAL_IRQ_RTC,                /**< 实时时钟中断 */
    HAL_IRQ_MOUSE,              /**< PS/2 鼠标中断 */
    HAL_IRQ_MAX                 /**< 逻辑 IRQ 类型数量 */
} hal_irq_type_t;

namespace hal {

/**
 * @brief 逻辑 IRQ 管理
 */
class Irq {
public:
    /* ============================================================================
     * IRQ Mapping Functions
     * ========================================================================== */

};

} // namespace hal

#endif /* _HAL_HAL_IRQ_H_ */
