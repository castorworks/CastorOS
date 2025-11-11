// ============================================================================
// timer.c - 可编程间隔定时器（PIT）驱动
// ============================================================================

#include <drivers/timer.h>
#include <kernel/io.h>
#include <kernel/irq.h>
#include <kernel/isr.h>
#include <kernel/task.h>
#include <lib/klog.h>
#include <lib/kprintf.h>

/* 全局变量 */
static volatile uint64_t timer_ticks = 0;  // 定时器滴答计数（volatile：中断处理程序会修改）
static uint32_t timer_frequency = 0;       // 定时器频率（Hz）

/* 定时器回调支持 */
#define MAX_TIMER_CALLBACKS 16

typedef struct {
    uint32_t id;                    // 定时器ID
    timer_callback_t callback;      // 回调函数
    void *data;                     // 用户数据
    uint64_t trigger_tick;          // 触发时刻（tick数）
    uint32_t interval_ms;           // 间隔（毫秒）
    bool repeat;                    // 是否重复
    bool active;                    // 是否活动
} timer_entry_t;

static timer_entry_t timer_callbacks[MAX_TIMER_CALLBACKS];
static uint32_t next_timer_id = 1;
static uint32_t active_timer_count = 0;

/**
 * 定时器中断处理函数
 * 每次定时器中断时被调用
 */
static void timer_callback(registers_t *regs) {
    (void)regs;  // 未使用参数
    timer_ticks++;

    // 每次定时器中断时，调用任务调度器
    task_schedule();
    
    /* 处理定时器回调 */
    for (uint32_t i = 0; i < MAX_TIMER_CALLBACKS; i++) {
        if (!timer_callbacks[i].active) {
            continue;
        }
        
        /* 检查是否到期 */
        if (timer_ticks >= timer_callbacks[i].trigger_tick) {
            /* 调用回调函数 */
            if (timer_callbacks[i].callback != NULL) {
                timer_callbacks[i].callback(timer_callbacks[i].data);
            }
            
            /* 处理重复定时器 */
            if (timer_callbacks[i].repeat) {
                /* 计算下一次触发时刻 */
                uint64_t interval_ticks = (timer_callbacks[i].interval_ms * timer_frequency) / 1000;
                timer_callbacks[i].trigger_tick = timer_ticks + interval_ticks;
            } else {
                /* 一次性定时器，标记为非活动 */
                timer_callbacks[i].active = false;
                active_timer_count--;
            }
        }
    }
}

/**
 * 初始化 PIT
 */
void timer_init(uint32_t frequency) {
    LOG_INFO_MSG("Initializing PIT (Programmable Interval Timer)...\n");
    
    /* 初始化定时器回调数组 */
    for (uint32_t i = 0; i < MAX_TIMER_CALLBACKS; i++) {
        timer_callbacks[i].active = false;
        timer_callbacks[i].id = 0;
        timer_callbacks[i].callback = NULL;
        timer_callbacks[i].data = NULL;
    }
    
    /* 保存频率 */
    timer_frequency = frequency;
    
    /* 计算除数 */
    uint32_t divisor = PIT_FREQUENCY / frequency;
    
    /* 防止除数过大或过小 */
    if (divisor > 65535) {
        divisor = 65535;
        timer_frequency = PIT_FREQUENCY / divisor;
        LOG_WARN_MSG("Requested frequency too low, using %u Hz\n", timer_frequency);
    } else if (divisor < 1) {
        divisor = 1;
        timer_frequency = PIT_FREQUENCY;
        LOG_WARN_MSG("Requested frequency too high, using %u Hz\n", timer_frequency);
    }
    
    /* 发送命令字节 */
    /* 通道 0, 访问模式: 先低后高, 模式 3: 方波发生器, 二进制模式 */
    outb(PIT_COMMAND, PIT_CMD_INIT);  // 使用常量而不是魔数 0x36
    
    /* 发送除数（先低字节，后高字节） */
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
    
    /* 注册 IRQ 0 处理函数 */
    irq_register_handler(0, timer_callback);
    
    LOG_INFO_MSG("PIT initialized successfully\n");
    LOG_DEBUG_MSG("  Frequency: %u Hz\n", timer_frequency);
    LOG_DEBUG_MSG("  Divisor: %u\n", divisor);
    LOG_DEBUG_MSG("  Interval: %u us\n", 1000000 / timer_frequency);
}

/**
 * 获取定时器滴答数
 */
uint64_t timer_get_ticks(void) {
    return timer_ticks;
}

/**
 * 获取定时器频率
 */
uint32_t timer_get_frequency(void) {
    return timer_frequency;
}

