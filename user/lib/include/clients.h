#ifndef _USERLAND_LIB_CLIENTS_H_
#define _USERLAND_LIB_CLIENTS_H_

#include <types.h>
#include <syscall.h>

// 服务进程记录“每个客户的共享缓冲区”的小工具。
//
// 文件服务、块设备驱动、网络服务都用同一种办法传大块数据：客户把一块缓冲区
// mem_grant 给服务，服务按客户的 PID 记下它在自己这边的地址。这里管理这张表，
// 包括回收已经退出的客户。

/**
 * @param buf_size 接受的缓冲区大小（别的大小一律拒绝）
 * @param on_drop  客户被回收时调用（可为 NULL），服务借此释放它为该客户保留的其他资源
 */
void clients_init(size_t buf_size, void (*on_drop)(int pid));

/** 处理一条 IPC_LABEL_GRANT 消息：记下（或替换）发送者的缓冲区 */
void clients_attach(const struct ipc_msg *grant);

/** 客户 pid 的缓冲区；它还没有送来缓冲区时返回 NULL */
char *clients_buf(int pid);

#endif // _USERLAND_LIB_CLIENTS_H_
