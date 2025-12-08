// ============================================================================
// isr.h - Interrupt Service Routines (i686)
// ============================================================================

#ifndef _ARCH_I686_ISR_H_
#define _ARCH_I686_ISR_H_

#include <types.h>

/**
 * 中断服务例程（ISR）
 * 
 * 处理 CPU 异常（0-31 号中断）
 */

/**
 * 中断寄存器状态结构
 * 保存中断发生时的 CPU 状态
 * 
 * 注意：在 Ring 0 中断时，CPU 只压入 EFLAGS, CS, EIP
 *       在 Ring 3→Ring 0 中断时，CPU 会额外压入 useresp, ss
 *       结构体定义包含所有可能的字段以兼容两种情况
 */
typedef struct {
    uint32_t ds;                                      // 数据段选择子（手动保存）
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;  // 通用寄存器（pusha）
    uint32_t int_no, err_code;                        // 中断号和错误码
    uint32_t eip, cs, eflags, useresp, ss;            // CPU 自动压栈
} registers_t;

/**
 * 中断处理函数类型
 */
typedef void (*isr_handler_t)(registers_t *regs);

/**
 * 初始化 ISR
 * 注册所有 CPU 异常处理程序
 */
void isr_init(void);

/**
 * 注册中断处理函数
 * @param n 中断号
 * @param handler 处理函数
 */
void isr_register_handler(uint8_t n, isr_handler_t handler);

/* CPU 异常（0-31）的汇编入口点 */
extern void isr0(void);   // 除零错误
extern void isr1(void);   // 调试异常
extern void isr2(void);   // 非屏蔽中断
extern void isr3(void);   // 断点
extern void isr4(void);   // 溢出
extern void isr5(void);   // 边界检查
extern void isr6(void);   // 无效操作码
extern void isr7(void);   // 设备不可用
extern void isr8(void);   // 双重故障
extern void isr9(void);   // 协处理器段超限（保留）
extern void isr10(void);  // 无效 TSS
extern void isr11(void);  // 段不存在
extern void isr12(void);  // 栈段错误
extern void isr13(void);  // 一般保护错误
extern void isr14(void);  // 页错误
extern void isr15(void);  // 保留
extern void isr16(void);  // 浮点异常
extern void isr17(void);  // 对齐检查
extern void isr18(void);  // 机器检查
extern void isr19(void);  // SIMD 浮点异常
extern void isr20(void);  // 保留
extern void isr21(void);  // 保留
extern void isr22(void);  // 保留
extern void isr23(void);  // 保留
extern void isr24(void);  // 保留
extern void isr25(void);  // 保留
extern void isr26(void);  // 保留
extern void isr27(void);  // 保留
extern void isr28(void);  // 保留
extern void isr29(void);  // 保留
extern void isr30(void);  // 保留
extern void isr31(void);  // 保留

/**
 * 获取 CR2 寄存器（页错误地址）
 */
 static inline uint32_t get_cr2(void) {
    uint32_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}

/**
 * 解析页错误错误码
 */
typedef struct {
    bool present;      // 0: 页不存在  1: 页保护违规
    bool write;        // 0: 读操作    1: 写操作
    bool user;         // 0: 内核模式  1: 用户模式
    bool reserved;     // 1: 保留位覆盖
    bool instruction;  // 1: 指令获取
} page_fault_info_t;

static inline page_fault_info_t parse_page_fault_error(uint32_t err_code) {
    page_fault_info_t info;
    info.present = (err_code & 0x1) != 0;
    info.write = (err_code & 0x2) != 0;
    info.user = (err_code & 0x4) != 0;
    info.reserved = (err_code & 0x8) != 0;
    info.instruction = (err_code & 0x10) != 0;
    return info;
}

/**
 * 解析一般保护错误码
 */
typedef struct {
    bool external;     // 1: 外部事件
    uint8_t table;     // 0: GDT  1: IDT  2/3: LDT
    uint16_t index;    // 选择子索引
} gpf_info_t;

static inline gpf_info_t parse_gpf_error(uint32_t err_code) {
    gpf_info_t info;
    info.external = (err_code & 0x1) != 0;
    info.table = (err_code >> 1) & 0x3;
    info.index = (err_code >> 3) & 0x1FFF;
    return info;
}

/**
 * 获取特定中断的触发次数
 * @param int_no 中断号
 * @return 触发次数
 */
uint64_t isr_get_interrupt_count(uint8_t int_no);

/**
 * 获取所有中断的总次数
 * @return 总触发次数
 */
uint64_t isr_get_total_interrupt_count(void);

/**
 * 重置中断统计
 */
void isr_reset_interrupt_counts(void);

/**
 * 打印中断统计信息
 */
void isr_print_statistics(void);

#endif // _ARCH_I686_ISR_H_
