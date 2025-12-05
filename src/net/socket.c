/**
 * @file socket.c
 * @brief BSD Socket API 实现
 */

#include <net/socket.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/ip.h>
#include <net/netdev.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <kernel/sync/spinlock.h>

// Socket 结构
typedef struct socket {
    int type;               // SOCK_STREAM, SOCK_DGRAM
    int protocol;           // IPPROTO_TCP, IPPROTO_UDP
    int domain;             // AF_INET
    
    union {
        tcp_pcb_t *tcp;     // TCP 控制块
        udp_pcb_t *udp;     // UDP 控制块
    } pcb;
    
    // 状态
    bool bound;             // 是否已绑定
    bool connected;         // 是否已连接
    bool listening;         // 是否在监听
    
    // 本地和远程地址
    struct sockaddr_in local_addr;
    struct sockaddr_in remote_addr;
    
    // 选项
    int recv_timeout;       // 接收超时（毫秒）
    int send_timeout;       // 发送超时（毫秒）
    bool reuse_addr;        // 地址重用
    int flags;              // 文件标志（O_NONBLOCK 等）
    
    // 错误状态
    int error;
} socket_t;

// Socket 表
#define MAX_SOCKETS     64
static socket_t *socket_table[MAX_SOCKETS];
static spinlock_t socket_lock;

// 用于标记正在分配中的 socket 槽位
static socket_t socket_allocating_marker;

/**
 * @brief 分配 socket 描述符（原子地标记为分配中）
 */
static int socket_alloc_fd(void) {
    bool irq_state;
    spinlock_lock_irqsave(&socket_lock, &irq_state);
    
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (socket_table[i] == NULL) {
            // 立即标记为分配中，防止竞态条件
            socket_table[i] = &socket_allocating_marker;
            spinlock_unlock_irqrestore(&socket_lock, irq_state);
            return i;
        }
    }
    
    spinlock_unlock_irqrestore(&socket_lock, irq_state);
    return -1;
}

/**
 * @brief 释放已预留但未使用的 socket 槽位
 */
static void socket_free_fd(int fd) {
    if (fd >= 0 && fd < MAX_SOCKETS) {
        bool irq_state;
        spinlock_lock_irqsave(&socket_lock, &irq_state);
        if (socket_table[fd] == &socket_allocating_marker) {
            socket_table[fd] = NULL;
        }
        spinlock_unlock_irqrestore(&socket_lock, irq_state);
    }
}

/**
 * @brief 获取 socket 结构
 */
static socket_t *socket_get(int fd) {
    if (fd < 0 || fd >= MAX_SOCKETS) {
        return NULL;
    }
    socket_t *sock = socket_table[fd];
    // 不返回分配中占位符
    if (sock == &socket_allocating_marker) {
        return NULL;
    }
    return sock;
}

void socket_init(void) {
    spinlock_init(&socket_lock);
    memset(socket_table, 0, sizeof(socket_table));
    
    LOG_INFO_MSG("socket: Socket subsystem initialized\n");
}

int sys_socket(int domain, int type, int protocol) {
    // 只支持 AF_INET
    if (domain != AF_INET) {
        LOG_WARN_MSG("socket: Unsupported domain %d\n", domain);
        return -1;
    }
    
    // 验证类型和协议
    if (type == SOCK_STREAM) {
        if (protocol != 0 && protocol != IPPROTO_TCP) {
            return -1;
        }
        protocol = IPPROTO_TCP;
    } else if (type == SOCK_DGRAM) {
        if (protocol != 0 && protocol != IPPROTO_UDP) {
            return -1;
        }
        protocol = IPPROTO_UDP;
    } else {
        LOG_WARN_MSG("socket: Unsupported type %d\n", type);
        return -1;
    }
    
    // 分配文件描述符
    int fd = socket_alloc_fd();
    if (fd < 0) {
        LOG_ERROR_MSG("socket: No free socket descriptors\n");
        return -1;
    }
    
    // 分配 socket 结构
    socket_t *sock = (socket_t *)kmalloc(sizeof(socket_t));
    if (!sock) {
        socket_free_fd(fd);  // 释放预留的 fd
        return -1;
    }
    
    memset(sock, 0, sizeof(socket_t));
    sock->domain = domain;
    sock->type = type;
    sock->protocol = protocol;
    
    // 创建协议控制块
    if (type == SOCK_STREAM) {
        sock->pcb.tcp = tcp_pcb_new();
        if (!sock->pcb.tcp) {
            kfree(sock);
            socket_free_fd(fd);  // 释放预留的 fd
            return -1;
        }
    } else if (type == SOCK_DGRAM) {
        sock->pcb.udp = udp_pcb_new();
        if (!sock->pcb.udp) {
            kfree(sock);
            socket_free_fd(fd);  // 释放预留的 fd
            return -1;
        }
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&socket_lock, &irq_state);
    socket_table[fd] = sock;  // 替换占位符为实际 socket
    spinlock_unlock_irqrestore(&socket_lock, irq_state);
    
    return fd;
}

