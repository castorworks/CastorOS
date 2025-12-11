/**
 * @file hal_irq.c
 * @brief HAL 逻辑中断号实现 (i686)
 * 
 * 实现 i686 架构的逻辑中断号到物理 PIC IRQ 号的映射。
 * 
 * @see Requirements 5.1
 */

#include <hal/hal_irq.h>
#include <kernel/irq.h>
#include <lib/klog.h>

/* ============================================================================
 * i686 PIC IRQ Mapping
 * 
 * i686 使用 8259 PIC，IRQ 0-15 映射到中断向量 32-47
 * 
 * IRQ 0:  Timer (PIT)
 * IRQ 1:  Keyboard
 * IRQ 2:  Cascade (slave PIC)
 * IRQ 3:  COM2/COM4
 * IRQ 4:  COM1/COM3
 * IRQ 5:  LPT2
 * IRQ 6:  Floppy
 * IRQ 7:  LPT1 / Spurious
 * IRQ 8:  RTC
 * IRQ 9:  Free (ACPI)
 * IRQ 10: Free
 * IRQ 11: Free (often used for PCI devices)
 * IRQ 12: PS/2 Mouse
 * IRQ 13: FPU
 * IRQ 14: Primary ATA
 * IRQ 15: Secondary ATA
 * ========================================================================== */

/**
 * @brief 逻辑 IRQ 到物理 IRQ 的映射表
 * 
 * 索引为 hal_irq_type_t，值为 PIC IRQ 号 (0-15)
 * -1 表示该逻辑 IRQ 在此架构上不可用
 */
