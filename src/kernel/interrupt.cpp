#include <kernel/interrupt.h>
#include <kernel/panic.h>
#include <lib/klog.h>

static volatile uint32_t interrupt_depth = 0;

void interrupt_enter(void) {
    interrupt_depth = interrupt_depth + 1;
}

void interrupt_exit(void) {
    if (interrupt_depth == 0) {
        return;
    }
    interrupt_depth = interrupt_depth - 1;
}

bool in_interrupt(void) {
    return interrupt_depth != 0;
}

uint32_t interrupt_depth_suspend(void) {
    uint32_t depth = interrupt_depth;
    interrupt_depth = 0;
    return depth;
}

void interrupt_depth_resume(uint32_t depth) {
    interrupt_depth = depth;
}

/* 违规时直接 panic：网络接收、TCP 定时器、USB 热插拔都已移到任务上下文，
 * 中断里不应再出现任何可睡眠操作。置为 false 可退回到只告警一次。 */
static const bool MAY_SLEEP_VIOLATION_PANICS = true;

void assert_may_sleep(const char *what) {
    if (!in_interrupt()) {
        return;
    }
    if (MAY_SLEEP_VIOLATION_PANICS) {
        LOG_ERROR_MSG("%s called in interrupt context\n", what);
        PANIC("sleeping operation in interrupt context");
    }
    /* 警告模式：每种操作只报一次，避免在中断里刷屏 */
    static const char *reported[8];
    for (uint32_t i = 0; i < 8; i++) {
        if (reported[i] == what) {
            return;
        }
        if (reported[i] == NULL) {
            reported[i] = what;
            LOG_ERROR_MSG("%s called in interrupt context (not allowed)\n", what);
            return;
        }
    }
}