int sys_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    socket_t *sock = socket_get(sockfd);
    if (!sock || !addr) {
        return -1;
    }
    
    if (addrlen < sizeof(struct sockaddr_in)) {
        return -1;
    }
    
    const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
    if (sin->sin_family != AF_INET) {
        return -1;
    }
    
    uint32_t ip = sin->sin_addr;
    uint16_t port = ntohs(sin->sin_port);
    
    int ret;
    if (sock->type == SOCK_STREAM) {
        ret = tcp_bind(sock->pcb.tcp, ip, port);
    } else {
        ret = udp_bind(sock->pcb.udp, ip, port);
    }
    
    if (ret == 0) {
        sock->bound = true;
        memcpy(&sock->local_addr, sin, sizeof(struct sockaddr_in));
    }
    
    return ret;
}

int sys_listen(int sockfd, int backlog) {
    socket_t *sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }
    
    if (sock->type != SOCK_STREAM) {
        return -1;  // 只有 TCP 支持 listen
    }
    
    if (!sock->bound) {
        return -1;  // 必须先绑定
    }
    
    int ret = tcp_listen(sock->pcb.tcp, backlog);
    if (ret == 0) {
        sock->listening = true;
    }
    
    return ret;
}

int sys_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    socket_t *sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }
    
    if (sock->type != SOCK_STREAM || !sock->listening) {
        return -1;
    }
    
    // 等待新连接
    tcp_pcb_t *new_pcb = tcp_accept(sock->pcb.tcp);
    if (!new_pcb) {
        return -1;  // 暂无连接
    }
    
    // 分配新的 socket 描述符
    int new_fd = socket_alloc_fd();
    if (new_fd < 0) {
        tcp_pcb_free(new_pcb);
        return -1;
    }
    
    // 创建新的 socket 结构
    socket_t *new_sock = (socket_t *)kmalloc(sizeof(socket_t));
    if (!new_sock) {
        tcp_pcb_free(new_pcb);
        return -1;
    }
    
    memset(new_sock, 0, sizeof(socket_t));
    new_sock->domain = AF_INET;
    new_sock->type = SOCK_STREAM;
    new_sock->protocol = IPPROTO_TCP;
    new_sock->pcb.tcp = new_pcb;
    new_sock->bound = true;
    new_sock->connected = true;
    
    // 设置地址
    new_sock->local_addr.sin_family = AF_INET;
    new_sock->local_addr.sin_port = htons(new_pcb->local_port);
    new_sock->local_addr.sin_addr = new_pcb->local_ip;
    
    new_sock->remote_addr.sin_family = AF_INET;
    new_sock->remote_addr.sin_port = htons(new_pcb->remote_port);
    new_sock->remote_addr.sin_addr = new_pcb->remote_ip;
    
    // 返回客户端地址
    if (addr && addrlen) {
        if (*addrlen >= sizeof(struct sockaddr_in)) {
            memcpy(addr, &new_sock->remote_addr, sizeof(struct sockaddr_in));
            *addrlen = sizeof(struct sockaddr_in);
        }
    }
    
    bool irq_state;
    spinlock_lock_irqsave(&socket_lock, &irq_state);
    socket_table[new_fd] = new_sock;
    spinlock_unlock_irqrestore(&socket_lock, irq_state);
    
    return new_fd;
}

int sys_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    socket_t *sock = socket_get(sockfd);
    if (!sock || !addr) {
        return -1;
    }
    
    if (addrlen < sizeof(struct sockaddr_in)) {
        return -1;
    }
    
    const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
    if (sin->sin_family != AF_INET) {
        return -1;
    }
    
    uint32_t ip = sin->sin_addr;
    uint16_t port = ntohs(sin->sin_port);
    
    int ret;
    if (sock->type == SOCK_STREAM) {
        ret = tcp_connect(sock->pcb.tcp, ip, port);
    } else {
        ret = udp_connect(sock->pcb.udp, ip, port);
    }
    
    if (ret == 0) {
        sock->connected = true;
        memcpy(&sock->remote_addr, sin, sizeof(struct sockaddr_in));
    }
    
    return ret;
}

