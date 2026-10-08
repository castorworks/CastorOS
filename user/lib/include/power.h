#ifndef _USERLAND_LIB_POWER_H_
#define _USERLAND_LIB_POWER_H_

// 关机和重启：请 init（PID 1）来做。
//
// 能让机器断电或复位的系统调用（power，见 syscall.h）要特权，只有 init 有。init 收到请求后
// 先让磁盘上的文件系统停下来（做完手上的请求就退出，盘上不留做到一半的修改），再让块设备
// 驱动把磁盘自己的缓存落盘，然后才调用它。

#include <syscall.h>

/** 请求的 label（和 names.h 里名字服务的那几个发给同一个进程，所以不能重号）。data[0]: POWER_OFF 或 POWER_REBOOT */
#define POWER_REQUEST   16

/**
 * 请 init 关机（POWER_OFF）或重启（POWER_REBOOT）。init 接下了就不应答，调用者停在这里
 * 直到机器断电或复位。
 * @return -1 = init 没有接：action 不认识，或者已经在关机了
 */
int power_request(int action);

#endif // _USERLAND_LIB_POWER_H_
