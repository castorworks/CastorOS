/**
 * utsname.h - 系统信息结构定义
 * 
 * POSIX 标准 utsname 结构，用于 uname 系统调用
 */

#ifndef _KERNEL_UTSNAME_H_
#define _KERNEL_UTSNAME_H_

#include <types.h>

/**
 * struct utsname - 系统信息结构
 * 
 * 用于 uname 系统调用返回系统基本信息
 */
struct utsname {
    char sysname[65];     // 操作系统名称（如 "CastorOS"）
    char nodename[65];    // 网络节点名称（主机名）
    char release[65];     // 内核版本号（如 "0.0.9"）
    char version[65];     // 版本信息（编译日期等）
    char machine[65];     // 硬件类型（如 "i386"）
};

#endif // _KERNEL_UTSNAME_H_

