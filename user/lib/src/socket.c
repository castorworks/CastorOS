/**
 * @file socket.c
 * @brief 用户态 Socket API 实现
 */

#include <types.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

// ============================================================================
// BSD Socket API 系统调用封装
// ============================================================================

int socket(int domain, int type, int protocol) {
    return (int)syscall3(SYS_SOCKET, (uint32_t)domain, (uint32_t)type, (uint32_t)protocol);
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    return (int)syscall3(SYS_BIND, (uint32_t)sockfd, (uint32_t)addr, (uint32_t)addrlen);
}

int listen(int sockfd, int backlog) {
    return (int)syscall2(SYS_LISTEN, (uint32_t)sockfd, (uint32_t)backlog);
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    return (int)syscall3(SYS_ACCEPT, (uint32_t)sockfd, (uint32_t)addr, (uint32_t)addrlen);
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    return (int)syscall3(SYS_CONNECT, (uint32_t)sockfd, (uint32_t)addr, (uint32_t)addrlen);
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags) {
    return (ssize_t)syscall4(SYS_SEND, (uint32_t)sockfd, (uint32_t)buf, 
                             (uint32_t)len, (uint32_t)flags);
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen) {
    return (ssize_t)syscall6(SYS_SENDTO, (uint32_t)sockfd, (uint32_t)buf,
                             (uint32_t)len, (uint32_t)flags,
                             (uint32_t)dest_addr, (uint32_t)addrlen);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    return (ssize_t)syscall4(SYS_RECV, (uint32_t)sockfd, (uint32_t)buf,
                             (uint32_t)len, (uint32_t)flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen) {
    return (ssize_t)syscall6(SYS_RECVFROM, (uint32_t)sockfd, (uint32_t)buf,
                             (uint32_t)len, (uint32_t)flags,
                             (uint32_t)src_addr, (uint32_t)addrlen);
}

int shutdown(int sockfd, int how) {
    return (int)syscall2(SYS_SHUTDOWN, (uint32_t)sockfd, (uint32_t)how);
}

int setsockopt(int sockfd, int level, int optname,
               const void *optval, socklen_t optlen) {
    return (int)syscall5(SYS_SETSOCKOPT, (uint32_t)sockfd, (uint32_t)level,
                         (uint32_t)optname, (uint32_t)optval, (uint32_t)optlen);
}

int getsockopt(int sockfd, int level, int optname,
               void *optval, socklen_t *optlen) {
    return (int)syscall5(SYS_GETSOCKOPT, (uint32_t)sockfd, (uint32_t)level,
                         (uint32_t)optname, (uint32_t)optval, (uint32_t)optlen);
}

int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    return (int)syscall3(SYS_GETSOCKNAME, (uint32_t)sockfd, (uint32_t)addr, (uint32_t)addrlen);
}

int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    return (int)syscall3(SYS_GETPEERNAME, (uint32_t)sockfd, (uint32_t)addr, (uint32_t)addrlen);
}

// ============================================================================
// select() 和 fcntl()
// ============================================================================

int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout) {
    return (int)syscall5(SYS_SELECT, (uint32_t)nfds, (uint32_t)readfds,
                         (uint32_t)writefds, (uint32_t)exceptfds, (uint32_t)timeout);
}

int fcntl(int fd, int cmd, ...) {
    int arg = 0;
    if (cmd == F_SETFL || cmd == F_SETFD || cmd == F_DUPFD) {
        __builtin_va_list ap;
        __builtin_va_start(ap, cmd);
        arg = __builtin_va_arg(ap, int);
        __builtin_va_end(ap);
    }
    return (int)syscall3(SYS_FCNTL, (uint32_t)fd, (uint32_t)cmd, (uint32_t)arg);
}

// ============================================================================
// 地址转换函数
// ============================================================================

int inet_aton(const char *cp, struct in_addr *inp) {
    if (!cp || !inp) return 0;
    
    uint32_t parts[4];
    int count = 0;
    const char *p = cp;
    
    while (*p && count < 4) {
        uint32_t val = 0;
        int digits = 0;
        
        while (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            p++;
            digits++;
        }
        
        if (digits == 0 || val > 255) return 0;
        parts[count++] = val;
        
        if (*p == '.') p++;
        else if (*p != '\0') return 0;
    }
    
    if (count != 4 || *p != '\0') return 0;
    
    inp->s_addr = (in_addr_t)(parts[0] | (parts[1] << 8) | 
                              (parts[2] << 16) | (parts[3] << 24));
    return 1;
}

in_addr_t inet_addr(const char *cp) {
    struct in_addr addr;
    if (inet_aton(cp, &addr)) return addr.s_addr;
    return INADDR_NONE;
}

char *inet_ntoa(struct in_addr in) {
    static char buf[INET_ADDRSTRLEN];
    uint32_t ip = in.s_addr;
    int len = 0;
    
    for (int i = 0; i < 4; i++) {
        uint8_t byte = (ip >> (i * 8)) & 0xFF;
        if (byte >= 100) {
            buf[len++] = '0' + (byte / 100);
            buf[len++] = '0' + ((byte / 10) % 10);
            buf[len++] = '0' + (byte % 10);
        } else if (byte >= 10) {
            buf[len++] = '0' + (byte / 10);
            buf[len++] = '0' + (byte % 10);
        } else {
            buf[len++] = '0' + byte;
        }
        if (i < 3) buf[len++] = '.';
    }
    buf[len] = '\0';
    return buf;
}

int inet_pton(int af, const char *src, void *dst) {
    if (af == AF_INET) return inet_aton(src, (struct in_addr *)dst);
    return -1;
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size) {
    if (af != AF_INET || size < INET_ADDRSTRLEN) return NULL;
    
    const struct in_addr *addr = (const struct in_addr *)src;
    uint32_t ip = addr->s_addr;
    int len = 0;
    
    for (int i = 0; i < 4; i++) {
        uint8_t byte = (ip >> (i * 8)) & 0xFF;
        if (byte >= 100) {
            dst[len++] = '0' + (byte / 100);
            dst[len++] = '0' + ((byte / 10) % 10);
            dst[len++] = '0' + (byte % 10);
        } else if (byte >= 10) {
            dst[len++] = '0' + (byte / 10);
            dst[len++] = '0' + (byte % 10);
        } else {
            dst[len++] = '0' + byte;
        }
        if (i < 3) dst[len++] = '.';
    }
    dst[len] = '\0';
    return dst;
}

// ============================================================================
// 便捷函数
// ============================================================================

int tcp_listen(uint16_t port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;
    
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return -1;
    }
    
    if (listen(sockfd, SOMAXCONN) < 0) {
        close(sockfd);
        return -1;
    }
    
    return sockfd;
}

int tcp_connect(const char *host, uint16_t port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_aton(host, &addr.sin_addr) == 0) {
        close(sockfd);
        return -1;
    }
    
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return -1;
    }
    
    return sockfd;
}

int udp_socket(uint16_t port) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) return -1;
    
    if (port > 0) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        
        if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            close(sockfd);
            return -1;
        }
    }
    
    return sockfd;
}

ssize_t udp_send(int sockfd, const char *host, uint16_t port, 
                 const void *data, size_t len) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_aton(host, &addr.sin_addr) == 0) return -1;
    
    return sendto(sockfd, data, len, 0, (struct sockaddr *)&addr, sizeof(addr));
}

