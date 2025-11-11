#include <drivers/serial.h>
#include <kernel/io.h>

/* COM1 串口基址 */
#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);  // 禁用中断
    outb(COM1 + 3, 0x80);  // 启用 DLAB（Divisor Latch Access Bit）
    outb(COM1 + 0, 0x03);  // 波特率 38400（低字节）
    outb(COM1 + 1, 0x00);  // 波特率 38400（高字节）
    outb(COM1 + 3, 0x03);  // 8 位数据，无校验，1 停止位
    outb(COM1 + 2, 0xC7);  // 启用 FIFO，清空缓冲区，14 字节阈值
    outb(COM1 + 4, 0x0B);  // 启用 IRQ，设置 RTS/DSR
}

void serial_putchar(char c) {
    /* 等待传输缓冲区为空 */
    while ((inb(COM1 + 5) & 0x20) == 0);
    outb(COM1, c);
}

void serial_print(const char *msg) {
    while (*msg) {
        /* 自动处理换行符，添加回车符 */
        if (*msg == '\n') {
            serial_putchar('\r');
        }
        serial_putchar(*msg++);
    }
}