static const int8_t irq_mapping[HAL_IRQ_MAX] = {
    [HAL_IRQ_TIMER]          = 0,   /* IRQ 0: PIT Timer */
    [HAL_IRQ_KEYBOARD]       = 1,   /* IRQ 1: Keyboard */
    [HAL_IRQ_SERIAL0]        = 4,   /* IRQ 4: COM1 */
    [HAL_IRQ_SERIAL1]        = 3,   /* IRQ 3: COM2 */
    [HAL_IRQ_DISK_PRIMARY]   = 14,  /* IRQ 14: Primary ATA */
    [HAL_IRQ_DISK_SECONDARY] = 15,  /* IRQ 15: Secondary ATA */
    [HAL_IRQ_NETWORK]        = 11,  /* IRQ 11: Common PCI slot (E1000 etc.) */
    [HAL_IRQ_USB]            = 11,  /* IRQ 11: Common PCI slot (UHCI etc.) */
    [HAL_IRQ_RTC]            = 8,   /* IRQ 8: RTC */
    [HAL_IRQ_MOUSE]          = 12,  /* IRQ 12: PS/2 Mouse */
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
 * Handler Wrapper
 * 
 * i686 的 IRQ 处理函数签名是 void handler(registers_t *regs)
 * 而 HAL 的签名是 void handler(void *data)
 * 需要一个包装层来转换
 * ========================================================================== */

/** 存储 HAL 风格的处理函数和数据 */
typedef struct {
    hal_interrupt_handler_t handler;
    void *data;
} hal_irq_entry_t;

static hal_irq_entry_t hal_irq_handlers[HAL_IRQ_MAX];

/**
 * @brief IRQ 处理函数包装器
 * 
 * 将 i686 的 registers_t* 调用转换为 HAL 的 void* 调用
 */
static void irq_wrapper_timer(registers_t *regs) {
    (void)regs;
    if (hal_irq_handlers[HAL_IRQ_TIMER].handler) {
        hal_irq_handlers[HAL_IRQ_TIMER].handler(hal_irq_handlers[HAL_IRQ_TIMER].data);
    }
}

static void irq_wrapper_keyboard(registers_t *regs) {
    (void)regs;
    if (hal_irq_handlers[HAL_IRQ_KEYBOARD].handler) {
        hal_irq_handlers[HAL_IRQ_KEYBOARD].handler(hal_irq_handlers[HAL_IRQ_KEYBOARD].data);
    }
}

static void irq_wrapper_serial0(registers_t *regs) {
    (void)regs;
    if (hal_irq_handlers[HAL_IRQ_SERIAL0].handler) {
        hal_irq_handlers[HAL_IRQ_SERIAL0].handler(hal_irq_handlers[HAL_IRQ_SERIAL0].data);
    }
}

static void irq_wrapper_serial1(registers_t *regs) {
    (void)regs;
    if (hal_irq_handlers[HAL_IRQ_SERIAL1].handler) {
        hal_irq_handlers[HAL_IRQ_SERIAL1].handler(hal_irq_handlers[HAL_IRQ_SERIAL1].data);
    }
}

static void irq_wrapper_disk_primary(registers_t *regs) {
    (void)regs;
    if (hal_irq_handlers[HAL_IRQ_DISK_PRIMARY].handler) {
        hal_irq_handlers[HAL_IRQ_DISK_PRIMARY].handler(hal_irq_handlers[HAL_IRQ_DISK_PRIMARY].data);
    }
}

static void irq_wrapper_disk_secondary(registers_t *regs) {
    (void)regs;
    if (hal_irq_handlers[HAL_IRQ_DISK_SECONDARY].handler) {
        hal_irq_handlers[HAL_IRQ_DISK_SECONDARY].handler(hal_irq_handlers[HAL_IRQ_DISK_SECONDARY].data);
    }
}

static void irq_wrapper_network(registers_t *regs) {
    (void)regs;
    if (hal_irq_handlers[HAL_IRQ_NETWORK].handler) {
        hal_irq_handlers[HAL_IRQ_NETWORK].handler(hal_irq_handlers[HAL_IRQ_NETWORK].data);
    }
}

static void irq_wrapper_usb(registers_t *regs) {
    (void)regs;
    if (hal_irq_handlers[HAL_IRQ_USB].handler) {
        hal_irq_handlers[HAL_IRQ_USB].handler(hal_irq_handlers[HAL_IRQ_USB].data);
    }
}

static void irq_wrapper_rtc(registers_t *regs) {
    (void)regs;
    if (hal_irq_handlers[HAL_IRQ_RTC].handler) {
        hal_irq_handlers[HAL_IRQ_RTC].handler(hal_irq_handlers[HAL_IRQ_RTC].data);
    }
}

static void irq_wrapper_mouse(registers_t *regs) {
    (void)regs;
    if (hal_irq_handlers[HAL_IRQ_MOUSE].handler) {
        hal_irq_handlers[HAL_IRQ_MOUSE].handler(hal_irq_handlers[HAL_IRQ_MOUSE].data);
    }
}

/**
 * @brief 获取逻辑 IRQ 类型对应的包装函数
 */
static isr_handler_t get_wrapper_for_type(hal_irq_type_t type) {
    switch (type) {
        case HAL_IRQ_TIMER:          return irq_wrapper_timer;
        case HAL_IRQ_KEYBOARD:       return irq_wrapper_keyboard;
        case HAL_IRQ_SERIAL0:        return irq_wrapper_serial0;
        case HAL_IRQ_SERIAL1:        return irq_wrapper_serial1;
        case HAL_IRQ_DISK_PRIMARY:   return irq_wrapper_disk_primary;
        case HAL_IRQ_DISK_SECONDARY: return irq_wrapper_disk_secondary;
        case HAL_IRQ_NETWORK:        return irq_wrapper_network;
        case HAL_IRQ_USB:            return irq_wrapper_usb;
        case HAL_IRQ_RTC:            return irq_wrapper_rtc;
        case HAL_IRQ_MOUSE:          return irq_wrapper_mouse;
        default:                     return NULL;
    }
}

/* ============================================================================
 * Public API Implementation
 * ========================================================================== */

/**
 * @brief 获取逻辑中断对应的物理 IRQ 号
 */
int32_t hal_irq_get_number(hal_irq_type_t type, uint32_t instance) {
    /* 目前 i686 不支持多实例设备 */
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
    (void)instance;  /* i686 不支持多实例 */
    
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
    
    /* 获取包装函数并注册到底层 IRQ 系统 */
    isr_handler_t wrapper = get_wrapper_for_type(type);
    if (wrapper == NULL) {
        return HAL_ERR_NOT_SUPPORTED;
    }
    
    irq_register_handler((uint8_t)phys_irq, wrapper);
    
    LOG_DEBUG_MSG("HAL IRQ: Registered %s handler on IRQ %d\n", 
                  irq_type_names[type], phys_irq);
    
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
    
    /* 注销底层 IRQ 处理函数 */
    irq_register_handler((uint8_t)phys_irq, NULL);
    
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
    
    irq_enable_line((uint8_t)phys_irq);
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
    
    irq_disable_line((uint8_t)phys_irq);
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
