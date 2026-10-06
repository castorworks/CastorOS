#ifndef _KERNEL_HW_ACCESS_H_
#define _KERNEL_HW_ACCESS_H_

#include <types.h>

/**
 * @file hw_access.h
 * @brief 按设备授权：一个进程碰得到哪些硬件
 *
 * 有特权的进程（init）什么硬件都能碰。它启动驱动时，在放弃特权之前用 hw_allow 把
 * 这个驱动的设备占用的资源（I/O 端口、设备内存、中断线）一条条记进自己的许可表，
 * 然后 drop_privilege：特权没有了，许可表留着，fork 和 exec 都保留。没有特权的进程
 * 只能碰许可表里有的东西，也不能再往表里加。
 *
 * 表里有什么，进程自己查得到（hw_allowed）：驱动据此得知自己的设备在哪里，
 * 不用自己去找，也不用把地址写死。
 */

/* 资源的种类（与 user/lib/include/syscall.h 保持一致） */
#define HW_PORTS        0       /**< x86 的 I/O 端口 [start, start + count) */
#define HW_MEMORY       1       /**< 设备内存 [start, start + count)，单位是字节 */
#define HW_IRQ          2       /**< 中断线 [start, start + count) */

/** 一个进程最多持有多少条许可 */
#define HW_ALLOW_MAX    8

/** 一条许可（内核和用户库各有一份定义，必须一致） */
struct hw_range {
    uint32_t kind;
    uint32_t reserved;
    uint64_t start;
    uint64_t count;
};

struct task;

namespace kernel {

class HwAccess {
public:
    /**
     * 往 task 的许可表里加一条。已经被表里的某一条盖住的不重复记。
     * 调用者负责检查权限（只有特权进程可以加）。
     * @return 种类不认识、范围为空或越界、表满时返回 false
     */
    static bool allow(struct task *task, uint32_t kind, uint64_t start, uint64_t count);

    /**
     * task 的许可表是否盖住了 [start, start + count)。
     * 设备内存按页算：映射的最小单位是一页，许可了一页里的一部分就等于许可了整页。
     */
    static bool covers(const struct task *task, uint32_t kind, uint64_t start, uint64_t count);

    /** 当前进程可不可以碰这段资源：有特权，或者许可表盖住了它 */
    static bool current_may(uint32_t kind, uint64_t start, uint64_t count);

    /** 当前进程是不是驱动（有特权，或者持有任何一条许可）：DMA 内存只给驱动 */
    static bool current_is_driver();
};

} // namespace kernel

#endif // _KERNEL_HW_ACCESS_H_
