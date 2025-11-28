#ifndef _USERLAND_LIB_SYSCALL_H_
#define _USERLAND_LIB_SYSCALL_H_

#include <types.h>

#ifndef __maybe_unused
#define __maybe_unused __attribute__((unused))
#endif

// ============================================================================
// 系统调用号定义（需与内核保持一致）
// ============================================================================

enum {
    // -------------------- 进程与线程 (0x00xx) --------------------
    SYS_EXIT            = 0x0000,
    SYS_FORK            = 0x0001,
    SYS_EXECVE          = 0x0002,
    SYS_WAITPID         = 0x0003,
    SYS_GETPID          = 0x0004,
    SYS_GETPPID         = 0x0005,
    SYS_SCHED_YIELD     = 0x0006,
    SYS_CLONE           = 0x0007,

    // -------------------- 文件与文件系统 (0x01xx) --------------------
    SYS_OPEN            = 0x0100,
    SYS_CLOSE           = 0x0101,
    SYS_READ            = 0x0102,
    SYS_WRITE           = 0x0103,
    SYS_LSEEK           = 0x0104,
    SYS_STAT            = 0x0105,
    SYS_FSTAT           = 0x0106,
    SYS_MKDIR           = 0x0107,
    SYS_RMDIR           = 0x0108,
    SYS_UNLINK          = 0x0109,
    SYS_RENAME          = 0x010A,
    SYS_GETCWD          = 0x010B,
    SYS_CHDIR           = 0x010C,
    SYS_GETDENTS        = 0x010D,
    SYS_FTRUNCATE       = 0x010E,
    SYS_PIPE            = 0x010F,
    SYS_DUP             = 0x0110,
    SYS_DUP2            = 0x0111,
    SYS_IOCTL           = 0x0112,

    // -------------------- 内存管理 (0x02xx) --------------------
    SYS_BRK             = 0x0200,
    SYS_MMAP            = 0x0201,
    SYS_MUNMAP          = 0x0202,
    SYS_MPROTECT        = 0x0203,

    // -------------------- 时间与时钟 (0x03xx) --------------------
    SYS_TIME            = 0x0300,
    SYS_GETTIMEOFDAY    = 0x0301,
    SYS_NANOSLEEP       = 0x0302,
    SYS_CLOCK_GETTIME   = 0x0303,

    // -------------------- 信号与进程控制 (0x04xx) --------------------
    SYS_KILL            = 0x0400,
    SYS_SIGACTION       = 0x0401,
    SYS_SIGPROCMASK     = 0x0402,
    SYS_SIGRETURN       = 0x0403,

    // -------------------- 系统信息 / 杂项 (0x05xx) --------------------
    SYS_UNAME           = 0x0500,
    SYS_GETRANDOM       = 0x0501,
    SYS_DEBUG_PRINT     = 0x0502,
    SYS_REBOOT          = 0x0503,
    SYS_POWEROFF        = 0x0504,

    // -------------------- 网络 BSD Socket API (0x06xx) --------------------
    SYS_SOCKET          = 0x0600,
    SYS_BIND            = 0x0601,
    SYS_LISTEN          = 0x0602,
    SYS_ACCEPT          = 0x0603,
    SYS_CONNECT         = 0x0604,
    SYS_SEND            = 0x0605,
    SYS_SENDTO          = 0x0606,
    SYS_RECV            = 0x0607,
    SYS_RECVFROM        = 0x0608,
    SYS_SHUTDOWN        = 0x0609,
    SYS_SETSOCKOPT      = 0x060A,
    SYS_GETSOCKOPT      = 0x060B,
    SYS_GETSOCKNAME     = 0x060C,
    SYS_GETPEERNAME     = 0x060D,

    SYS_MAX
};

// ============================================================================
// 通用系统调用入口（必须内联，因为包含内联汇编）
// ============================================================================

static inline __maybe_unused uint32_t __syscall4(uint32_t num, uint32_t arg0,
                                                 uint32_t arg1,
                                                 uint32_t arg2,
                                                 uint32_t arg3) {
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg0), "c"(arg1), "d"(arg2), "S"(arg3)
        : "memory", "cc"
    );
    return ret;
}

