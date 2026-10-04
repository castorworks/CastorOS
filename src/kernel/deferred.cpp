// ============================================================================
// deferred.cpp - 延迟工作（kworker 线程）
// ============================================================================

#include <kernel/deferred.h>
#include <kernel/task.h>
#include <kernel/interrupt.h>
#include <lib/klog.h>

namespace kernel {

#define DEFERRED_MAX 8

struct DeferredWork {
    void (*fn)(void);
    const char *name;
    volatile bool pending;
};

static DeferredWork works[DEFERRED_MAX];
static int work_count = 0;
static bool worker_started = false;

int Deferred::add(void (*fn)(void), const char *name) {
    if (!fn) {
        return -1;
    }
    bool irq_state = kernel::Interrupts::disable();
    int id = -1;
    if (work_count < DEFERRED_MAX) {
        id = work_count++;
        works[id].fn = fn;
        works[id].name = name;
        works[id].pending = false;
    }
    kernel::Interrupts::restore(irq_state);
    if (id < 0) {
        LOG_ERROR_MSG("deferred: table full, cannot add '%s'\n", name ? name : "?");
    }
    return id;
}

void Deferred::raise(int id) {
    if (id < 0 || id >= work_count) {
        return;
    }
    works[id].pending = true;
    kernel::Scheduler::wakeup(works);
}

/* 取出一项待执行的工作；没有则返回 -1。调用者必须已关中断。 */
static int take_pending(void) {
    for (int i = 0; i < work_count; i++) {
        if (works[i].pending) {
            works[i].pending = false;
            return i;
        }
    }
    return -1;
}

static void worker_thread(void) {
    while (true) {
        bool irq_state = kernel::Interrupts::disable();
        int id = take_pending();
        if (id < 0) {
            // 关中断下确认没有待办才阻塞，raise() 不会插在检查和阻塞之间
            kernel::Scheduler::block(works);
            kernel::Interrupts::restore(irq_state);
            continue;
        }
        kernel::Interrupts::restore(irq_state);
        works[id].fn();
    }
}

void Deferred::start() {
    if (worker_started) {
        return;
    }
    worker_started = true;
    kernel::Scheduler::create_kernel_thread(worker_thread, "kworker");
}

} // namespace kernel
