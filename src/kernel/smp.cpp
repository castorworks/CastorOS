// ============================================================================
// smp.cpp - 多个 CPU：内核大锁，和其余 CPU 的启动
// ============================================================================

#include <kernel/smp.h>
#include <kernel/task.h>
#include <kernel/interrupt.h>
#include <kernel/sync/spinlock.h>
#include <hal/hal.h>
#include <lib/klog.h>

namespace kernel {

/* ============================================================================
 * 内核大锁
 * ========================================================================== */

/** 锁本身 */
static sync::Spinlock lock_word;

/**
 * 每个 CPU 拿了几层。只有这个 CPU 自己读写，而且都是在关着中断的时候。
 * 启动 CPU 在 kernel_main 一开头就 enter() 一次：它启动内核的那段时间里别的 CPU 还没
 * 起来，但规矩从头就立着，后面的代码不用分情况。
 */
static uint32_t depth[MAX_CPUS];

static void spin_acquire(void) {
    // 别人拿着就空转着等。等的时候中断是关着的；拿着锁的 CPU 不会等我们做任何事
    lock_word.lock();
}

static void spin_release(void) {
    lock_word.unlock();
}

void KernelLock::enter() {
    InterruptGuard guard;
    uint32_t cpu = hal::Cpu::id();
    if (depth[cpu] == 0) {
        spin_acquire();
    }
    depth[cpu]++;
}

void KernelLock::leave() {
    InterruptGuard guard;
    uint32_t cpu = hal::Cpu::id();
    if (depth[cpu] == 0) {
        return;
    }
    if (--depth[cpu] == 0) {
        spin_release();
    }
}

void KernelLock::release() {
    InterruptGuard guard;
    uint32_t cpu = hal::Cpu::id();
    if (depth[cpu] != 0) {
        depth[cpu] = 0;
        spin_release();
    }
}

bool KernelLock::held() {
    InterruptGuard guard;
    return depth[hal::Cpu::id()] != 0;
}

/* ============================================================================
 * 其余 CPU 的启动
 * ========================================================================== */

static volatile uint32_t online_cpus = 1;

uint32_t Smp::cpu_count() {
    return online_cpus;
}

void Smp::secondary_main() {
    // 这个 CPU 刚从固件手里接过来，中断还关着。先拿锁：从这里起它和别的 CPU 一样守规矩
    KernelLock::enter();
    hal::Cpu::init_secondary();

    uint32_t cpu = hal::Cpu::id();
    LOG_INFO_MSG("CPU %u online\n", cpu);
    online_cpus = online_cpus + 1;      // 拿着内核锁，不会和别的 CPU 撞上

    // 成为这个 CPU 的 idle 任务：之后它像启动 CPU 一样，没事就等中断，有任务就去运行
    Scheduler::run_idle();
}

void Smp::start_secondaries() {
    uint32_t started = hal::Cpu::start_secondaries();
    if (started > 0) {
        LOG_INFO_MSG("SMP: %u CPUs running\n", cpu_count());
    }
}

} // namespace kernel
