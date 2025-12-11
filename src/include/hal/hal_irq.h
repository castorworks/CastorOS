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

/* ============================================================================
 * IRQ Mapping Functions
 * ========================================================================== */

/**
 * @brief 获取逻辑中断对应的物理 IRQ 号
 * 
 * 将逻辑中断类型映射到当前架构的物理 IRQ 号。
 * 
 * @param type 逻辑中断类型
 * @param instance 设备实例号（多个同类设备时使用，通常为 0）
 * @return 物理 IRQ 号，如果不支持返回 -1
 * 
 * @note 不同架构返回的物理 IRQ 号可能不同：
 *       - i686/x86_64: PIC/APIC 中断向量号
 *       - ARM64: GIC 中断号
 * 
 * @see Requirements 5.1
 */
int32_t hal_irq_get_number(hal_irq_type_t type, uint32_t instance);

/**
 * @brief 注册逻辑中断处理程序
 * 
 * 使用逻辑中断类型注册中断处理程序，自动映射到物理 IRQ。
 * 
 * @param type 逻辑中断类型
 * @param instance 设备实例号
 * @param handler 中断处理函数
 * @param data 传递给处理函数的用户数据
 * @return HAL_OK 成功，其他为错误码
 * 
 * @see Requirements 5.4
 */
hal_error_t hal_irq_register_logical(hal_irq_type_t type, uint32_t instance,
                                      hal_interrupt_handler_t handler, void *data);

/**
 * @brief 注销逻辑中断处理程序
 * 
 * @param type 逻辑中断类型
 * @param instance 设备实例号
 * @return HAL_OK 成功，其他为错误码
 */
hal_error_t hal_irq_unregister_logical(hal_irq_type_t type, uint32_t instance);

/**
 * @brief 启用逻辑中断
 * 
 * @param type 逻辑中断类型
 * @param instance 设备实例号
 * @return HAL_OK 成功，其他为错误码
 */
hal_error_t hal_irq_enable_logical(hal_irq_type_t type, uint32_t instance);

/**
 * @brief 禁用逻辑中断
 * 
 * @param type 逻辑中断类型
 * @param instance 设备实例号
 * @return HAL_OK 成功，其他为错误码
 */
hal_error_t hal_irq_disable_logical(hal_irq_type_t type, uint32_t instance);

/**
 * @brief 检查逻辑中断类型是否在当前架构上可用
 * 
 * @param type 逻辑中断类型
 * @return true 如果可用，false 如果不支持
 * 
 * @see Requirements 5.4
 */
bool hal_irq_is_available(hal_irq_type_t type);

/**
 * @brief 获取逻辑中断类型的名称字符串
 * 
 * @param type 逻辑中断类型
 * @return 中断类型名称字符串
 */
const char *hal_irq_type_name(hal_irq_type_t type);

#endif /* _HAL_HAL_IRQ_H_ */
