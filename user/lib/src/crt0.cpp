/**
 * @file crt0.cpp
 * @brief 用户程序 C++ 启动代码
 *
 * 内核把控制权交给 _start（链接脚本 ENTRY(_start)）。这里先运行全局/静态对象的
 * 构造函数，从参数页取出 argv，再调用程序的 main(argc, argv)，最后用它的返回值
 * 调用 exit()。
 */

#include <syscall.h>

typedef void (*ctor_func_t)(void);

/* 由用户程序链接脚本提供 */
extern "C" ctor_func_t __init_array_start[];
extern "C" ctor_func_t __init_array_end[];
extern "C" ctor_func_t __ctors_start[];
extern "C" ctor_func_t __ctors_end[];

int main(int argc, char **argv);

extern "C" void _start(void);

// 参数页：内核在 exec 时把参数块放在用户栈区域最顶上的一页
// （与内核 src/include/kernel/task.h 里的 USER_ARGS_ADDR / user_args_t 一致）
#if defined(ARCH_ARM64)
#define USER_ARGS_ADDR  (0x00007FFFFF000000ULL - 4096)
#else
#define USER_ARGS_ADDR  (0x80000000UL - 4096)
#endif
#define USER_ARGS_MAX   (4096 - sizeof(uint32_t))

struct user_args {
    uint32_t length;                // data 里有效的字节数
    char data[USER_ARGS_MAX];       // "arg0\0arg1\0...argN\0"
};

#define ARGV_MAX 64
static char *argv_storage[ARGV_MAX + 1];

/** 把参数块切成 argv 数组，返回 argc */
static int build_argv(void) {
    struct user_args *args = (struct user_args *)USER_ARGS_ADDR;
    uint32_t length = args->length <= USER_ARGS_MAX ? args->length : 0;
    int argc = 0;
    uint32_t pos = 0;
    while (pos < length && argc < ARGV_MAX) {
        argv_storage[argc++] = &args->data[pos];
        while (pos < length && args->data[pos] != '\0') {
            pos++;
        }
        pos++;      // 跳过 NUL
    }
    argv_storage[argc] = nullptr;
    return argc;
}

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

    int argc = build_argv();
    exit(main(argc, argv_storage));
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
