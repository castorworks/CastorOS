#ifndef _KERNEL_IO_H_
#define _KERNEL_IO_H_

#include <types.h>

/**
 * 端口 I/O 操作
 * 
 * 这些内联函数用于直接访问 x86 架构的 I/O 端口
 * 仅在 x86 架构 (i686, x86_64) 上可用
 */

#if defined(ARCH_I686) || defined(ARCH_X86_64)

/**
 * 向指定端口输出一个字节
 * @param port 端口号
 * @param val 要输出的字节值
 */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/**
 * 从指定端口读取一个字节
 * @param port 端口号
 * @return 读取到的字节值
 */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * 从指定端口读取一个字
 * @param port 端口号
 * @return 读取到的字值
 */
static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * 向指定端口输出一个字
 * @param port 端口号
 * @param val 要输出的字值
 */
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

/**
 * 从指定端口读取一个双字
 * @param port 端口号
 * @return 读取到的双字值
 */
static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * 向指定端口输出一个双字
 * @param port 端口号
 * @param val 要输出的双字值
 */
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

#endif /* ARCH_I686 || ARCH_X86_64 */

#endif /* _KERNEL_IO_H_ */