ssize_t sys_send(int sockfd, const void *buf, size_t len, int flags) {
    (void)flags;  // 暂不使用
    
    socket_t *sock = socket_get(sockfd);
    if (!sock || !buf) {
        return -1;
    }
    
    if (!sock->connected) {
        return -1;
    }
    
    if (sock->type == SOCK_STREAM) {
        return tcp_write(sock->pcb.tcp, buf, len);
    } else {
        // UDP: 使用已连接的地址
        netbuf_t *nbuf = netbuf_alloc(len);
        if (!nbuf) {
            return -1;
        }
        
        uint8_t *data = netbuf_put(nbuf, len);
        memcpy(data, buf, len);
        
        int ret = udp_send(sock->pcb.udp, nbuf);
        if (ret < 0) {
            netbuf_free(nbuf);
            return -1;
        }
        
        return len;
    }
}

ssize_t sys_sendto(int sockfd, const void *buf, size_t len, int flags,
                   const struct sockaddr *dest_addr, socklen_t addrlen) {
    (void)flags;
    
    socket_t *sock = socket_get(sockfd);
    if (!sock || !buf) {
        return -1;
    }
    
    // TCP 不支持 sendto
    if (sock->type == SOCK_STREAM) {
        if (dest_addr) {
            return -1;
        }
        return sys_send(sockfd, buf, len, flags);
    }
    
    // UDP
    if (!dest_addr || addrlen < sizeof(struct sockaddr_in)) {
        return -1;
    }
    
    const struct sockaddr_in *sin = (const struct sockaddr_in *)dest_addr;
    if (sin->sin_family != AF_INET) {
        return -1;
    }
    
    netbuf_t *nbuf = netbuf_alloc(len);
    if (!nbuf) {
        return -1;
    }
    
    uint8_t *data = netbuf_put(nbuf, len);
    memcpy(data, buf, len);
    
    int ret = udp_sendto(sock->pcb.udp, nbuf, sin->sin_addr, ntohs(sin->sin_port));
    if (ret < 0) {
        netbuf_free(nbuf);
        return -1;
    }
    
    return len;
}

ssize_t sys_recv(int sockfd, void *buf, size_t len, int flags) {
    socket_t *sock = socket_get(sockfd);
    if (!sock || !buf) {
        return -1;
    }
    
    bool nonblock = (sock->flags & O_NONBLOCK) || (flags & MSG_DONTWAIT);
    
    if (sock->type == SOCK_STREAM) {
        tcp_pcb_t *pcb = sock->pcb.tcp;
        
        // 检查是否有数据可读
        if (pcb->recv_len == 0) {
            // 检查连接状态
            if (pcb->state == TCP_CLOSE_WAIT || pcb->state == TCP_CLOSED) {
                return 0;  // 连接关闭
            }
            if (nonblock) {
                return -EAGAIN;  // 非阻塞模式，无数据
            }
            return -1;  // 暂无数据
        }
        
        return tcp_read(pcb, buf, len);
    } else {
        // UDP: 从接收队列获取
        udp_pcb_t *pcb = sock->pcb.udp;
        netbuf_t *nbuf = udp_recv_poll(pcb);
        if (!nbuf) {
            if (nonblock) {
                return -EAGAIN;  // 非阻塞模式，无数据
            }
            return -1;  // 暂无数据
        }
        
        size_t copy_len = (nbuf->len < len) ? nbuf->len : len;
        memcpy(buf, nbuf->data, copy_len);
        
        netbuf_free(nbuf);
        return copy_len;
    }
}

