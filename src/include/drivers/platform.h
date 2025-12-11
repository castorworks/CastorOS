/**
 * @file platform.h
 * @brief 平台设备模型接口
 * 
 * 提供统一的平台设备抽象，使驱动程序可以同时支持 PCI 枚举（x86）
 * 和设备树发现（ARM64）两种设备发现机制。
 * 
 * 设计目标：
 * - 驱动程序通过 platform_device 接口访问设备资源
 * - 无需关心设备是通过 PCI 还是 DTB 发现的
 * - 统一的资源访问 API（MMIO、IRQ、DMA）
 * 
 * @see Requirements 6.1, 6.2, 6.3, 6.4
 */

#ifndef _DRIVERS_PLATFORM_H_
#define _DRIVERS_PLATFORM_H_

#include <types.h>
#include <hal/hal_error.h>

/* ============================================================================
 * 常量定义
 * ============================================================================ */

/** 平台设备名称最大长度 */
#define PLATFORM_NAME_MAX       32

/** 每个设备最大资源数 */
#define PLATFORM_MAX_RESOURCES  8

/** 最大注册驱动数 */
#define PLATFORM_MAX_DRIVERS    16

/** 最大平台设备数 */
#define PLATFORM_MAX_DEVICES    32

/** PCI ID 列表结束标记 */
#define PCI_ID_END              0xFFFF

/** Compatible 字符串列表结束标记 */
#define COMPATIBLE_END          NULL

/* ============================================================================
 * 资源类型定义
 * ============================================================================ */

/**
 * @brief 平台设备资源类型
 */
typedef enum platform_res_type {
    PLATFORM_RES_MEM = 0,   /**< 内存映射区域 (MMIO) */
    PLATFORM_RES_IRQ,       /**< 中断资源 */
    PLATFORM_RES_DMA,       /**< DMA 通道 */
    PLATFORM_RES_IO,        /**< I/O 端口（仅 x86） */
} platform_res_type_t;

/**
 * @brief 资源标志
 */
typedef enum platform_res_flags {
    PLATFORM_RES_FLAG_NONE       = 0,
    PLATFORM_RES_FLAG_PREFETCH   = (1 << 0),  /**< 可预取内存 */
    PLATFORM_RES_FLAG_READONLY   = (1 << 1),  /**< 只读资源 */
    PLATFORM_RES_FLAG_SHARED     = (1 << 2),  /**< 共享中断 */
    PLATFORM_RES_FLAG_64BIT      = (1 << 3),  /**< 64 位地址 */
} platform_res_flags_t;

/* ============================================================================
 * 设备发现来源
 * ============================================================================ */

/**
 * @brief 设备发现来源
 */
typedef enum platform_source {
    PLATFORM_SRC_UNKNOWN = 0,   /**< 未知来源 */
    PLATFORM_SRC_PCI,           /**< PCI 枚举发现 */
    PLATFORM_SRC_DTB,           /**< 设备树发现 */
    PLATFORM_SRC_ACPI,          /**< ACPI 发现 */
    PLATFORM_SRC_MANUAL,        /**< 手动注册 */
} platform_source_t;

/* ============================================================================
 * 数据结构定义
 * ============================================================================ */

/**
 * @brief 平台设备资源
 * 
 * 描述设备的一个资源（内存区域、中断等）
 */
typedef struct platform_resource {
    platform_res_type_t type;   /**< 资源类型 */
    uint64_t start;             /**< 起始地址/IRQ 号/DMA 通道 */
    uint64_t end;               /**< 结束地址（对于内存区域） */
    uint32_t flags;             /**< 资源标志 */
    const char *name;           /**< 资源名称（可选） */
} platform_resource_t;

/**
 * @brief PCI 设备标识信息
 */
typedef struct platform_pci_info {
    uint16_t vendor_id;         /**< PCI 厂商 ID */
    uint16_t device_id;         /**< PCI 设备 ID */
    uint16_t subsys_vendor_id;  /**< 子系统厂商 ID */
    uint16_t subsys_device_id;  /**< 子系统设备 ID */
    uint8_t bus;                /**< 总线号 */
    uint8_t slot;               /**< 插槽号 */
    uint8_t func;               /**< 功能号 */
    uint8_t class_code;         /**< 类别代码 */
    uint8_t subclass;           /**< 子类别 */
    uint8_t prog_if;            /**< 编程接口 */
} platform_pci_info_t;

