#ifndef _USERLAND_LIB_STDIO_H_
#define _USERLAND_LIB_STDIO_H_

#include <syscall.h>
#include <types.h>
#include <libgcc_stub.h>

// 标准输入和标准输出
//
// 默认是控制台（输出走内核的 console_write，输入来自 uart 驱动）。命令行可以把它们
// 换成文件（cmd < in > out 2> err）或者另一个进程（cmd1 | cmd2）：它在 exec 的参数最后
// 多放一个以 STDIO_ARG_MARK 开头的参数，启动代码把它摘下来交给 stdio_setup。格式是
//   "\x01" 输入 "\x01" 输出 [ "\x01" 错误输出 ]
// 各是：空 = 控制台，"f<文件名>" = 文件，"a<文件名>" = 追加到文件（仅输出），
// "p<pid>" = 管道另一头的进程（错误输出不接管道）。
//
// 标准错误是给人看的话：用法、报错。它和标准输出分开，这样 cmd > file 或者
// cmd1 | cmd2 的时候报错仍然出现在屏幕上，不会混进数据里。
//
// 管道没有缓冲区，直接用同步 IPC：写的一方 ipc_send 一条 STDIO_DATA 消息
// （data[0] = 字节数，内容在 data[1] 开始的内存里），读的一方 ipc_recv 指定对方的 PID。
// 写的一方退出后读的一方读到"输入结束"；读的一方退出后写的一方也跟着退出。
#define STDIO_ARG_MARK  '\x01'
#define STDIO_DATA      0x53544449      // 管道消息的 label
#define STDIO_DATA_MAX  40              // 一条管道消息最多带的字节数

/** 启动代码调用：按命令行给的说明设置标准输入/输出。文件打不开时报错并退出 */
void stdio_setup(const char *spec);

/** 写标准输出。@return 写出的字节数，失败返回 -1 */
long write_out(const void *buf, size_t len);

/**
 * 读标准输入，最多 len 字节。来自键盘时按行给（带回显和退格，含结尾的换行符）。
 * @return 读到的字节数；0 = 输入结束
 */
long read_input(char *buf, size_t len);

/**
 * 从标准输入读一行。结果不含换行符，以 '\0' 结尾；比 size 长的行分几次给。
 * @return 这一行的长度；-1 = 输入结束
 */
long read_line(char *buf, size_t size);

/** 写标准错误。@return 写出的字节数，失败返回 -1 */
long write_err(const void *buf, size_t len);

/**
 * 格式化后写到标准输出。格式符: %s %c %d %i %u %x %X %o %p %f %%，可以带 l / ll；
 * 标志 -（左对齐）和 0（用 0 填充），宽度（%5d），%f 的精度（%.2f，默认 6 位）
 */
void printf(const char *format, ...);
/** 格式化后写到标准错误：用法说明和报错用这个 */
void eprintf(const char *format, ...);
/** 把字符串写到标准输出 */
void print(const char *msg);

/** 格式化进 str（最多 size 字节，含结尾的 '\0'）。@return 写进去的字符数，放不下的部分被截掉 */
int snprintf(char *str, size_t size, const char *format, ...);

#endif /* _USERLAND_LIB_STDIO_H_ */

