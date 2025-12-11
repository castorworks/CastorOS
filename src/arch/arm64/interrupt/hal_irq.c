/**
 * @file hal_irq.c
 * @brief HAL 逻辑中断号实现 (ARM64)
 * 
 * 实现 ARM64 架构的逻辑中断号到 GIC IRQ 号的映射。
 * 
 * @see Requirements 5.1
 */

#include <hal/hal_irq.h>
#include "gic.h"

/* Forward declaration for serial output */
extern void serial_puts(const char *str);
extern void serial_put_hex64(uint64_t value);

/* ============================================================================
 * ARM64 GIC IRQ Mapping
 * 
 * ARM64 使用 GIC (Generic Interrupt Controller)
 * 
 * 中断号范围:
 * - SGI (Software Generated Interrupts): 0-15
 * - PPI (Private Peripheral Interrupts): 16-31
 * - SPI (Shared Peripheral Interrupts): 32+
 * 
 * QEMU virt machine 常用中断号:
 * - 27: Virtual Timer (PPI)
 * - 30: Physical Timer (PPI)
 * - 33: UART0 (SPI)
 * - 35: RTC (SPI)
 * - 47: Virtio (SPI)
 * ========================================================================== */

/**
 * @brief 逻辑 IRQ 到 GIC IRQ 的映射表
 * 
 * 索引为 hal_irq_type_t，值为 GIC 中断号
 * -1 表示该逻辑 IRQ 在此架构上不可用
 * 
 * 注意: ARM64 没有传统的键盘/鼠标中断，这些设备通常通过 USB 或其他总线连接
 */
static const int16_t irq_mapping[HAL_IRQ_MAX] = {
    [HAL_IRQ_TIMER]          = GIC_INTID_VTIMER,  /* Virtual Timer PPI (27) */
    [HAL_IRQ_KEYBOARD]       = -1,                /* 不可用 - ARM64 无 PS/2 */
    [HAL_IRQ_SERIAL0]        = GIC_INTID_UART0,   /* UART0 SPI (33) */
    [HAL_IRQ_SERIAL1]        = 34,                /* UART1 SPI (34) - QEMU virt */
    [HAL_IRQ_DISK_PRIMARY]   = 48,                /* Virtio Block (SPI) */
    [HAL_IRQ_DISK_SECONDARY] = -1,                /* 不可用 */
    [HAL_IRQ_NETWORK]        = 47,                /* Virtio Net (SPI) */
    [HAL_IRQ_USB]            = -1,                /* 需要具体 USB 控制器配置 */
    [HAL_IRQ_RTC]            = 35,                /* RTC SPI */
    [HAL_IRQ_MOUSE]          = -1,                /* 不可用 - ARM64 无 PS/2 */
};

/**
 * @brief 逻辑 IRQ 类型名称
 */
static const char *irq_type_names[HAL_IRQ_MAX] = {
    [HAL_IRQ_TIMER]          = "Timer",
    [HAL_IRQ_KEYBOARD]       = "Keyboard",
    [HAL_IRQ_SERIAL0]        = "Serial0",
    [HAL_IRQ_SERIAL1]        = "Serial1",
    [HAL_IRQ_DISK_PRIMARY]   = "Disk Primary",
    [HAL_IRQ_DISK_SECONDARY] = "Disk Secondary",
    [HAL_IRQ_NETWORK]        = "Network",
    [HAL_IRQ_USB]            = "USB",
    [HAL_IRQ_RTC]            = "RTC",
    [HAL_IRQ_MOUSE]          = "Mouse",
};

/* ============================================================================
 * Handler Storage
 * 
 * ARM64 GIC 已经使用 hal_interrupt_handler_t 签名，
 * 所以不需要像 x86 那样的包装层
 * ========================================================================== */

/** 存储 HAL 风格的处理函数和数据 */
typedef struct {
    hal_interrupt_handler_t handler;
    void *data;
} hal_irq_entry_t;

static hal_irq_entry_t hal_irq_handlers[HAL_IRQ_MAX];

