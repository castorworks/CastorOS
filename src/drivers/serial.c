/**
 * 串口驱动
 * 
 * 同步机制：使用 spinlock + IRQ save 保护串口输出
 * - 防止多核/中断并发输出导致字符交错
 * - 确保日志输出的完整性
 */

#include <drivers/serial.h>
#include <kernel/io.h>
#include <kernel/sync/spinlock.h>

/* COM1 串口基址 */
#define COM1 0x3F8

/* 串口输出锁 */
static spinlock_t serial_lock;

void serial_init(void) {
    spinlock_init(&serial_lock);
    
    outb(COM1 + 1, 0x00);  // 禁用中断
    outb(COM1 + 3, 0x80);  // 启用 DLAB（Divisor Latch Access Bit）
    outb(COM1 + 0, 0x03);  // 波特率 38400（低字节）
    outb(COM1 + 1, 0x00);  // 波特率 38400（高字节）
    outb(COM1 + 3, 0x03);  // 8 位数据，无校验，1 停止位
    outb(COM1 + 2, 0xC7);  // 启用 FIFO，清空缓冲区，14 字节阈值
    outb(COM1 + 4, 0x0B);  // 启用 IRQ，设置 RTS/DSR
}

/**
 * 内部函数：输出单个字符（不加锁）
 */
static void serial_putchar_nolock(char c) {
    /* 等待传输缓冲区为空 */
    while ((inb(COM1 + 5) & 0x20) == 0);
    outb(COM1, c);
}

void serial_putchar(char c) {
    bool irq_state;
    spinlock_lock_irqsave(&serial_lock, &irq_state);
    serial_putchar_nolock(c);
    spinlock_unlock_irqrestore(&serial_lock, irq_state);
}

void serial_print(const char *msg) {
    bool irq_state;
    spinlock_lock_irqsave(&serial_lock, &irq_state);
    
    while (*msg) {
        /* 自动处理换行符，添加回车符 */
        if (*msg == '\n') {
            serial_putchar_nolock('\r');
        }
        serial_putchar_nolock(*msg++);
    }
    
    spinlock_unlock_irqrestore(&serial_lock, irq_state);
}