static inline __maybe_unused uint32_t syscall0(uint32_t num) {
    return __syscall4(num, 0, 0, 0, 0);
}

static inline __maybe_unused uint32_t syscall1(uint32_t num, uint32_t arg0) {
    return __syscall4(num, arg0, 0, 0, 0);
}

static inline __maybe_unused uint32_t syscall2(uint32_t num, uint32_t arg0,
                                               uint32_t arg1) {
    return __syscall4(num, arg0, arg1, 0, 0);
}

static inline __maybe_unused uint32_t syscall3(uint32_t num, uint32_t arg0,
                                               uint32_t arg1,
                                               uint32_t arg2) {
    return __syscall4(num, arg0, arg1, arg2, 0);
}

static inline __maybe_unused uint32_t syscall4(uint32_t num, uint32_t arg0,
                                               uint32_t arg1,
                                               uint32_t arg2,
                                               uint32_t arg3) {
    return __syscall4(num, arg0, arg1, arg2, arg3);
}

// 5 参数系统调用（使用 edi 寄存器）
static inline __maybe_unused uint32_t __syscall5(uint32_t num, uint32_t arg0,
                                                 uint32_t arg1,
                                                 uint32_t arg2,
                                                 uint32_t arg3,
                                                 uint32_t arg4) {
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg0), "c"(arg1), "d"(arg2), "S"(arg3), "D"(arg4)
        : "memory", "cc"
    );
    return ret;
}

static inline __maybe_unused uint32_t syscall5(uint32_t num, uint32_t arg0,
                                               uint32_t arg1,
                                               uint32_t arg2,
                                               uint32_t arg3,
                                               uint32_t arg4) {
    return __syscall5(num, arg0, arg1, arg2, arg3, arg4);
}

// 6 参数系统调用（使用 ebp 寄存器作为第 6 个参数）
// 注意：ebp 会被内核从中断帧中读取
static inline __maybe_unused uint32_t __syscall6(uint32_t num, uint32_t arg0,
                                                 uint32_t arg1, uint32_t arg2,
                                                 uint32_t arg3, uint32_t arg4,
                                                 uint32_t arg5) {
    uint32_t ret;
    __asm__ volatile (
        "push %%ebp\n\t"        // 保存原 ebp
        "mov %7, %%ebp\n\t"     // 将第 6 个参数放入 ebp
        "int $0x80\n\t"
        "pop %%ebp"             // 恢复原 ebp
        : "=a"(ret)
        : "a"(num), "b"(arg0), "c"(arg1), "d"(arg2), "S"(arg3), "D"(arg4), "g"(arg5)
        : "memory", "cc"
    );
    return ret;
}

static inline __maybe_unused uint32_t syscall6(uint32_t num, uint32_t arg0,
                                               uint32_t arg1, uint32_t arg2,
                                               uint32_t arg3, uint32_t arg4,
                                               uint32_t arg5) {
    return __syscall6(num, arg0, arg1, arg2, arg3, arg4, arg5);
}

// ============================================================================
// 用户态封装函数声明
// ============================================================================

#define STDIN_FILENO   0
#define STDOUT_FILENO  1
#define STDERR_FILENO  2

void exit(int code);
int fork(void);
int exec(const char *path);
int getpid(void);
int getppid(void);
int waitpid(int pid, int *wstatus, int options);
int wait(int *wstatus);
int open(const char *path, int flags, uint32_t mode);
int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);
int mkdir(const char *path, uint32_t mode);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
int getdents(int fd, uint32_t index, struct dirent *dirent);
int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int ftruncate(int fd, off_t length);
int pipe(int fds[2]);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int ioctl(int fd, unsigned long request, void *argp);
void *brk(void *addr);
void *sbrk(int increment);
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);
int uname(struct utsname *buf);
int rename(const char *oldpath, const char *newpath);
size_t strlen_simple(const char *str);
void print(const char *msg);
int reboot(void);
int poweroff(void);

