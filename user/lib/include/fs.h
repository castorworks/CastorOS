#ifndef _USERLAND_LIB_FS_H_
#define _USERLAND_LIB_FS_H_

// ============================================================================
// 标准文件描述符
// ============================================================================

#ifndef STDIN_FILENO
#define STDIN_FILENO   0
#endif

#ifndef STDOUT_FILENO
#define STDOUT_FILENO  1
#endif

#ifndef STDERR_FILENO
#define STDERR_FILENO  2
#endif

// 权限标志
#define FS_PERM_READ  0x01
#define FS_PERM_WRITE 0x02
#define FS_PERM_EXEC  0x04

#endif // _USERLAND_LIB_FS_H_