/**
 * @brief DTB 设备标识信息
 */
typedef struct platform_dtb_info {
    const char *compatible;     /**< compatible 字符串 */
    const char *node_name;      /**< 设备树节点名称 */
    uint32_t phandle;           /**< 设备树 phandle */
} platform_dtb_info_t;

/* 前向声明 */
struct platform_driver;

/**
 * @brief 平台设备结构
 * 
 * 表示一个平台设备，包含设备资源和标识信息
 */
typedef struct platform_device {
    /* 基本信息 */
    char name[PLATFORM_NAME_MAX];   /**< 设备名称 */
    uint32_t id;                    /**< 设备实例 ID */
    bool in_use;                    /**< 设备是否在使用中 */
    
    /* 设备资源 */
    platform_resource_t resources[PLATFORM_MAX_RESOURCES];
    uint32_t num_resources;         /**< 资源数量 */
    
    /* 设备发现来源 */
    platform_source_t source;       /**< 发现来源 */
    
    /* 来源特定信息 */
    union {
        platform_pci_info_t pci;    /**< PCI 信息（如果来自 PCI） */
        platform_dtb_info_t dtb;    /**< DTB 信息（如果来自设备树） */
    };
    
    /* 驱动关联 */
    struct platform_driver *driver; /**< 绑定的驱动程序 */
    void *driver_data;              /**< 驱动私有数据 */
    
    /* 设备状态 */
    bool probed;                    /**< 是否已探测 */
    bool enabled;                   /**< 是否已启用 */
} platform_device_t;

/**
 * @brief PCI ID 匹配表项
 */
typedef struct platform_pci_id {
    uint16_t vendor_id;             /**< 厂商 ID（PCI_ID_END 表示结束） */
    uint16_t device_id;             /**< 设备 ID */
} platform_pci_id_t;

/**
 * @brief 平台驱动结构
 * 
 * 描述一个平台驱动程序
 */
typedef struct platform_driver {
    /* 驱动信息 */
    const char *name;               /**< 驱动名称 */
    bool in_use;                    /**< 驱动是否已注册 */
    
    /* 匹配信息 */
    const platform_pci_id_t *pci_ids;   /**< PCI ID 匹配表（以 PCI_ID_END 结尾） */
    const char **compatible;            /**< DTB compatible 字符串数组（以 NULL 结尾） */
    
    /* 驱动回调 */
    int (*probe)(platform_device_t *dev);   /**< 设备探测回调 */
    void (*remove)(platform_device_t *dev); /**< 设备移除回调 */
    int (*suspend)(platform_device_t *dev); /**< 设备挂起回调（可选） */
    int (*resume)(platform_device_t *dev);  /**< 设备恢复回调（可选） */
} platform_driver_t;

/* ============================================================================
 * 驱动注册 API
 * ============================================================================ */

/**
 * @brief 注册平台驱动
 * 
 * @param drv 驱动结构指针
 * @return HAL_OK 成功，其他为错误码
 * 
 * 注册驱动后，框架会自动尝试将其与已发现的设备匹配。
 */
hal_error_t platform_driver_register(platform_driver_t *drv);

/**
 * @brief 注销平台驱动
 * 
 * @param drv 驱动结构指针
 * @return HAL_OK 成功，其他为错误码
 * 
 * 注销前会调用驱动的 remove 回调移除所有绑定的设备。
 */
hal_error_t platform_driver_unregister(platform_driver_t *drv);

/* ============================================================================
 * 设备注册 API
 * ============================================================================ */

/**
 * @brief 注册平台设备
 * 
 * @param dev 设备结构指针
 * @return HAL_OK 成功，其他为错误码
 * 
 * 注册设备后，框架会自动尝试将其与已注册的驱动匹配。
 */
hal_error_t platform_device_register(platform_device_t *dev);