/* ============================================================================
 * Public API Implementation
 * ========================================================================== */

/**
 * @brief 获取逻辑中断对应的物理 IRQ 号
 */
int32_t hal_irq_get_number(hal_irq_type_t type, uint32_t instance) {
    /* 目前 ARM64 不支持多实例设备 */
    (void)instance;
    
    if (type >= HAL_IRQ_MAX) {
        return -1;
    }
    
    return irq_mapping[type];
}

/**
 * @brief 注册逻辑中断处理程序
 */
hal_error_t hal_irq_register_logical(hal_irq_type_t type, uint32_t instance,
                                      hal_interrupt_handler_t handler, void *data) {
    (void)instance;  /* ARM64 不支持多实例 */
    
    if (type >= HAL_IRQ_MAX) {
        return HAL_ERR_INVALID_PARAM;
    }
    
    int32_t phys_irq = irq_mapping[type];
    if (phys_irq < 0) {
        return HAL_ERR_NOT_SUPPORTED;
    }
    
    /* 保存 HAL 风格的处理函数 */
    hal_irq_handlers[type].handler = handler;
    hal_irq_handlers[type].data = data;
    
    /* 直接注册到 GIC */
    gic_register_handler((uint32_t)phys_irq, handler, data);
    
    /* 启用该中断 */
    gic_enable_irq((uint32_t)phys_irq);
    
    serial_puts("HAL IRQ: Registered ");
    serial_puts(irq_type_names[type]);
    serial_puts(" handler on GIC IRQ ");
    serial_put_hex64((uint64_t)phys_irq);
    serial_puts("\n");
    
    return HAL_OK;
}

/**
 * @brief 注销逻辑中断处理程序
 */
hal_error_t hal_irq_unregister_logical(hal_irq_type_t type, uint32_t instance) {
    (void)instance;
    
    if (type >= HAL_IRQ_MAX) {
        return HAL_ERR_INVALID_PARAM;
    }
    
    int32_t phys_irq = irq_mapping[type];
    if (phys_irq < 0) {
        return HAL_ERR_NOT_SUPPORTED;
    }
    
    /* 清除 HAL 处理函数 */
    hal_irq_handlers[type].handler = NULL;
    hal_irq_handlers[type].data = NULL;
    
    /* 禁用并注销 GIC 中断 */
    gic_disable_irq((uint32_t)phys_irq);
    gic_unregister_handler((uint32_t)phys_irq);
    
    return HAL_OK;
}

/**
 * @brief 启用逻辑中断
 */
hal_error_t hal_irq_enable_logical(hal_irq_type_t type, uint32_t instance) {
    (void)instance;
    
    if (type >= HAL_IRQ_MAX) {
        return HAL_ERR_INVALID_PARAM;
    }
    
    int32_t phys_irq = irq_mapping[type];
    if (phys_irq < 0) {
        return HAL_ERR_NOT_SUPPORTED;
    }
    
    gic_enable_irq((uint32_t)phys_irq);
    return HAL_OK;
}

/**
 * @brief 禁用逻辑中断
 */
hal_error_t hal_irq_disable_logical(hal_irq_type_t type, uint32_t instance) {
    (void)instance;
    
    if (type >= HAL_IRQ_MAX) {
        return HAL_ERR_INVALID_PARAM;
    }
    
    int32_t phys_irq = irq_mapping[type];
    if (phys_irq < 0) {
        return HAL_ERR_NOT_SUPPORTED;
    }
    
    gic_disable_irq((uint32_t)phys_irq);
    return HAL_OK;
}

/**
 * @brief 检查逻辑中断类型是否可用
 */
bool hal_irq_is_available(hal_irq_type_t type) {
    if (type >= HAL_IRQ_MAX) {
        return false;
    }
    return irq_mapping[type] >= 0;
}

/**
 * @brief 获取逻辑中断类型名称
 */
const char *hal_irq_type_name(hal_irq_type_t type) {
    if (type >= HAL_IRQ_MAX) {
        return "Unknown";
    }
    return irq_type_names[type];
}
