#ifndef _DRIVERS_X86_SERIAL_H_
#define _DRIVERS_X86_SERIAL_H_

namespace drivers {

/**
 * @brief 串口驱动（x86: 16550 UART；ARM64: PL011）
 */
class Serial {
public:
    /**
     * 串口驱动
     * 
     * 提供串口通信功能，用于内核调试和日志输出
     * 使用 COM1 (0x3F8) 作为默认串口
     */

    /**
     * 初始化串口
     * 配置波特率为 38400，8位数据位，无校验，1停止位
     */
    static void init();

    /**
     * 通过串口输出一个字符
     * @param c 要输出的字符
     */
    static void putchar(char c);

    /**
     * 通过串口输出字符串
     * @param msg 要输出的字符串，以 null 结尾
     */
    static void print(const char *msg);

    /**
     * 非阻塞读取一个字符（轮询 LSR）
     * @return 读到的字符，没有数据时返回 -1
     */
    static int getchar_nonblock();
};

} // namespace drivers

#endif /* _DRIVERS_X86_SERIAL_H_ */
