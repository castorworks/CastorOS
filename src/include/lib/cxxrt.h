/**
 * @file cxxrt.h
 * @brief 内核 C++ 运行时支持
 *
 * 内核以 freestanding C++ 构建（-fno-exceptions -fno-rtti），没有 libstdc++/libsupc++。
 * 这里提供语言本身需要的最小运行时：
 *   - 全局对象构造函数的调用（.init_array / .ctors）
 *   - operator new / delete（基于内核堆 kmalloc / kfree）
 *   - placement new
 */

#ifndef _LIB_CXXRT_H_
#define _LIB_CXXRT_H_

#include <types.h>

/**
 * @brief 调用所有全局/静态对象的构造函数
 *
 * 必须在 kernel_main 最开始调用一次（BSS 已由引导代码清零之后）。
 * 注意：此时堆尚未初始化，全局对象的构造函数中不能分配内存。
 */
void cxx_global_ctors_init(void);

/* placement new（标准形式，不分配内存） */
inline void *operator new(__SIZE_TYPE__, void *ptr) noexcept { return ptr; }
inline void *operator new[](__SIZE_TYPE__, void *ptr) noexcept { return ptr; }
inline void operator delete(void *, void *) noexcept {}
inline void operator delete[](void *, void *) noexcept {}

#endif // _LIB_CXXRT_H_
