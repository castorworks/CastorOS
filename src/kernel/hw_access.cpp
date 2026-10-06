// ============================================================================
// hw_access.cpp - 按设备授权：一个进程碰得到哪些硬件
// ============================================================================

#include <kernel/hw_access.h>
#include <kernel/task.h>

namespace kernel {

/** I/O 端口号和中断号的上限（不含）：再大的范围不可能有意义 */
#define HW_PORT_LIMIT   0x10000ULL
#define HW_IRQ_LIMIT    0x10000ULL

/** [start, start + count) 是不是一个合法的 kind 类资源范围 */
static bool range_ok(uint32_t kind, uint64_t start, uint64_t count) {
    if (count == 0 || start + count < start) {
        return false;
    }
    switch (kind) {
        case HW_PORTS:  return start + count <= HW_PORT_LIMIT;
        case HW_IRQ:    return start + count <= HW_IRQ_LIMIT;
        case HW_MEMORY: return true;
        default:        return false;
    }
}

bool HwAccess::covers(const task_t *task, uint32_t kind, uint64_t start, uint64_t count) {
    if (!task || !range_ok(kind, start, count)) {
        return false;
    }
    for (uint32_t i = 0; i < task->hw_allowed_count; i++) {
        const hw_range *r = &task->hw_allowed[i];
        if (r->kind != kind) {
            continue;
        }
        uint64_t first = r->start;
        uint64_t end = r->start + r->count;
        if (kind == HW_MEMORY) {
            first &= ~(uint64_t)(PAGE_SIZE - 1);
            // 最后一页顶到地址空间的尽头时向上取整会回绕成 0：那就是"到尽头为止"
            end = (end + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
            if (end == 0) {
                end = ~(uint64_t)0;
            }
        }
        if (start >= first && start + count <= end) {
            return true;
        }
    }
    return false;
}

bool HwAccess::allow(task_t *task, uint32_t kind, uint64_t start, uint64_t count) {
    if (!task || !range_ok(kind, start, count)) {
        return false;
    }
    if (covers(task, kind, start, count)) {
        return true;
    }
    if (task->hw_allowed_count >= HW_ALLOW_MAX) {
        return false;
    }
    hw_range *r = &task->hw_allowed[task->hw_allowed_count++];
    r->kind = kind;
    r->reserved = 0;
    r->start = start;
    r->count = count;
    return true;
}

bool HwAccess::current_may(uint32_t kind, uint64_t start, uint64_t count) {
    return Scheduler::current_is_privileged() || covers(Scheduler::get_current(), kind, start, count);
}

bool HwAccess::current_is_driver() {
    if (Scheduler::current_is_privileged()) {
        return true;
    }
    const task_t *current = Scheduler::get_current();
    return current && current->hw_allowed_count > 0;
}

} // namespace kernel
