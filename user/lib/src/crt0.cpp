/**
 * @file crt0.cpp
 * @brief 用户程序 C++ 启动代码
 *
 * 内核把控制权交给 _start（链接脚本 ENTRY(_start)）。这里先运行全局/静态对象的
 * 构造函数，再调用程序的 main()，最后用它的返回值调用 exit()。
 */

#include <unistd.h>

typedef void (*ctor_func_t)(void);

/* 由用户程序链接脚本提供 */
extern "C" ctor_func_t __init_array_start[];
extern "C" ctor_func_t __init_array_end[];
extern "C" ctor_func_t __ctors_start[];
extern "C" ctor_func_t __ctors_end[];

int main();

extern "C" void _start(void);

void _start(void) {
    /* .init_array（aarch64-elf）：按地址升序执行 */
    for (ctor_func_t *fn = __init_array_start; fn < __init_array_end; fn++) {
        (*fn)();
    }

    /* .ctors（i686-elf / x86_64-elf）：按约定逆序执行 */
    for (ctor_func_t *fn = __ctors_end; fn > __ctors_start; ) {
        fn--;
        (*fn)();
    }

    exit(main());
}

extern "C" {

/* 纯虚函数被调用：程序 bug，直接终止 */
void __cxa_pure_virtual(void) {
    exit(127);
}

/* 进程退出时不运行全局对象的析构函数 */
void *__dso_handle = nullptr;

int __cxa_atexit(void (*)(void *), void *, void *) {
    return 0;
}

} // extern "C"
