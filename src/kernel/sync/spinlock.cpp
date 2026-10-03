#include <kernel/sync/spinlock.h>

static constexpr uint32_t SPINLOCK_UNLOCKED = 0;
static constexpr uint32_t SPINLOCK_LOCKED   = 1;

#if defined(ARCH_ARM64)
/* ARM64 原子交换操作 */
static inline uint32_t atomic_xchg(volatile uint32_t *addr, uint32_t new_value) {
    uint32_t old_value, tmp;
    __asm__ volatile(
        "1: ldaxr %w0, [%2]\n"      /* Load-Acquire Exclusive */
        "   stlxr %w1, %w3, [%2]\n" /* Store-Release Exclusive */
        "   cbnz  %w1, 1b\n"        /* Retry if store failed */
        : "=&r"(old_value), "=&r"(tmp)
        : "r"(addr), "r"(new_value)
        : "memory"
    );
    return old_value;
}

/* ARM64 CPU 暂停提示 */
static inline void cpu_relax(void) {
    __asm__ volatile("yield" ::: "memory");
}
#else
/* x86 原子交换操作 */
static inline uint32_t atomic_xchg(volatile uint32_t *addr, uint32_t new_value) {
    uint32_t old_value;
    __asm__ volatile("xchg %0, %1"
                     : "=r"(old_value), "+m"(*addr)
                     : "0"(new_value)
                     : "memory");
    return old_value;
}

/* x86 CPU 暂停提示 */
static inline void cpu_relax(void) {
    __asm__ volatile("pause" ::: "memory");
}
#endif

namespace sync {

void Spinlock::init() {
    value_ = SPINLOCK_UNLOCKED;
}

bool Spinlock::try_lock() {
    return atomic_xchg(&value_, SPINLOCK_LOCKED) == SPINLOCK_UNLOCKED;
}

void Spinlock::lock() {
    while (!try_lock()) {
        cpu_relax();
    }
}

void Spinlock::unlock() {
    // 使用原子交换操作确保多核可见性
    atomic_xchg(&value_, SPINLOCK_UNLOCKED);
}

bool Spinlock::is_locked() const {
    return value_ == SPINLOCK_LOCKED;
}

void Spinlock::lock_irqsave(bool &irq_state) {
    irq_state = kernel::Interrupts::disable();
    lock();
}

void Spinlock::unlock_irqrestore(bool irq_state) {
    unlock();
    kernel::Interrupts::restore(irq_state);
}

} // namespace sync
