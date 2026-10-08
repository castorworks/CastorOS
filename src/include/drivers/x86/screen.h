#ifndef _DRIVERS_X86_SCREEN_H_
#define _DRIVERS_X86_SCREEN_H_

namespace drivers {

/**
 * @brief 屏幕（x86: VGA 文本模式，80x25）
 *
 * 内核的控制台输出除了写串口，也写到这里：没有串口的机器上，这是唯一能看到的输出。
 * 只管输出；键盘是用户态的驱动。
 *
 * 没有自己的锁：只有 kprintf.cpp 调用，调用时已经独占控制台（ConsoleGuard）。
 */
class Screen {
public:
    /** 清屏，光标回到左上角 */
    static void init();

    /**
     * 在光标处输出一个字符，光标后移，写满一屏就向上滚一行。
     * 认识 \n \r \b \t，以及三个转义序列：改颜色（ESC [ ... m，klog 用它给日志上色）、
     * 移动光标（ESC [ 行;列 H）和清屏（ESC [ 2 J）；别的转义序列整个丢掉。
     */
    static void putchar(char c);
};

} // namespace drivers

#endif /* _DRIVERS_X86_SCREEN_H_ */
