#ifndef _KERNEL_PANIC_H_
#define _KERNEL_PANIC_H_

/**
 * 内核 Panic
 * 显示错误信息并挂起系统
 */
void kernel_panic(const char* message, const char* file, int line);

/**
 * 断言宏
 */
#define ASSERT(condition) \
    do { \
        if (!(condition)) { \
            kernel_panic("Assertion failed: " #condition, __FILE__, __LINE__); \
        } \
    } while(0)

/**
 * Panic 宏
 */
#define PANIC(msg) kernel_panic(msg, __FILE__, __LINE__)

#endif // _KERNEL_PANIC_H_