/**
 * @brief 注销平台设备
 * 
 * @param dev 设备结构指针
 * @return HAL_OK 成功，其他为错误码
 */
hal_error_t platform_device_unregister(platform_device_t *dev);

/**
 * @brief 分配一个新的平台设备结构
 * 
 * @param name 设备名称
 * @param id 设备实例 ID（-1 表示自动分配）
 * @return 设备指针，失败返回 NULL
 */
platform_device_t *platform_device_alloc(const char *name, int id);

/**
 * @brief 释放平台设备结构
 * 
 * @param dev 设备指针
 */
void platform_device_free(platform_device_t *dev);

/* ============================================================================
 * 资源访问 API
 * ============================================================================ */

/**
 * @brief 获取设备资源
 * 
 * @param dev 设备指针
 * @param type 资源类型
 * @param index 资源索引（同类型资源中的第几个）
 * @return 资源指针，未找到返回 NULL
 */
platform_resource_t *platform_get_resource(platform_device_t *dev,
                                           platform_res_type_t type,
                                           uint32_t index);

/**
 * @brief 获取设备 IRQ 号
 * 
 * @param dev 设备指针
 * @param index IRQ 索引
 * @return IRQ 号，失败返回 -1
 */
int32_t platform_get_irq(platform_device_t *dev, uint32_t index);

/**
 * @brief 获取设备 MMIO 基地址
 * 
 * @param dev 设备指针
 * @param index MMIO 区域索引
 * @return MMIO 基地址，失败返回 0
 */
uint64_t platform_get_mmio_base(platform_device_t *dev, uint32_t index);

/**
 * @brief 获取设备 MMIO 区域大小
 * 
 * @param dev 设备指针
 * @param index MMIO 区域索引
 * @return 区域大小，失败返回 0
 */
uint64_t platform_get_mmio_size(platform_device_t *dev, uint32_t index);

/* ============================================================================
 * 设备数据 API
 * ============================================================================ */

/**
 * @brief 设置驱动私有数据
 * 
 * @param dev 设备指针
 * @param data 私有数据指针
 */
static inline void platform_set_drvdata(platform_device_t *dev, void *data) {
    if (dev) {
        dev->driver_data = data;
    }
}

/**
 * @brief 获取驱动私有数据
 * 
 * @param dev 设备指针
 * @return 私有数据指针
 */
static inline void *platform_get_drvdata(platform_device_t *dev) {
    return dev ? dev->driver_data : NULL;
}

/* ============================================================================
 * 资源添加 API
 * ============================================================================ */

/**
 * @brief 向设备添加内存资源
 * 
 * @param dev 设备指针
 * @param start 起始地址
 * @param size 区域大小
 * @param flags 资源标志
 * @return HAL_OK 成功，其他为错误码
 */
hal_error_t platform_device_add_mem_resource(platform_device_t *dev,
                                              uint64_t start,
                                              uint64_t size,
                                              uint32_t flags);

/**
 * @brief 向设备添加 IRQ 资源
 * 
 * @param dev 设备指针
 * @param irq IRQ 号
 * @param flags 资源标志
 * @return HAL_OK 成功，其他为错误码
 */
hal_error_t platform_device_add_irq_resource(platform_device_t *dev,
                                              uint32_t irq,
                                              uint32_t flags);

/* ============================================================================
 * 框架初始化
 * ============================================================================ */

/**
 * @brief 初始化平台设备框架
 * 
 * @return HAL_OK 成功，其他为错误码
 */
hal_error_t platform_init(void);

/**
 * @brief 触发设备与驱动的匹配
 * 
 * 在所有设备和驱动注册完成后调用，尝试匹配并探测设备。
 * 
 * @return 成功匹配的设备数量
 */
int platform_match_devices(void);

/* ============================================================================
 * 调试 API
 * ============================================================================ */

/**
 * @brief 打印所有已注册的平台设备
 */
void platform_print_devices(void);

/**
 * @brief 打印所有已注册的平台驱动
 */
void platform_print_drivers(void);

#endif /* _DRIVERS_PLATFORM_H_ */