// ============================================================================
// BSD Socket API（符合 POSIX.1-2008 标准）
// ============================================================================

// 地址族
#define AF_UNSPEC       0
#define AF_INET         2

// Socket 类型
#define SOCK_STREAM     1       // TCP
#define SOCK_DGRAM      2       // UDP
#define SOCK_RAW        3       // Raw socket

// 协议号
#define IPPROTO_IP      0
#define IPPROTO_ICMP    1
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17

// Socket 选项级别
#define SOL_SOCKET      1

// Socket 选项
#define SO_REUSEADDR    2
#define SO_KEEPALIVE    9
#define SO_RCVTIMEO     20
#define SO_SNDTIMEO     21
#define SO_RCVBUF       8
#define SO_SNDBUF       7
#define SO_ERROR        4

// shutdown() how 参数
#define SHUT_RD         0
#define SHUT_WR         1
#define SHUT_RDWR       2

// 消息标志
#define MSG_PEEK        0x02
#define MSG_DONTWAIT    0x40
#define MSG_WAITALL     0x100

// 最大连接数
#define SOMAXCONN       128

// 特殊地址
#define INADDR_ANY          0x00000000
#define INADDR_BROADCAST    0xFFFFFFFF
#define INADDR_LOOPBACK     0x7F000001

typedef uint32_t socklen_t;

/**
 * @brief 通用 socket 地址结构
 */
struct sockaddr {
    uint16_t sa_family;
    char     sa_data[14];
};

/**
 * @brief IPv4 socket 地址结构
 */
struct sockaddr_in {
    uint16_t sin_family;        // AF_INET
    uint16_t sin_port;          // 端口号（网络字节序）
    uint32_t sin_addr;          // IP 地址（网络字节序）
    uint8_t  sin_zero[8];       // 填充
};

// 字节序转换（适用于 32 位小端系统）
static inline __maybe_unused uint16_t htons(uint16_t hostshort) {
    return ((hostshort & 0xFF) << 8) | ((hostshort >> 8) & 0xFF);
}

static inline __maybe_unused uint16_t ntohs(uint16_t netshort) {
    return htons(netshort);
}

static inline __maybe_unused uint32_t htonl(uint32_t hostlong) {
    return ((hostlong & 0xFF) << 24) |
           ((hostlong & 0xFF00) << 8) |
           ((hostlong >> 8) & 0xFF00) |
           ((hostlong >> 24) & 0xFF);
}

static inline __maybe_unused uint32_t ntohl(uint32_t netlong) {
    return htonl(netlong);
}

/**
 * @brief 创建 socket
 * @param domain 地址族（AF_INET）
 * @param type socket 类型（SOCK_STREAM/SOCK_DGRAM）
 * @param protocol 协议（通常为 0）
 * @return socket 描述符，-1 失败
 */
int socket(int domain, int type, int protocol);

/**
 * @brief 绑定地址
 * @param sockfd socket 描述符
 * @param addr 地址
 * @param addrlen 地址长度
 * @return 0 成功，-1 失败
 */
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

/**
 * @brief 监听连接
 * @param sockfd socket 描述符
 * @param backlog 等待队列长度
 * @return 0 成功，-1 失败
 */
int listen(int sockfd, int backlog);

/**
 * @brief 接受连接
 * @param sockfd socket 描述符
 * @param addr 客户端地址（输出）
 * @param addrlen 地址长度（输入/输出）
 * @return 新 socket 描述符，-1 失败
 */
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @brief 发起连接
 * @param sockfd socket 描述符
 * @param addr 服务端地址
 * @param addrlen 地址长度
 * @return 0 成功，-1 失败
 */
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

/**
 * @brief 发送数据
 * @param sockfd socket 描述符
 * @param buf 数据缓冲区
 * @param len 数据长度
 * @param flags 标志
 * @return 发送的字节数，-1 失败
 */
ssize_t send(int sockfd, const void *buf, size_t len, int flags);

/**
 * @brief 发送数据到指定地址
 */
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);