ssize_t sys_recvfrom(int sockfd, void *buf, size_t len, int flags,
                     struct sockaddr *src_addr, socklen_t *addrlen) {
    socket_t *sock = socket_get(sockfd);
    if (!sock || !buf) {
        return -1;
    }
    
    bool nonblock = (sock->flags & O_NONBLOCK) || (flags & MSG_DONTWAIT);
    
    // TCP 不支持 recvfrom
    if (sock->type == SOCK_STREAM) {
        return sys_recv(sockfd, buf, len, flags);
    }
    
    // UDP
    udp_pcb_t *pcb = sock->pcb.udp;
    netbuf_t *nbuf = udp_recv_poll(pcb);
    if (!nbuf) {
        if (nonblock) {
            return -EAGAIN;  // 非阻塞模式，无数据
        }
        return -1;  // 暂无数据
    }
    
    size_t copy_len = (nbuf->len < len) ? nbuf->len : len;
    memcpy(buf, nbuf->data, copy_len);
    
    // 从 nbuf 中获取源地址信息
    if (src_addr && addrlen && *addrlen >= sizeof(struct sockaddr_in)) {
        struct sockaddr_in *sin = (struct sockaddr_in *)src_addr;
        sin->sin_family = AF_INET;
        sin->sin_port = htons(nbuf->src_port);  // 端口转为网络字节序
        sin->sin_addr = nbuf->src_ip;           // IP 已经是网络字节序
        memset(sin->sin_zero, 0, sizeof(sin->sin_zero));
        *addrlen = sizeof(struct sockaddr_in);
    }
    
    netbuf_free(nbuf);
    return copy_len;
}

int sys_closesocket(int sockfd) {
    socket_t *sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }
    
    // 关闭协议控制块
    if (sock->type == SOCK_STREAM && sock->pcb.tcp) {
        tcp_close(sock->pcb.tcp);
        tcp_pcb_free(sock->pcb.tcp);
    } else if (sock->type == SOCK_DGRAM && sock->pcb.udp) {
        udp_pcb_free(sock->pcb.udp);
    }
    
    // 从表中移除
    bool irq_state;
    spinlock_lock_irqsave(&socket_lock, &irq_state);
    socket_table[sockfd] = NULL;
    spinlock_unlock_irqrestore(&socket_lock, irq_state);
    
    kfree(sock);
    return 0;
}

int sys_shutdown(int sockfd, int how) {
    socket_t *sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }
    
    if (sock->type == SOCK_STREAM) {
        if (how == SHUT_WR || how == SHUT_RDWR) {
            tcp_close(sock->pcb.tcp);
        }
    }
    
    return 0;
}

int sys_setsockopt(int sockfd, int level, int optname, 
                   const void *optval, socklen_t optlen) {
    socket_t *sock = socket_get(sockfd);
    if (!sock || !optval) {
        return -1;
    }
    
    if (level == SOL_SOCKET) {
        switch (optname) {
            case SO_REUSEADDR:
                if (optlen >= sizeof(int)) {
                    sock->reuse_addr = *(int *)optval;
                    return 0;
                }
                break;
            case SO_RCVTIMEO:
                if (optlen >= sizeof(int)) {
                    sock->recv_timeout = *(int *)optval;
                    return 0;
                }
                break;
            case SO_SNDTIMEO:
                if (optlen >= sizeof(int)) {
                    sock->send_timeout = *(int *)optval;
                    return 0;
                }
                break;
        }
    }
    
    return -1;
}

int sys_getsockopt(int sockfd, int level, int optname, 
                   void *optval, socklen_t *optlen) {
    socket_t *sock = socket_get(sockfd);
    if (!sock || !optval || !optlen) {
        return -1;
    }
    
    if (level == SOL_SOCKET) {
        switch (optname) {
            case SO_REUSEADDR:
                if (*optlen >= sizeof(int)) {
                    *(int *)optval = sock->reuse_addr;
                    *optlen = sizeof(int);
                    return 0;
                }
                break;
            case SO_ERROR:
                if (*optlen >= sizeof(int)) {
                    *(int *)optval = sock->error;
                    sock->error = 0;
                    *optlen = sizeof(int);
                    return 0;
                }
                break;
        }
    }
    
    return -1;
}

int sys_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    socket_t *sock = socket_get(sockfd);
    if (!sock || !addr || !addrlen) {
        return -1;
    }
    
    if (*addrlen >= sizeof(struct sockaddr_in)) {
        memcpy(addr, &sock->local_addr, sizeof(struct sockaddr_in));
        *addrlen = sizeof(struct sockaddr_in);
        return 0;
    }
    
    return -1;
}

int sys_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    socket_t *sock = socket_get(sockfd);
    if (!sock || !addr || !addrlen) {
        return -1;
    }
    
    if (!sock->connected) {
        return -1;
    }
    
    if (*addrlen >= sizeof(struct sockaddr_in)) {
        memcpy(addr, &sock->remote_addr, sizeof(struct sockaddr_in));
        *addrlen = sizeof(struct sockaddr_in);
        return 0;
    }
    
    return -1;
}

