/**
 * @file errno.h
 * @brief 错误码定义
 * 
 * 符合 POSIX.1-2008 标准
 */

#ifndef _ERRNO_H_
#define _ERRNO_H_

// ============================================================================
// errno 变量
// ============================================================================

extern int errno;

// ============================================================================
// 错误码
// ============================================================================

#define EPERM           1       // 操作不允许
#define ENOENT          2       // 文件或目录不存在
#define ESRCH           3       // 进程不存在
#define EINTR           4       // 系统调用被中断
#define EIO             5       // I/O 错误
#define ENXIO           6       // 设备不存在
#define E2BIG           7       // 参数列表太长
#define ENOEXEC         8       // 执行格式错误
#define EBADF           9       // 坏的文件描述符
#define ECHILD          10      // 没有子进程
#define EAGAIN          11      // 资源暂时不可用
#define EWOULDBLOCK     EAGAIN  // 操作会阻塞
#define ENOMEM          12      // 内存不足
#define EACCES          13      // 权限不足
#define EFAULT          14      // 地址错误
#define ENOTBLK         15      // 不是块设备
#define EBUSY           16      // 设备或资源忙
#define EEXIST          17      // 文件已存在
#define EXDEV           18      // 跨设备链接
#define ENODEV          19      // 设备不存在
#define ENOTDIR         20      // 不是目录
#define EISDIR          21      // 是目录
#define EINVAL          22      // 无效参数
#define ENFILE          23      // 系统文件表溢出
#define EMFILE          24      // 打开文件过多
#define ENOTTY          25      // 不是终端
#define ETXTBSY         26      // 文本文件忙
#define EFBIG           27      // 文件太大
#define ENOSPC          28      // 空间不足
#define ESPIPE          29      // 非法 seek
#define EROFS           30      // 只读文件系统
#define EMLINK          31      // 链接过多
#define EPIPE           32      // 管道破裂
#define EDOM            33      // 数学参数超出范围
#define ERANGE          34      // 结果太大

// 网络相关错误
#define EADDRINUSE      98      // 地址已在使用
#define EADDRNOTAVAIL   99      // 地址不可用
#define ENETDOWN        100     // 网络已关闭
#define ENETUNREACH     101     // 网络不可达
#define ENETRESET       102     // 网络连接被重置
#define ECONNABORTED    103     // 连接中止
#define ECONNRESET      104     // 连接被对端重置
#define ENOBUFS         105     // 缓冲空间不足
#define EISCONN         106     // 已连接
#define ENOTCONN        107     // 未连接
#define ESHUTDOWN       108     // 传输端点已关闭
#define ETIMEDOUT       110     // 连接超时
#define ECONNREFUSED    111     // 连接被拒绝
#define EHOSTDOWN       112     // 主机已关闭
#define EHOSTUNREACH    113     // 主机不可达
#define EALREADY        114     // 操作已在进行
#define EINPROGRESS     115     // 操作正在进行

// 文件系统相关
#define ENOTEMPTY       39      // 目录非空
#define ENAMETOOLONG    36      // 文件名太长
#define ELOOP           40      // 符号链接层数过多
#define ENOSYS          38      // 功能未实现

// ============================================================================
// 辅助函数
// ============================================================================

/**
 * @brief 获取错误描述字符串
 * @param errnum 错误码
 * @return 错误描述
 */
char *strerror(int errnum);

/**
 * @brief 打印错误信息
 * @param s 前缀字符串
 */
void perror(const char *s);

#endif // _ERRNO_H_

