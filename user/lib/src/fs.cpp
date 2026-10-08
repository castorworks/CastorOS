/**
 * 文件服务的客户端：按路径把请求发给对应的服务进程，内容经共享缓冲区传递
 */

#include <fs.h>
#include <names.h>
#include <syscall.h>
#include <string.h>
#include <stdio.h>

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
#define FS_RAM  0
#define FS_DISK 1

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

// 当前目录：从根写起的路径（根是空串）
static char cwd_path[FS_NAME_MAX] = "";

/**
 * 根在磁盘上吗。只问一次，问的是有定论的答案：开机时磁盘文件服务可能还在挂载，
 * 这里一直等到它登记了，或者退出了（没有磁盘，或者盘上没有我们的文件系统）。
 */
static bool root_on_disk(void) {
    static int known = -1;
    if (known < 0) {
        known = name_settle(FS_DISK_SERVICE_NAME) > 0;
    }
    return known;
}

/** 从根写起的路径 path 归哪个服务：根在磁盘上时，只有 /tmp 这棵子树在内存里 */
static int service_of(const char *path) {
    size_t n = strlen(FS_TMP_DIR);
    bool in_tmp = strncmp(path, FS_TMP_DIR, n) == 0 && (path[n] == '\0' || path[n] == '/');
    return root_on_disk() && !in_tmp ? FS_DISK : FS_RAM;
}

/**
 * 把调用者给的路径变成"哪个服务 + 从根写起的完整路径"（不带开头的 '/'，根是空串）：
 * 接上当前目录、把 "." 和 ".." 算掉。服务端不认识这些，它只收完整的路径。
 * @return 路径太长，或者 ".." 走到了根的上面，返回 false
 */
static bool resolve(const char *path, int *fs, char *out) {
    size_t len = 0;
    out[0] = '\0';
    if (path[0] != '/') {               // 相对路径：从当前目录出发
        strcpy(out, cwd_path);
        len = strlen(out);
    }

    while (*path) {
        while (*path == '/') {
            path++;
        }
        size_t n = 0;
        while (path[n] && path[n] != '/') {
            n++;
        }
        if (n == 0 || (n == 1 && path[0] == '.')) {
            // 空的一段或者 "."：原地不动
        } else if (n == 2 && path[0] == '.' && path[1] == '.') {
            if (len == 0) {
                return false;           // 根没有上一级
            }
            while (len > 0 && out[len - 1] != '/') {
                len--;
            }
            if (len > 0) {
                len--;                  // 连同前面的 '/' 一起去掉
            }
            out[len] = '\0';
        } else {
            if (len + (len > 0) + n >= FS_NAME_MAX) {
                return false;
            }
            if (len > 0) {
                out[len++] = '/';
            }
            memcpy(out + len, path, n);
            len += n;
            out[len] = '\0';
        }
        path += n;
    }
    *fs = service_of(out);
    return true;
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
        // 服务不在了（崩溃后可能被 init 重启成另一个进程）：忘掉这条连接，下一次调用
        // 重新按名字找。原来打开的句柄在新的服务那里不存在
        munmap(c->buf, FS_BUF_SIZE);
        c->buf = NULL;
        c->server = 0;
        return -1;
    }
    return (long)(int64_t)m->data[0];
}

/** 选好连接并把完整的路径放进共享缓冲区。根目录（空路径）不是一个能打开、能删的东西 */
static struct conn *put_name(const char *name, int *index) {
    char full[FS_NAME_MAX];
    if (!name || !resolve(name, index, full) || full[0] == '\0') {
        return NULL;
    }
    struct conn *c = fs_connect(*index);
    if (c) {
        strcpy(c->buf, full);
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

int fs_mkdir(const char *path) {
    int index;
    struct conn *c = put_name(path, &index);
    if (!c) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = FS_MKDIR;
    return (int)fs_request(c, &m);
}

int fs_rename(const char *from, const char *to) {
    char old_path[FS_NAME_MAX], new_path[FS_NAME_MAX];
    int old_fs, new_fs;
    if (!from || !to || !resolve(from, &old_fs, old_path) || !resolve(to, &new_fs, new_path) ||
        old_fs != new_fs || old_path[0] == '\0' || new_path[0] == '\0') {
        return -1;
    }
    struct conn *c = fs_connect(old_fs);
    if (!c) {
        return -1;
    }
    // 两个路径一前一后放在缓冲区里，中间隔一个 '\0'
    strcpy(c->buf, old_path);
    strcpy(c->buf + strlen(old_path) + 1, new_path);
    struct ipc_msg m = {};
    m.label = FS_RENAME;
    return (int)fs_request(c, &m);
}

int fs_list(const char *dir, int index, char *name, uint32_t *size, bool *is_dir) {
    char full[FS_NAME_MAX];
    int fs;
    if (!resolve(dir ? dir : "", &fs, full)) {
        return -1;
    }
    struct conn *c = fs_connect(fs);
    if (!c) {
        return -1;
    }
    strcpy(c->buf, full);       // 空串是根目录
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
    if (is_dir) {
        *is_dir = m.data[2] != 0;
    }
    return 0;
}

// ============================================================================
// 当前目录
// ============================================================================

int fs_chdir(const char *path) {
    char full[FS_NAME_MAX];
    int fs;
    if (!path || !resolve(path, &fs, full)) {
        return -1;
    }
    if (full[0] != '\0') {
        // 它得是一个存在的目录：在它的上一级里找到它，看是不是目录
        char parent[FS_NAME_MAX + 1] = "/";
        char *slash = NULL;
        for (char *p = full; *p; p++) {
            if (*p == '/') {
                slash = p;
            }
        }
        const char *leaf = slash ? slash + 1 : full;
        if (slash) {
            memcpy(parent + 1, full, (size_t)(slash - full));
            parent[1 + (size_t)(slash - full)] = '\0';
        }
        char name[FS_NAME_MAX];
        bool is_dir = false, found = false;
        for (int i = 0; !found && fs_list(parent, i, name, NULL, &is_dir) == 0; i++) {
            found = strcmp(name, leaf) == 0;
        }
        if (!found || !is_dir) {
            return -1;
        }
    }
    strcpy(cwd_path, full);
    return 0;
}

void fs_getcwd(char *buf) {
    snprintf(buf, FS_NAME_MAX + 8, "/%s", cwd_path);
}

const char *fs_cwd_spec(void) {
    return cwd_path;
}

void fs_set_cwd_spec(const char *spec) {
    if (strlen(spec) < FS_NAME_MAX) {
        strcpy(cwd_path, spec);
    }
}