int sys_fcntl(int sockfd, int cmd, int arg) {
    socket_t *sock = socket_get(sockfd);
    if (!sock) {
        return -1;
    }
    
    switch (cmd) {
        case F_GETFL:
            return sock->flags;
            
        case F_SETFL:
            sock->flags = arg;
            return 0;
            
        default:
            return -1;
    }
}

int sys_select(int nfds, fd_set *readfds, fd_set *writefds,
               fd_set *exceptfds, struct timeval *timeout) {
    fd_set read_result, write_result, except_result;
    FD_ZERO(&read_result);
    FD_ZERO(&write_result);
    FD_ZERO(&except_result);
    
    // 计算超时时间（毫秒）
    uint32_t timeout_ms;
    if (timeout) {
        timeout_ms = (uint32_t)(timeout->tv_sec * 1000 + timeout->tv_usec / 1000);
    } else {
        timeout_ms = 0xFFFFFFFF;  // 无限等待
    }
    
    // 需要包含 timer.h
    extern uint64_t timer_get_uptime_ms(void);
    uint32_t start = (uint32_t)timer_get_uptime_ms();
    
    int ready_count = 0;
    
    // 限制 nfds 范围
    if (nfds > MAX_SOCKETS) {
        nfds = MAX_SOCKETS;
    }
    
    while (1) {
        ready_count = 0;
        
        for (int fd = 0; fd < nfds; fd++) {
            socket_t *sock = socket_get(fd);
            if (!sock) continue;
            
            // 检查可读
            if (readfds && FD_ISSET(fd, readfds)) {
                bool readable = false;
                
                if (sock->type == SOCK_STREAM) {
                    tcp_pcb_t *pcb = sock->pcb.tcp;
                    
                    if (sock->listening) {
                        // 监听 socket：有待接受的连接
                        readable = (pcb->accept_queue != NULL);
                    } else {
                        // 已连接 socket：有数据或连接关闭
                        readable = (pcb->recv_len > 0) ||
                                  (pcb->state == TCP_CLOSE_WAIT) ||
                                  (pcb->state == TCP_CLOSED);
                    }
                } else {
                    // UDP socket：有数据
                    readable = udp_has_data(sock->pcb.udp);
                }
                
                if (readable) {
                    FD_SET(fd, &read_result);
                    ready_count++;
                }
            }
            
            // 检查可写
            if (writefds && FD_ISSET(fd, writefds)) {
                bool writable = false;
                
                if (sock->type == SOCK_STREAM) {
                    tcp_pcb_t *pcb = sock->pcb.tcp;
                    // 发送窗口有空间
                    if (pcb->state == TCP_ESTABLISHED) {
                        uint32_t in_flight = pcb->snd_nxt - pcb->snd_una;
                        uint32_t effective_window = (pcb->snd_wnd < pcb->cwnd) ? 
                                                    pcb->snd_wnd : pcb->cwnd;
                        writable = (in_flight < effective_window);
                    }
                } else {
                    // UDP 总是可写
                    writable = true;
                }
                
                if (writable) {
                    FD_SET(fd, &write_result);
                    ready_count++;
                }
            }
            
            // 检查异常
            if (exceptfds && FD_ISSET(fd, exceptfds)) {
                bool has_error = (sock->error != 0);
                
                if (sock->type == SOCK_STREAM) {
                    tcp_pcb_t *pcb = sock->pcb.tcp;
                    // 连接被重置也算异常
                    has_error = has_error || (pcb->state == TCP_CLOSED && sock->connected);
                }
                
                if (has_error) {
                    FD_SET(fd, &except_result);
                    ready_count++;
                }
            }
        }
        
        // 有就绪的描述符，返回
        if (ready_count > 0) break;
        
        // 检查超时
        uint32_t elapsed = (uint32_t)timer_get_uptime_ms() - start;
        if (elapsed >= timeout_ms) break;
        
        // 让出 CPU（简单忙等待，实际应该使用调度器）
        // 这里可以调用 task_yield() 但为了简单起见先忙等
    }
    
    // 复制结果
    if (readfds) memcpy(readfds, &read_result, sizeof(fd_set));
    if (writefds) memcpy(writefds, &write_result, sizeof(fd_set));
    if (exceptfds) memcpy(exceptfds, &except_result, sizeof(fd_set));
    
    return ready_count;
}

