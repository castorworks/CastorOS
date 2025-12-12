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
#include <kernel/fd_table.h>

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
#define USER_SPACE_END 0x80000000

/** @brief 默认时间片（10ms） */
#define DEFAULT_TIME_SLICE 10

/** @brief 默认优先级 */
#define DEFAULT_PRIORITY 10

/** @brief 工作目录路径最大长度 */
#define MAX_CWD_LENGTH 256

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
 * 架构相关：i686 使用 32 位寄存器，x86_64 使用 64 位寄存器
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
    
    /* 文件系统 */
    fd_table_t *fd_table;            ///< 文件描述符表
    char cwd[MAX_CWD_LENGTH];        ///< 当前工作目录
    
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
} task_t;

/* ============================================================================
 * 全局变量声明
 * ========================================================================== */

/** @brief 任务控制块池（在 task.c 中定义） */
extern task_t task_pool[MAX_TASKS];

/* ============================================================================
 * 核心函数声明
 * ========================================================================== */

/**
 * @brief 初始化任务管理系统
 * 
 * 创建 idle 任务，初始化调度器
 */
void task_init(void);

/**
 * @brief 创建内核线程
 * 
 * @param entry 线程入口函数
 * @param name 线程名称
 * @return 成功返回 PID，失败返回 0
 */
uint32_t task_create_kernel_thread(void (*entry)(void), const char *name);

/**
 * @brief 创建用户进程
 * 
 * @param name 进程名称
 * @param entry_point 用户程序入口点
 * @param page_dir 页目录
 * @param program_end 程序加载的最高地址（用于设置堆起始地址）
 * @return 成功返回 PID，失败返回 0
 */
uint32_t task_create_user_process(const char *name, uintptr_t entry_point,
                                   page_directory_t *page_dir, uintptr_t program_end);

/**
 * @brief 退出当前任务
 * 
 * @param exit_code 退出码
 * @note 此函数不会返回
 */
void task_exit(uint32_t exit_code) __attribute__((noreturn));

/**
 * @brief 主动让出 CPU（切换到其他任务）
 */
void task_yield(void);

/**
 * @brief 任务睡眠指定时间
 * 
 * @param ms 睡眠时间（毫秒）
 */
void task_sleep(uint32_t ms);

/**
 * @brief 阻塞当前任务
 * 
 * @param wait_object 等待对象指针（用于调试）
 */
void task_block(void *wait_object);

/**
 * @brief 唤醒等待在指定对象上的一个任务
 * 
 * @param wait_object 等待对象指针（用于调试）
 */
void task_wakeup(void *wait_object);

/**
 * @brief 从中断上下文调度
 * 
 * @param regs 中断寄存器状态
 */
void schedule_from_irq(void *regs);

/**
 * @brief 获取当前正在运行的任务
 * 
 * @return 当前任务指针，如果没有则返回 NULL
 */
task_t* task_get_current(void);

/**
 * @brief 根据 PID 查找任务
 * 
 * @param pid 进程 ID
 * @return 任务指针，如果未找到则返回 NULL
 */
task_t* task_get_by_pid(uint32_t pid);

/**
 * @brief 任务调度器
 * 
 * 选择下一个任务并切换上下文
 * 通常由定时器中断或主动让出时调用
 */
void task_schedule(void);

/**
 * @brief 定时器中断处理
 * 
 * 更新任务运行时间，处理睡眠任务，触发调度
 * 由定时器驱动调用
 */
void task_timer_tick(void);

/* ============================================================================
 * 辅助函数
 * ========================================================================== */

/**
 * @brief 分配一个空闲的任务控制块
 * 
 * @return 任务指针，如果没有空闲 PCB 则返回 NULL
 */
task_t* task_alloc(void);

/**
 * @brief 释放任务控制块
 * 
 * @param task 任务指针
 */
void task_free(task_t *task);

/**
 * @brief 设置用户栈
 * 
 * 为用户进程分配并映射用户栈空间
 * 
 * @param task 任务指针
 * @return 成功返回 true，失败返回 false
 */
bool task_setup_user_stack(task_t *task);

/**
 * @brief 将任务添加到就绪队列
 * 
 * @param task 任务指针
 */
void ready_queue_add(task_t *task);

/**
 * @brief 从就绪队列移除任务
 * 
 * @param task 任务指针
 */
void ready_queue_remove(task_t *task);

/**
 * @brief 打印所有任务信息（用于调试）
 */
void task_print_all(void);

/**
 * @brief 获取系统中的任务数量
 * 
 * @return 活动任务数量
 */
uint32_t task_get_count(void);

/* ============================================================================
 * 汇编函数声明（在 task_asm.asm 中实现）
 * ========================================================================== */

/**
 * @brief 执行上下文切换
 * 
 * @param old_ctx 保存旧任务上下文的地址
 * @param new_ctx 新任务上下文的地址
 */
extern void task_switch_context(cpu_context_t **old_ctx, cpu_context_t *new_ctx);

/**
 * @brief 首次进入任务（用于内核线程）
 * 
 * 用于第一次启动内核线程
 */
extern void task_enter_kernel_thread(void);

/* ============================================================================
 * 测试辅助函数（仅在测试模式下使用）
 * ========================================================================== */

/**
 * @brief 检查是否应该使栈页分配失败（用于测试）
 * 
 * @param page_index 页索引
 * @return 如果应该失败返回 true
 */
bool task_should_fail_stack_page(uint32_t page_index);

#endif // _KERNEL_TASK_H_

