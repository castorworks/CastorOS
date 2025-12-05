/**
 * @file sys/select.h
 * @brief I/O 多路复用
 * 
 * 符合 POSIX.1-2008 标准
 */

#ifndef _SYS_SELECT_H_
#define _SYS_SELECT_H_

#include <types.h>

// ============================================================================
// fd_set 定义
// ============================================================================

#define FD_SETSIZE      64      // 最大文件描述符数

typedef struct {
    uint32_t fds_bits[FD_SETSIZE / 32];
} fd_set;

#define _FD_BITS(fd)    ((fd) / 32)
#define _FD_MASK(fd)    (1U << ((fd) % 32))

#define FD_SET(fd, set)     ((set)->fds_bits[_FD_BITS(fd)] |= _FD_MASK(fd))
#define FD_CLR(fd, set)     ((set)->fds_bits[_FD_BITS(fd)] &= ~_FD_MASK(fd))
#define FD_ISSET(fd, set)   (((set)->fds_bits[_FD_BITS(fd)] & _FD_MASK(fd)) != 0)
#define FD_ZERO(set)        do { \
    for (int _i = 0; _i < (int)(FD_SETSIZE / 32); _i++) \
        (set)->fds_bits[_i] = 0; \
} while(0)

// ============================================================================
// timeval 结构
// ============================================================================

struct timeval {
    long tv_sec;        // 秒
    long tv_usec;       // 微秒
};

// ============================================================================
// select 函数
// ============================================================================

/**
 * @brief I/O 多路复用
 * @param nfds 最大文件描述符 + 1
 * @param readfds 读集合（输入/输出）
 * @param writefds 写集合（输入/输出）
 * @param exceptfds 异常集合（输入/输出）
 * @param timeout 超时时间（NULL 表示无限等待）
 * @return 就绪的描述符数量，0 超时，-1 错误
 */
int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout);

/**
 * @brief pselect（带信号掩码的 select）
 */
int pselect(int nfds, fd_set *readfds, fd_set *writefds,
            fd_set *exceptfds, const struct timespec *timeout,
            const void *sigmask);

#endif // _SYS_SELECT_H_