/**
 * 获取系统运行时间（毫秒）
 */
uint64_t timer_get_uptime_ms(void) {
    if (timer_frequency == 0) {
        return 0;
    }
    return (timer_ticks * 1000) / timer_frequency;
}

/**
 * 获取系统运行时间（秒）
 */
uint32_t timer_get_uptime_sec(void) {
    if (timer_frequency == 0) {
        return 0;
    }
    return (uint32_t)(timer_ticks / timer_frequency);
}

/**
 * 简单延迟（忙等待）
 * 
 * 警告：这是忙等待实现，会占用 CPU
 * 仅用于早期启动阶段或短时间延迟
 */
void timer_wait(uint32_t ms) {
    uint64_t target = timer_get_uptime_ms() + ms;
    while (timer_get_uptime_ms() < target) {
        __asm__ volatile("pause");  // 减少功耗
    }
}

/**
 * 微秒级延迟（高精度）
 * 
 * 使用 PIT 计数器提供高精度延迟
 * 适用于需要精确短延迟的场景
 */
void timer_udelay(uint32_t us) {
    /* 对于小于 1 毫秒的延迟，使用基于 PIT 计数的方法 */
    if (us == 0) {
        return;
    }
    
    /* 计算需要等待的 PIT 计数 */
    uint32_t ticks_needed = (us * PIT_FREQUENCY) / 1000000;
    
    if (ticks_needed == 0) {
        ticks_needed = 1;
    }
    
    /* 锁存当前计数器值 */
    outb(PIT_COMMAND, 0x00);
    uint8_t low = inb(PIT_CHANNEL0);
    uint8_t high = inb(PIT_CHANNEL0);
    uint16_t initial = (high << 8) | low;
    
    /* 等待计数器递减到目标值 */
    uint32_t elapsed = 0;
    while (elapsed < ticks_needed) {
        /* 读取当前计数器 */
        outb(PIT_COMMAND, 0x00);
        low = inb(PIT_CHANNEL0);
        high = inb(PIT_CHANNEL0);
        uint16_t current = (high << 8) | low;
        
        /* 计算已过去的计数（处理回绕） */
        if (current <= initial) {
            elapsed = initial - current;
        } else {
            /* 计数器回绕了 */
            elapsed = initial + (0xFFFF - current);
        }
        
        __asm__ volatile("pause");
    }
}

/**
 * 注册定时器回调
 */
uint32_t timer_register_callback(timer_callback_t callback, void *data,
                                  uint32_t interval_ms, bool repeat) {
    if (callback == NULL || interval_ms == 0) {
        LOG_WARN_MSG("Invalid timer callback parameters\n");
        return 0;
    }
    
    /* 查找空闲槽位 */
    for (uint32_t i = 0; i < MAX_TIMER_CALLBACKS; i++) {
        if (!timer_callbacks[i].active) {
            /* 计算触发时刻 */
            uint64_t interval_ticks = (interval_ms * timer_frequency) / 1000;
            
            /* 填充定时器条目 */
            timer_callbacks[i].id = next_timer_id++;
            timer_callbacks[i].callback = callback;
            timer_callbacks[i].data = data;
            timer_callbacks[i].trigger_tick = timer_ticks + interval_ticks;
            timer_callbacks[i].interval_ms = interval_ms;
            timer_callbacks[i].repeat = repeat;
            timer_callbacks[i].active = true;
            
            active_timer_count++;
            
            LOG_DEBUG_MSG("Timer callback registered: ID=%u, interval=%ums, repeat=%d\n",
                         timer_callbacks[i].id, interval_ms, repeat);
            
            return timer_callbacks[i].id;
        }
    }
    
    LOG_WARN_MSG("No free timer slots available (max: %d)\n", MAX_TIMER_CALLBACKS);
    return 0;
}

/**
 * 取消定时器回调
 */
bool timer_unregister_callback(uint32_t timer_id) {
    if (timer_id == 0) {
        return false;
    }
    
    for (uint32_t i = 0; i < MAX_TIMER_CALLBACKS; i++) {
        if (timer_callbacks[i].active && timer_callbacks[i].id == timer_id) {
            timer_callbacks[i].active = false;
            active_timer_count--;
            
            LOG_DEBUG_MSG("Timer callback unregistered: ID=%u\n", timer_id);
            return true;
        }
    }
    
    LOG_WARN_MSG("Timer ID %u not found\n", timer_id);
    return false;
}

/**
 * 获取活动定时器数量
 */
uint32_t timer_get_active_count(void) {
    return active_timer_count;
}
