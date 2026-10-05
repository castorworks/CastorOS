/**
 * @file task.h
 * @brief 任务管理 - 进程和线程控制
 * 
 * 实现多任务调度、进程管理、上下文切换等核心功能
 */

#ifndef _KERNEL_TASK_H_
#define _KERNEL_TASK_H_

#include <types.h>
#include <mm/vmm.h>
#include <kernel/ipc.h>

/* ============================================================================
 * 常量定义
 * ========================================================================== */

/** @brief 最大任务数量 */
#define MAX_TASKS 256

/** @brief 内核栈大小（8KB） */
#define KERNEL_STACK_SIZE (8 * 1024)

/** @brief 用户栈大小（1MB） */
#define USER_STACK_SIZE (1 * 1024 * 1024)

/** @brief 用户空间结束地址（内核空间起始地址） */
#if defined(ARCH_ARM64)
/* ARM64: User space is in TTBR0 region (0x0000_0000_0000_0000 - 0x0000_FFFF_FFFF_FFFF)
 * We use a more conservative limit for user stack placement */
#define USER_SPACE_END          0x0000800000000000ULL  /* 128TB - reasonable user space limit */
#define ARM64_USER_STACK_TOP    0x00007FFFFF000000ULL  /* User stack top (below 128TB) */
#else
#define USER_SPACE_END 0x80000000
#endif

/**
 * @brief init 的 PID。用户态把它当作名字服务的固定地址，所以必须是确定的值：
 * 普通任务的 PID 从 2 开始分配，这个号留给内核加载的第一个用户进程。
 */
#define INIT_PID 1

/** @brief 用户栈区域的顶端（不含） */
#if defined(ARCH_ARM64)
#define USER_STACK_TOP  ARM64_USER_STACK_TOP
#else
#define USER_STACK_TOP  USER_SPACE_END
#endif

/**
 * @brief 参数页：用户栈区域最顶上的一页不当栈用，exec 把程序参数放在这里
 *
 * 布局是 user_args_t：一个长度，后面跟着 length 字节、以 NUL 分隔的参数串。
 * 地址固定，所以用户态的启动代码不依赖任何寄存器约定就能找到它
 * （user/lib/src/crt0.cpp 里有同样的定义）。栈从这一页的下面开始向下长。
 */
#define USER_ARGS_ADDR  (USER_STACK_TOP - PAGE_SIZE)
#define USER_ARGS_MAX   (PAGE_SIZE - sizeof(uint32_t))

typedef struct {
    uint32_t length;                ///< data 里有效的字节数；0 表示没有参数
    char data[USER_ARGS_MAX];       ///< "arg0\0arg1\0...argN\0"
} user_args_t;

/** @brief 默认时间片（10ms） */
#define DEFAULT_TIME_SLICE 10

/** @brief 默认优先级 */
#define DEFAULT_PRIORITY 10

/* ============================================================================
 * 数据结构定义
 * ========================================================================== */

/**
 * @brief 任务状态枚举
 */
typedef enum {
    TASK_UNUSED = 0,      ///< 未使用（空闲 PCB 槽位）
    TASK_READY,           ///< 就绪状态（等待调度）
    TASK_RUNNING,         ///< 运行状态（正在执行）
    TASK_BLOCKED,         ///< 阻塞状态（等待事件）
    TASK_ZOMBIE,          ///< 僵尸状态（已退出，等待父进程回收）
    TASK_TERMINATED       ///< 终止状态（已退出）
} task_state_t;

/**
 * @brief CPU 上下文结构
 * 
 * 保存任务切换时需要保存/恢复的所有 CPU 寄存器
 * 架构相关：i686 使用 32 位寄存器，x86_64 使用 64 位寄存器，ARM64 使用 64 位寄存器
 */
#if defined(ARCH_X86_64)
/* x86_64: 64-bit context structure */
typedef struct {
    /* General purpose registers */
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    /* Instruction pointer */
    uint64_t rip;        ///< 指令指针

    /* Code segment */
    uint64_t cs;

    /* Flags register */
    uint64_t rflags;     ///< 标志寄存器

    /* Stack pointer */
    uint64_t rsp;        ///< 栈指针

    /* Stack segment */
    uint64_t ss;

    /* Page table base register */
    uint64_t cr3;        ///< 页目录物理地址
} __attribute__((packed)) cpu_context_t;

