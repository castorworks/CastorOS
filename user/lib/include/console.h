#ifndef _USERLAND_LIB_CONSOLE_H_
#define _USERLAND_LIB_CONSOLE_H_

// 控制台输入：uart 驱动的 IPC 协议，以及程序读键盘输入用的函数。
//
// 驱动把输入分给两个读者。"终端的主人"是命令行：它用 UART_ATTACH 登记自己，
// 再用 UART_SET_FOREGROUND 告诉驱动哪个进程在前台。有前台进程时，输入归前台进程，
// 只有 Ctrl-C 仍然交给主人（由它决定终止谁）；没有前台进程时，输入都归主人。
// 其他进程（比如后台任务）读不到输入。

#include <types.h>

enum {
    /**
     * 读取输入。请求的 data[0] 是最多等多少毫秒（0 = 一直等到有输入）。
     * 应答: data[0] = 字节数 n (1..UART_READ_MAX)，字节依次放在 data[1] 开始的内存里；
     *       n == 0 表示超时了，没有输入；n == -1 表示调用者既不是主人也不是前台进程
     */
    UART_READ = 1,
    /** 登记为终端的主人（还没有主人，或者原来的主人已经退出时才行）。应答: data[0] = 0 / -1 */
    UART_ATTACH = 2,
    /**
     * 主人专用：data[0] = 前台进程的 PID，0 表示没有前台进程。
     * 设置时，主人还没读走的输入转给前台进程；清除时，前台进程没读完的输入还给主人。
     * 应答: data[0] = 0 / -1
     */
    UART_SET_FOREGROUND = 3,
    /**
     * 主人专用：把已经读走的输入退回去，留给接下来的前台进程（Ctrl-C 仍然留给主人）。
     * data[0] = 字节数 (<= UART_READ_MAX)，字节在 data[1] 开始的内存里。应答: data[0] = 0 / -1
     */
    UART_UNREAD = 4,
};

#define UART_READ_MAX   32

#define CONSOLE_CTRL_C  0x03
#define CONSOLE_CTRL_D  0x04    // 行首的 Ctrl-D：输入结束

/**
 * 读键盘输入（不回显、不按行）。最多等 timeout_ms 毫秒，0 表示一直等。
 * @return 读到的字节数；0 = 超时；-1 = 没有输入可读（没有驱动，或者不在前台）
 */
long console_read(char *buf, size_t len, uint32_t timeout_ms);

/**
 * 读一行：回显，支持退格，回车结束。结果不含换行符，以 '\0' 结尾。
 * @return 这一行的长度；-1 = 输入结束（行首的 Ctrl-D，或者没有输入可读）
 */
long read_line(char *buf, size_t size);

#endif // _USERLAND_LIB_CONSOLE_H_
