#ifndef _KERNEL_UACCESS_H_
#define _KERNEL_UACCESS_H_

#include <types.h>

/**
 * @file uaccess.h
 * @brief 用户指针校验
 *
 * 系统调用收到的地址、长度都来自用户态，使用前必须确认整个区间落在
 * 当前进程的用户地址空间内，并且每一页都以用户权限映射。
 * 校验失败时系统调用应返回 -EFAULT，而不是去解引用。
 */

namespace kernel {

class UAccess {
public:
    /** 用户空间上界（不含）。 */
    static uintptr_t user_end();

    /** [addr, addr+len) 是否全部可被用户态读取。len 为 0 时恒为真。 */
    static bool can_read(const void *addr, size_t len);

    /**
     * [addr, addr+len) 是否全部可被用户态写入。
     * 写时复制的页会在这里先完成复制，内核随后的写入不会再触发缺页。
     */
    static bool can_write(void *addr, size_t len);

};

} // namespace kernel

#endif // _KERNEL_UACCESS_H_