/* Compatibility aliases for x86_64 */
#define eip rip
#define esp rsp
#define eflags rflags
#define eax rax
#define ebx rbx
#define ecx rcx
#define edx rdx
#define esi rsi
#define edi rdi
#define ebp rbp

#elif defined(ARCH_ARM64)
/* ARM64: 64-bit context structure */
typedef struct {
    /* General purpose registers X0-X30 */
    uint64_t x[31];              /* X0-X30 */

    /* Stack pointer */
    uint64_t sp;

    /* Program counter - stored in ELR_EL1 */
    uint64_t pc;

    /* Processor state - stored in SPSR_EL1 */
    uint64_t pstate;

    /* User page table base register (TTBR0_EL1) */
    uint64_t ttbr0;

    /* Kernel stack top loaded into SP_EL1 before returning to EL0.
     * Layout must match arm64_context_t (arch/arm64/include/context.h). */
    uint64_t kernel_sp;
} __attribute__((packed, aligned(16))) cpu_context_t;

static_assert(sizeof(cpu_context_t) == 288, "cpu_context_t must match arm64_context_t");
static_assert(__builtin_offsetof(cpu_context_t, kernel_sp) == 280, "kernel_sp offset mismatch");

/* Compatibility aliases for ARM64 */
#define eip pc
#define esp sp
#define cr3 ttbr0

/* ARM64 PSTATE bits */
#define ARM64_PSTATE_EL0t   0x00    /* EL0 with SP_EL0 */
#define ARM64_PSTATE_EL1t   0x04    /* EL1 with SP_EL0 */
#define ARM64_PSTATE_EL1h   0x05    /* EL1 with SP_EL1 */

#else
/* i686: 32-bit context structure */
typedef struct {
    /* 段寄存器 */
    uint16_t gs, _gs_padding;
    uint16_t fs, _fs_padding;
    uint16_t es, _es_padding;
    uint16_t ds, _ds_padding;

    /* 通用寄存器（按 PUSHA 顺序） */
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy;  // PUSHA 会压入 ESP，但我们不使用它
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;

    /* 特殊寄存器 */
    uint32_t eip;        ///< 指令指针
    uint16_t cs, _cs_padding;
    uint32_t eflags;     ///< 标志寄存器

    /* 用户态栈指针（Ring 3 时使用） */
    uint32_t esp;        ///< 栈指针
    uint16_t ss, _ss_padding;

    /* 页目录基址寄存器 */
    uint32_t cr3;        ///< 页目录物理地址
} __attribute__((packed)) cpu_context_t;
#endif

/**
 * @brief 任务控制块（TCB/PCB）
 * 
 * 进程控制块，包含进程的所有状态信息
 * 地址相关字段使用 uintptr_t 以支持 32/64 位架构
 */
