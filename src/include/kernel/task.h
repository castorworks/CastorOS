#ifndef _KERNEL_TASK_H_
#define _KERNEL_TASK_H_

#include <types.h>
#include <mm/vmm.h>

/**
 * 进程管理
 * 
 * 实现多任务支持，包括进程创建、销毁、调度等
 */

/* 最大进程数 */
#define MAX_TASKS 256

/* 内核栈大小（8KB） */
#define KERNEL_STACK_SIZE 8192

/* 进程状态 */
typedef enum {
    TASK_UNUSED = 0,    // 未使用（PCB 空闲）
    TASK_READY,         // 就绪（等待调度）
    TASK_RUNNING,       // 运行中
    TASK_BLOCKED,       // 阻塞（等待事件）
    TASK_TERMINATED     // 已终止（等待回收）
} task_state_t;

/* CPU 上下文（用于上下文切换） */
typedef struct {
    /* 通用寄存器 */
    uint32_t eax, ebx, ecx, edx;  // 通用寄存器
    uint32_t esi, edi, ebp;       // 索引和基址寄存器
    uint32_t esp;                 // 栈指针
    uint32_t eip;                 // 指令指针
    uint32_t eflags;              // 标志寄存器
    uint32_t cr3;                 // 页目录基址寄存器
    uint16_t cs, ds, es, fs, gs, ss;  // 段寄存器
} __attribute__((packed)) cpu_context_t;

/* 进程控制块（PCB） */
typedef struct task {
    uint32_t pid;                 // 进程 ID
    char name[32];                // 进程名称
    task_state_t state;           // 进程状态
    
    cpu_context_t context;        // CPU 上下文
    
    uint32_t kernel_stack;        // 内核栈顶地址
    uint32_t kernel_stack_base;   // 内核栈底地址
    
    page_directory_t *page_dir;   // 页目录（虚拟地址）
    uint32_t page_dir_phys;       // 页目录（物理地址，用于加载到 CR3）
    
    uint32_t priority;            // 优先级（0-255，数字越大优先级越高）
    uint32_t time_slice;          // 时间片（剩余 ticks）
    uint64_t total_runtime;       // 总运行时间（ticks）
    
    struct task *next;            // 链表指针（用于就绪队列等）
    struct task *parent;          // 父进程
    struct task *child;           // 第一个子进程
    struct task *sibling;         // 兄弟进程
    
    uint32_t exit_code;           // 退出码
    
    void *wait_channel;           // 等待通道（用于阻塞）
} task_t;

/**
 * 初始化任务管理系统
 */
void task_init(void);

/**
 * 创建内核线程
 * @param entry 入口函数
 * @param name 线程名称
 * @return 进程 PID，失败返回 0
 */
uint32_t task_create_kernel_thread(void (*entry)(void), const char *name);

/**
 * 获取当前任务
 * @return 当前任务的 PCB 指针
 */
task_t *task_get_current(void);

/**
 * 根据 PID 获取任务
 * @param pid 进程 ID
 * @return 任务的 PCB 指针，如果未找到返回 NULL
 */
task_t *task_get_by_pid(uint32_t pid);

/**
 * 调度器（选择下一个任务）
 * 从就绪队列中选择下一个任务
 */
void task_schedule(void);

/**
 * 主动让出 CPU（系统调用）
 */
void task_yield(void);

/**
 * 退出当前任务（系统调用）
 * @param exit_code 退出码
 */
void task_exit(uint32_t exit_code) __attribute__((noreturn));

/**
 * 睡眠指定毫秒数（系统调用）
 * @param ms 睡眠时间（毫秒）
 */
void task_sleep(uint32_t ms);

/**
 * 唤醒等待特定通道的所有任务
 * @param channel 等待通道
 */
void task_wakeup(void *channel);

/**
 * 阻塞当前任务，等待通道
 * @param channel 等待通道
 */
void task_block(void *channel);

/**
 * 定时器回调（由 PIT 中断调用）
 * 更新时间片，触发调度
 */
void task_timer_tick(void);

/**
 * 切换到指定任务（汇编实现）
 * @param current 当前任务 PCB 地址
 * @param next 下一个任务 PCB 地址
 */
extern void task_switch(task_t *current, task_t *next);

/**
 * 分配一个空闲的 PCB
 * @return PCB 指针，失败返回 NULL
 * 注意：这是内部函数，供 fork() 等系统调用使用
 */
task_t *task_alloc(void);

/**
 * 释放 PCB
 * @param task 任务 PCB 指针
 * 注意：这是内部函数，供系统调用使用
 */
void task_free(task_t *task);

/**
 * 将任务添加到就绪队列
 * @param task 任务 PCB 指针
 * 注意：这是内部函数，供 fork() 等系统调用使用
 */
void ready_queue_add(task_t *task);

#endif // _KERNEL_TASK_H_