/**
 * @brief 接收数据
 * @param sockfd socket 描述符
 * @param buf 数据缓冲区
 * @param len 缓冲区大小
 * @param flags 标志
 * @return 接收的字节数，0 连接关闭，-1 失败
 */
ssize_t recv(int sockfd, void *buf, size_t len, int flags);

/**
 * @brief 接收数据并获取源地址
 */
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);

/**
 * @brief 部分关闭 socket
 * @param sockfd socket 描述符
 * @param how 关闭方式（SHUT_RD/SHUT_WR/SHUT_RDWR）
 * @return 0 成功，-1 失败
 */
int shutdown(int sockfd, int how);

/**
 * @brief 设置 socket 选项
 */
int setsockopt(int sockfd, int level, int optname,
               const void *optval, socklen_t optlen);

/**
 * @brief 获取 socket 选项
 */
int getsockopt(int sockfd, int level, int optname,
               void *optval, socklen_t *optlen);

/**
 * @brief 获取本地地址
 */
int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @brief 获取对端地址
 */
int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

// ============================================================================
// ioctl 请求码（用于网络配置）
// ============================================================================

#define SIOCBASE        0x8900

// 网络接口配置
#define SIOCGIFADDR     (SIOCBASE + 0x01)
#define SIOCSIFADDR     (SIOCBASE + 0x02)
#define SIOCGIFNETMASK  (SIOCBASE + 0x03)
#define SIOCSIFNETMASK  (SIOCBASE + 0x04)
#define SIOCGIFFLAGS    (SIOCBASE + 0x05)
#define SIOCSIFFLAGS    (SIOCBASE + 0x06)
#define SIOCGIFHWADDR   (SIOCBASE + 0x07)
#define SIOCGIFMTU      (SIOCBASE + 0x08)
#define SIOCSIFMTU      (SIOCBASE + 0x09)
#define SIOCGIFCONF     (SIOCBASE + 0x10)
#define SIOCGIFINDEX    (SIOCBASE + 0x11)
#define SIOCGIFGATEWAY  (SIOCBASE + 0x12)
#define SIOCSIFGATEWAY  (SIOCBASE + 0x13)

// ARP 操作
#define SIOCSARP        (SIOCBASE + 0x20)
#define SIOCGARP        (SIOCBASE + 0x21)
#define SIOCDARP        (SIOCBASE + 0x22)

// CastorOS 扩展
#define SIOCPING        (SIOCBASE + 0x40)
#define SIOCGIFSTATS    (SIOCBASE + 0x41)

// 接口标志
#define IFF_UP          0x0001
#define IFF_BROADCAST   0x0002
#define IFF_LOOPBACK    0x0008
#define IFF_RUNNING     0x0040
#define IFF_MULTICAST   0x1000

/**
 * @brief 网络接口请求结构（用于 ioctl）
 */
struct ifreq {
    char ifr_name[16];
    union {
        struct sockaddr_in ifr_addr;
        struct sockaddr_in ifr_netmask;
        struct sockaddr_in ifr_gateway;
        struct {
            uint8_t sa_data[14];
        } ifr_hwaddr;
        int32_t ifr_flags;
        int32_t ifr_mtu;
        int32_t ifr_ifindex;
    };
};

/**
 * @brief ARP 请求结构（用于 ioctl）
 */
struct arpreq {
    struct sockaddr_in arp_pa;
    struct {
        uint16_t sa_family;
        uint8_t sa_data[14];
    } arp_ha;
    int32_t arp_flags;
    char arp_dev[16];
};

#define ATF_COM         0x02
#define ATF_PERM        0x04
#define ATF_PUBL        0x08

/**
 * @brief Ping 请求结构（CastorOS 扩展）
 */
struct ping_req {
    char host[64];
    int32_t count;
    int32_t timeout_ms;
    uint32_t sent;
    uint32_t received;
    uint32_t min_rtt;
    uint32_t max_rtt;
    uint32_t avg_rtt;
};

/**
 * @brief 网络接口统计结构（CastorOS 扩展）
 */
struct ifstats {
    char ifr_name[16];
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
};

#endif /* _USERLAND_LIB_SYSCALL_H_ */
