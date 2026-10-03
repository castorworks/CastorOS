/**
 * @file cxxrt.cpp
 * @brief 内核 C++ 运行时支持实现
 */

#include <lib/cxxrt.h>
#include <mm/heap.h>
#include <kernel/panic.h>

/* ============================================================================
 * 全局构造函数
 * ========================================================================== */

typedef void (*ctor_func_t)(void);

/* 由链接脚本提供 */
extern "C" ctor_func_t __init_array_start[];
extern "C" ctor_func_t __init_array_end[];
extern "C" ctor_func_t __ctors_start[];
extern "C" ctor_func_t __ctors_end[];

void cxx_global_ctors_init(void) {
    /* .init_array（aarch64-elf）：按地址升序执行 */
    for (ctor_func_t *fn = __init_array_start; fn < __init_array_end; fn++) {
        (*fn)();
    }

    /* .ctors（i686-elf / x86_64-elf）：按约定逆序执行 */
    for (ctor_func_t *fn = __ctors_end; fn > __ctors_start; ) {
        fn--;
        (*fn)();
    }
}

/* ============================================================================
 * operator new / delete
 * ========================================================================== */

void *operator new(__SIZE_TYPE__ size) {
    return kmalloc(size);
}

void *operator new[](__SIZE_TYPE__ size) {
    return kmalloc(size);
}

void operator delete(void *ptr) noexcept {
    kfree(ptr);
}

void operator delete[](void *ptr) noexcept {
    kfree(ptr);
}

void operator delete(void *ptr, __SIZE_TYPE__) noexcept {
    kfree(ptr);
}

void operator delete[](void *ptr, __SIZE_TYPE__) noexcept {
    kfree(ptr);
}

/* ============================================================================
 * ABI 支持符号
 * ========================================================================== */

extern "C" {

/* 纯虚函数被调用（对象构造/析构期间）—— 属于内核 bug */
void __cxa_pure_virtual(void) {
    PANIC("pure virtual function called");
}

/* 内核永不退出，全局对象的析构函数不需要注册 */
void *__dso_handle = nullptr;

int __cxa_atexit(void (*)(void *), void *, void *) {
    return 0;
}

} // extern "C"
