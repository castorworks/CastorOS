/**
 * 文件服务的客户端：按文件名前缀把请求发给对应的服务进程，内容经共享缓冲区传递
 */

#include <fs.h>
#include <names.h>
#include <syscall.h>
#include <string.h>

// 每个文件服务一条连接。句柄的高位记录它属于哪条连接。
struct conn {
    const char *service;    // 名字服务里的名字
    bool required;          // 必须存在（开机时等它出现）还是可有可无
    int server;             // 服务进程的 PID
    char *buf;              // 与服务共享的缓冲区
    int owner;              // 建立这条连接的进程：fork 出来的子进程要自己重新建立
};

static struct conn conns[] = {
    { FS_SERVICE_NAME, true, 0, NULL, 0 },
    { FS_DISK_SERVICE_NAME, false, 0, NULL, 0 },
};

#define CONN_SHIFT  8
#define FD_MASK     ((1 << CONN_SHIFT) - 1)

/** 确保当前进程与服务之间有共享缓冲区 */
static struct conn *fs_connect(int index) {
    struct conn *c = &conns[index];
    int self = getpid();
    if (c->buf && c->owner == self) {
        return c;
    }

    int server = c->required ? name_wait(c->service) : name_lookup(c->service);
    if (server <= 0) {
        return NULL;
    }
    char *buf = (char *)mmap(NULL, FS_BUF_SIZE, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED) {
        return NULL;
    }
    // 服务从内核发出的授予通知里得知缓冲区在它那边的位置
    if (mem_grant(server, buf, FS_BUF_SIZE) != 0) {
        munmap(buf, FS_BUF_SIZE);
        return NULL;
    }

    c->server = server;
    c->buf = buf;
    c->owner = self;
    return c;
}

/** 按前缀选连接；*name 被改成去掉前缀之后的部分 */
static int route(const char **name) {
    size_t n = strlen(FS_DISK_PREFIX);
    if (strncmp(*name, FS_DISK_PREFIX, n) == 0) {
        *name += n;
        return 1;
    }
    return 0;
}

/** 句柄对应的连接；*fd 被改成服务端的句柄 */
static struct conn *conn_of(int *fd) {
    int index = *fd >> CONN_SHIFT;
    if (*fd < 0 || index >= (int)(sizeof(conns) / sizeof(conns[0]))) {
        return NULL;
    }
    *fd &= FD_MASK;
    return fs_connect(index);
}

/** 发一个请求，返回应答的 data[0]；m 里带回完整应答 */
static long fs_request(struct conn *c, struct ipc_msg *m) {
    if (ipc_call(c->server, m) != 0) {
        return -1;
    }
    return (long)(int64_t)m->data[0];
}

/** 选好连接并把（去掉前缀的）文件名放进共享缓冲区 */
static struct conn *put_name(const char *name, int *index) {
    if (!name) {
        return NULL;
    }
    *index = route(&name);
    if (name[0] == '\0' || strlen(name) >= FS_NAME_MAX) {
        return NULL;
    }
    struct conn *c = fs_connect(*index);
    if (c) {
        strcpy(c->buf, name);
    }
    return c;
}

int fs_open(const char *name, int flags) {
    int index;
    struct conn *c = put_name(name, &index);
    if (!c) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = FS_OPEN;
    m.data[0] = (uint64_t)flags;
    long fd = fs_request(c, &m);
    return fd < 0 ? -1 : (int)(fd | (index << CONN_SHIFT));
}

int fs_close(int fd) {
    struct conn *c = conn_of(&fd);
    if (!c) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = FS_CLOSE;
    m.data[0] = (uint64_t)fd;
    return (int)fs_request(c, &m);
}

long fs_read(int fd, uint32_t offset, void *buf, size_t len) {
    struct conn *c = conn_of(&fd);
    if (!c) {
        return -1;
    }
    size_t done = 0;
    while (done < len) {
        size_t chunk = len - done > FS_BUF_SIZE ? FS_BUF_SIZE : len - done;
        struct ipc_msg m = {};
        m.label = FS_READ;
        m.data[0] = (uint64_t)fd;
        m.data[1] = offset + done;
        m.data[2] = chunk;
        long n = fs_request(c, &m);
        if (n < 0) {
            return done > 0 ? (long)done : -1;
        }
        memcpy((char *)buf + done, c->buf, (size_t)n);
        done += (size_t)n;
        if ((size_t)n < chunk) {
            break;      // 文件末尾
        }
    }
    return (long)done;
}

long fs_write(int fd, uint32_t offset, const void *buf, size_t len) {
    struct conn *c = conn_of(&fd);
    if (!c) {
        return -1;
    }
    size_t done = 0;
    while (done < len) {
        size_t chunk = len - done > FS_BUF_SIZE ? FS_BUF_SIZE : len - done;
        memcpy(c->buf, (const char *)buf + done, chunk);
        struct ipc_msg m = {};
        m.label = FS_WRITE;
        m.data[0] = (uint64_t)fd;
        m.data[1] = offset + done;
        m.data[2] = chunk;
        long n = fs_request(c, &m);
        if (n <= 0) {
            return done > 0 ? (long)done : -1;
        }
        done += (size_t)n;
        if ((size_t)n < chunk) {
            break;      // 写不下了（磁盘满）
        }
    }
    return (long)done;
}

long fs_size(int fd) {
    struct conn *c = conn_of(&fd);
    if (!c) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = FS_SIZE;
    m.data[0] = (uint64_t)fd;
    return fs_request(c, &m);
}

int fs_unlink(const char *name) {
    int index;
    struct conn *c = put_name(name, &index);
    if (!c) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = FS_UNLINK;
    return (int)fs_request(c, &m);
}

int fs_list(const char *where, int index, char *name, uint32_t *size) {
    const char *rest = where ? where : "";
    struct conn *c = fs_connect(route(&rest));
    if (!c) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = FS_LIST;
    m.data[0] = (uint64_t)index;
    if (fs_request(c, &m) != 0) {
        return -1;
    }
    c->buf[FS_NAME_MAX - 1] = '\0';
    strcpy(name, c->buf);
    if (size) {
        *size = (uint32_t)m.data[1];
    }
    return 0;
}
