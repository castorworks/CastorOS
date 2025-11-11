#ifndef _DRIVERS_SERIAL_H_
#define _DRIVERS_SERIAL_H_

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
void serial_init(void);

/**
 * 通过串口输出一个字符
 * @param c 要输出的字符
 */
void serial_putchar(char c);

/**
 * 通过串口输出字符串
 * @param msg 要输出的字符串，以 null 结尾
 */
void serial_print(const char *msg);

#endif /* _DRIVERS_SERIAL_H_ */
