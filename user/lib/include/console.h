#ifndef _USERLAND_LIB_CONSOLE_H_
#define _USERLAND_LIB_CONSOLE_H_

// 控制台输入：console 服务的 IPC 协议，以及程序读键盘输入用的函数。
//
// 输入有两个来源：串口，和 PC 上的键盘。它们各有自己的驱动（uart，键盘是 kbd 和 usbkbd），收到的字符用
// CONSOLE_INPUT 交给 console，从那里起不分来源。console 自己不碰硬件。
//
// console 把输入分给两个读者。"终端的主人"是命令行：它用 CONSOLE_ATTACH 登记自己，
// 再用 CONSOLE_SET_FOREGROUND 告诉它哪个进程在前台。有前台进程时，输入归前台进程，
// 只有 Ctrl-C 仍然交给主人（由它决定终止谁）；没有前台进程时，输入都归主人。
// 其他进程（比如后台任务）读不到输入。

#include <types.h>

enum {
    /**
     * 读取输入。请求的 data[0] 是最多等多少毫秒（0 = 一直等到有输入）。
     * 应答: data[0] = 字节数 n (1..CONSOLE_READ_MAX)，字节依次放在 data[1] 开始的内存里；
     *       n == 0 表示超时了，没有输入；n == -1 表示调用者既不是主人也不是前台进程
     */
    CONSOLE_READ = 1,
    /** 登记为终端的主人（还没有主人，或者原来的主人已经退出时才行）。应答: data[0] = 0 / -1 */
    CONSOLE_ATTACH = 2,
    /**
     * 主人专用：data[0] = 前台进程的 PID，0 表示没有前台进程。
     * 设置时，主人还没读走的输入转给前台进程；清除时，前台进程没读完的输入还给主人。
     * 应答: data[0] = 0 / -1
     */
    CONSOLE_SET_FOREGROUND = 3,
    /**
     * 主人专用：把已经读走的输入退回去，留给接下来的前台进程（Ctrl-C 仍然留给主人）。
     * data[0] = 字节数 (<= CONSOLE_READ_MAX)，字节在 data[1] 开始的内存里。应答: data[0] = 0 / -1
     */
    CONSOLE_UNREAD = 4,
    /**
     * 输入设备的驱动专用（登记了 UART_NAME、KBD_NAME 或 USBKBD_NAME 的进程）：设备收到的字符。
     * data[0] = 字节数 (<= CONSOLE_READ_MAX)，字节在 data[1] 开始的内存里。应答: data[0] = 0 / -1
     */
    CONSOLE_INPUT = 5,
    /**
     * 调试：收到的进程应答之后立刻退出（像崩溃了一样），用来验证 init 会重启它、别的进程会
     * 重新找到它。console 和输入设备的驱动都认这个请求。应答: data[0] = 0
     */
    CONSOLE_DEBUG_EXIT = 6,
};

/** console 服务登记的名字 */
#define CONSOLE_SERVICE_NAME "console"
/** 串口驱动、PS/2 键盘驱动、USB 键盘驱动登记的名字。它们不提供服务，登记是为了让 console 认得它们 */
#define UART_NAME       "uart"
#define KBD_NAME        "kbd"
#define USBKBD_NAME     "usbkbd"

#define CONSOLE_READ_MAX   32

#define CONSOLE_CTRL_C  0x03
#define CONSOLE_CTRL_D  0x04    // 行首的 Ctrl-D：输入结束

/**
 * 读键盘输入（不回显、不按行）。最多等 timeout_ms 毫秒，0 表示一直等。
 * @return 读到的字节数；0 = 超时；-1 = 没有输入可读（没有驱动，或者不在前台）
 */
long console_read(char *buf, size_t len, uint32_t timeout_ms);

/**
 * 从键盘读一行：回显，可以移动光标修改（见 <lineedit.h>），回车结束。结果不含换行符，以 '\0' 结尾。
 * 程序一般不直接用它，而是用 <stdio.h> 的 read_line / read_input（标准输入可能被重定向）。
 * @return 这一行的长度；-1 = 输入结束（行首的 Ctrl-D，或者没有输入可读）
 */
long console_read_line(char *buf, size_t size);

/**
 * 输入设备的驱动专用：把设备收到的 n 个字符（n <= CONSOLE_READ_MAX）交给 console。
 * @return 0 成功；-1 = console 不在（正在重启），这几个字符丢了
 */
long console_input(const char *chars, size_t n);

/**
 * 调试：让登记了 name（CONSOLE_SERVICE_NAME 或一个输入设备驱动的名字）的进程退出。
 * @return 0 成功，-1 = 没有这个进程
 */
int console_debug_exit(const char *name);

#endif // _USERLAND_LIB_CONSOLE_H_