typedef struct task {
    /* 基本信息 */
    uint32_t pid;                    ///< 进程 ID
    char name[32];                   ///< 进程名称
    task_state_t state;              ///< 任务状态

    /* 调度信息 */
    uint32_t priority;               ///< 优先级（数值越小优先级越高）
    uint32_t time_slice;             ///< 时间片（毫秒）
    uint64_t runtime_ms;             ///< 累计运行时间（毫秒）
    uint64_t sleep_until_ms;         ///< 睡眠截止时间（0 表示不睡眠）

    /* CPU 上下文 */
    cpu_context_t context;           ///< CPU 寄存器状态

    /* 内核栈 */
    uintptr_t kernel_stack_base;     ///< 内核栈基址（低地址）
    uintptr_t kernel_stack;          ///< 内核栈顶指针（高地址）

    /* 用户空间（仅用户进程） */
    bool is_user_process;            ///< 是否为用户进程
    uintptr_t user_entry;            ///< 用户程序入口点
    uintptr_t user_stack_base;       ///< 用户栈基址（低地址）
    uintptr_t user_stack;            ///< 用户栈顶指针（高地址）

    /* 内存管理 */
    uintptr_t page_dir_phys;         ///< 页目录物理地址
    page_directory_t *page_dir;      ///< 页目录虚拟地址

    /* 堆管理 */
    uintptr_t heap_start;            ///< 堆起始地址（初始 brk）
    uintptr_t heap_end;              ///< 当前堆结束地址（当前 brk）
    uintptr_t heap_max;              ///< 堆最大地址（防止与栈冲突）

    /* 进程关系 */
    struct task *parent;             ///< 父进程

    /* 退出信息 */
    uint32_t exit_code;              ///< 退出码
    bool exit_signaled;              ///< 是否通过信号终止
    uint32_t exit_signal;            ///< 终止信号号（如果 exit_signaled 为 true）

    /* 等待队列 */
    struct task *waiting_parent;     ///< 正在等待此进程的父进程

    /* 链表指针（用于就绪队列） */
    struct task *next;               ///< 下一个任务（链表）
    struct task *prev;               ///< 上一个任务（链表）

    /* 阻塞时等待的对象（Mutex/Semaphore 等的地址）；Scheduler::wakeup 按它匹配 */
    void *wait_object;

    /* 待处理的终止请求（kill）。别的任务只置这个标志，由目标任务自己在
     * 系统调用返回用户态前（不持有任何内核锁时）执行退出 */
    bool kill_pending;
    uint32_t kill_signal;            ///< kill_pending 为 true 时的信号号

    /* 进程间通信（kernel/ipc.cpp）。阻塞在 IPC 上时 wait_object == &ipc_state */
    ipc_state_t ipc_state;           ///< 是否正阻塞在 send/recv 上
    uint32_t ipc_peer;               ///< SENDING: 目标 PID；RECEIVING: 期望的发送者或 IPC_ANY
    int ipc_result;                  ///< 等待结束时对方（或退出路径）写入的结果
    bool ipc_calling;                ///< SENDING 且处于 call 中：消息被取走后直接转为等应答
    ipc_msg ipc_buf;                 ///< SENDING: 待取走的消息；RECEIVING: 投递进来的消息
    uint32_t irq_pending;            ///< 已到达、还没被 recv 取走的设备中断（kernel/user_irq.h）

    /**
     * 特权进程：可以 kill 任意用户进程、访问设备寄存器、认领设备中断。
     * 第一个用户进程（init）有特权，fork 和 exec 都保留，
     * 直到进程自己调用 drop_privilege（不可恢复）。
     */
    bool privileged;
} task_t;

/* ============================================================================
 * 全局变量声明
 * ========================================================================== */

/** @brief 任务控制块池（在 task.c 中定义） */
extern task_t task_pool[MAX_TASKS];

/**
 * @brief 退出当前任务
 * 
 * @param exit_code 退出码
 * @note 此函数不会返回
 */
extern "C" void task_exit(uint32_t exit_code) __attribute__((noreturn));

/**
 * @brief 从中断上下文调度
 * 
 * @param regs 中断寄存器状态
 */
void schedule_from_irq(bool from_user);

/**
 * @brief 执行上下文切换
 * 
 * @param old_ctx 保存旧任务上下文的地址
 * @param new_ctx 新任务上下文的地址
 */
extern "C" void task_switch_context(cpu_context_t **old_ctx, cpu_context_t *new_ctx);

/**
 * @brief 首次进入任务（用于内核线程）
 * 
 * 用于第一次启动内核线程
 */
extern "C" void task_enter_kernel_thread(void);

namespace kernel {

/**
 * @brief 任务管理与调度器（任务创建/销毁、就绪队列、阻塞与唤醒）
 */
class Scheduler {
public:
    /* ============================================================================
     * 核心函数声明
     * ========================================================================== */

    /**
     * @brief 初始化任务管理系统
     * 
     * 创建 idle 任务，初始化调度器
     */
    static void init();

    /**
     * @brief 创建内核线程
     * 
     * @param entry 线程入口函数
     * @param name 线程名称
     * @return 成功返回 PID，失败返回 0
     */
    static uint32_t create_kernel_thread(void (*entry)(), const char *name);

    /**
     * @brief 创建用户进程
     * 
     * @param name 进程名称
     * @param entry_point 用户程序入口点
     * @param page_dir 页目录
     * @param program_end 程序加载的最高地址（用于设置堆起始地址）
     * @return 成功返回 PID，失败返回 0
     *
     * 所有权：成功后 page_dir 归新进程所有，随进程回收一起释放；
     * 失败（返回 0）时 page_dir 仍归调用者，由调用者销毁。
     */
    static uint32_t create_user_process(const char *name, uintptr_t entry_point,
                                       page_directory_t *page_dir, uintptr_t program_end);

    /**
     * @brief 把刚创建、还没运行过的任务 pid 改成 init（PID 1）
     * @return 成功返回 true；找不到该任务或 PID 1 已被占用返回 false
     */
    static bool make_init(uint32_t pid);

