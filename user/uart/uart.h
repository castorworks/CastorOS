#ifndef _UART_H_
#define _UART_H_

// uart 驱动的 IPC 协议

enum {
    /**
     * 读取控制台输入。请求的 data[0] 是最多等多少毫秒（0 = 一直等到有输入）。
     * 应答: label = UART_READ, data[0] = 字节数 n (0..UART_READ_MAX)，
     *       字节依次放在 data[1] 开始的内存里；n == 0 表示超时了，没有输入
     * 同一时刻只能有一个读者；已有读者在等时应答 data[0] = 0。
     */
    UART_READ = 1,
};

#define UART_READ_MAX   32

#endif // _UART_H_
