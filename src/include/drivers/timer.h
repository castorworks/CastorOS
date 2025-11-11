#ifndef _DRIVERS_TIMER_H_
#define _DRIVERS_TIMER_H_

#include <types.h>

/**
 * 可编程间隔定时器（PIT）驱动
 * 
 * Intel 8253/8254 PIT 芯片
 * 提供周期性中断，用于时间管理和进程调度
 */

/* PIT 端口 */
#define PIT_CHANNEL0    0x40    // 通道 0 数据端口
#define PIT_CHANNEL1    0x41    // 通道 1 数据端口（已废弃）
#define PIT_CHANNEL2    0x42    // 通道 2 数据端口（PC 扬声器）
#define PIT_COMMAND     0x43    // 命令寄存器

/* PIT 频率常量 */
#define PIT_FREQUENCY   1193182 // 输入频率（约 1.19 MHz）

/* PIT 命令字节组成部分 */
#define PIT_CMD_CHANNEL0        0x00    // 选择通道 0
#define PIT_CMD_ACCESS_LOHI     0x30    // 访问模式：先低后高字节
#define PIT_CMD_MODE3           0x06    // 操作模式 3：方波发生器
#define PIT_CMD_BINARY          0x00    // 二进制模式（非 BCD）

/* PIT 初始化命令（通道0，先低后高，模式3，二进制）*/
#define PIT_CMD_INIT    (PIT_CMD_CHANNEL0 | PIT_CMD_ACCESS_LOHI | PIT_CMD_MODE3 | PIT_CMD_BINARY)

/**
 * 初始化 PIT
 * @param frequency 目标频率（Hz），推荐 100-1000 Hz
 */
void timer_init(uint32_t frequency);

/**
 * 获取系统运行时间（毫秒）
 * @return 自启动以来的毫秒数
 */
uint64_t timer_get_uptime_ms(void);

/**
 * 获取系统运行时间（秒）
 * @return 自启动以来的秒数
 */
uint32_t timer_get_uptime_sec(void);

/**
 * 获取定时器滴答数
 * @return 定时器中断次数
 */
uint64_t timer_get_ticks(void);

/**
 * 获取定时器频率
 * @return 定时器频率（Hz）
 */
uint32_t timer_get_frequency(void);

/**
 * 简单延迟（忙等待）
 * @param ms 延迟毫秒数
 * 
 * 注意：这是忙等待实现，会占用 CPU
 * 仅用于早期启动阶段或短时间延迟
 */
void timer_wait(uint32_t ms);

/**
 * 微秒级延迟（高精度）
 * @param us 延迟微秒数
 * 
 * 注意：这是忙等待实现，使用 PIT 计数器提供高精度
 * 适用于需要精确短延迟的场景（如硬件初始化）
 */
void timer_udelay(uint32_t us);

/**
 * 定时器回调函数类型
 */
typedef void (*timer_callback_t)(void *data);

/**
 * 注册定时器回调
 * @param callback 回调函数
 * @param data 传递给回调函数的数据
 * @param interval_ms 间隔（毫秒）
 * @param repeat 是否重复（true=周期性，false=一次性）
 * @return 定时器ID（用于取消），失败返回 0
 */
uint32_t timer_register_callback(timer_callback_t callback, void *data,
                                  uint32_t interval_ms, bool repeat);

/**
 * 取消定时器回调
 * @param timer_id 定时器ID
 * @return true 成功，false 失败（未找到）
 */
bool timer_unregister_callback(uint32_t timer_id);

/**
 * 获取活动定时器数量
 * @return 当前注册的定时器回调数量
 */
uint32_t timer_get_active_count(void);

#endif // _DRIVERS_TIMER_H_