    /**
     * @brief 主动让出 CPU（切换到其他任务）
     */
    static void yield();

    /**
     * @brief 任务睡眠指定时间
     * 
     * @param ms 睡眠时间（毫秒）
     */
    static void sleep(uint32_t ms);

    /**
     * @brief 阻塞当前任务
     * 
     * @param wait_object 等待对象指针（用于调试）
     */
    static void block(void *wait_object);

    /**
     * @brief 唤醒等待在指定对象上的一个任务
     * 
     * @param wait_object 等待对象指针（用于调试）
     */
    static void wakeup(void *wait_object);

    /**
     * @brief 结束当前任务（不返回）
     *
     * 处理子进程，然后变成僵尸（有父进程）或
     * 交给调度器延迟回收（无父进程）。只能在任务自己的上下文调用。
     *
     * @param exit_code 退出码
     * @param signaled  是否因信号终止
     * @param signal    信号号（signaled 为 true 时有效）
     */
    static void exit_current(uint32_t exit_code, bool signaled, uint32_t signal)
        __attribute__((noreturn));

    /**
     * @brief 请求终止另一个任务
     *
     * 只记录待处理的信号（并把正在 sleep 或等待 IPC 的目标提前唤醒），不触碰目标的
     * 状态和资源：目标停在内核里的某个 yield/block 点，可能正持有互斥锁，
     * 必须由它自己在安全点（deliver_pending_kill）退出。
     *
     * @return 目标仍然存活并已记录请求返回 true
     */
    static bool request_kill(task_t *target, uint32_t signal);

    /**
     * @brief 若当前任务有待处理的 kill，就地退出（不返回）
     *
     * 在系统调用返回用户态之前调用；此时当前任务不持有任何内核锁。
     */
    static void deliver_pending_kill();

    /**
     * @brief 获取当前正在运行的任务
     *
     * @return 当前任务指针，如果没有则返回 NULL
     */
    static task_t* get_current();

    /**
     * 当前执行流是否有特权。没有当前任务（启动阶段）和内核线程视为有特权。
     */
    static bool current_is_privileged();

    /**
     * target 是否是 ancestor 的子孙进程（不含 ancestor 自身）
     */
    static bool is_descendant(task_t *target, task_t *ancestor);

    /**
     * @brief 根据 PID 查找任务
     * 
     * @param pid 进程 ID
     * @return 任务指针，如果未找到则返回 NULL
     */
    static task_t* get_by_pid(uint32_t pid);

    /**
     * @brief 任务调度器
     * 
     * 选择下一个任务并切换上下文
     * 通常由定时器中断或主动让出时调用
     */
    static void schedule();

    /**
     * @brief 定时器中断处理
     * 
     * 更新任务运行时间，处理睡眠任务，触发调度
     * 由定时器驱动调用
     */
    static void timer_tick();

    /* ============================================================================
     * 辅助函数
     * ========================================================================== */

    /**
     * @brief 分配一个空闲的任务控制块
     * 
     * @return 任务指针，如果没有空闲 PCB 则返回 NULL
     */
    static task_t* alloc();

    /**
     * @brief 释放任务控制块
     * 
     * @param task 任务指针
     */
    static void free(task_t *task);

    /**
     * @brief 设置用户栈
     * 
     * 为用户进程分配并映射用户栈空间
     * 
     * @param task 任务指针
     * @return 成功返回 true，失败返回 false
     */
    static bool setup_user_stack(task_t *task);

    /**
     * @brief 将任务添加到就绪队列
     * 
     * @param task 任务指针
     */
    static void ready_queue_add(task_t *task);

    /**
     * @brief 获取系统中的任务数量
     * 
     * @return 活动任务数量
     */
    static uint32_t get_count();

    /* ============================================================================
     * 汇编函数声明（在 task_asm.asm 中实现）
     * ========================================================================== */

    /* ============================================================================
     * 测试辅助函数（仅在测试模式下使用）
     * ========================================================================== */

    /**
     * @brief 检查是否应该使栈页分配失败（用于测试）
     * 
     * @param page_index 页索引
     * @return 如果应该失败返回 true
     */
    static bool should_fail_stack_page(uint32_t page_index);
};

} // namespace kernel

#endif // _KERNEL_TASK_H_
