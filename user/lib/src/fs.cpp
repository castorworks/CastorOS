/**
 * 文件服务的客户端：把请求发给登记为 "fs" 的服务进程，内容经共享缓冲区传递
 */

#include <fs.h>
#include <names.h>
#include <syscall.h>
#include <string.h>

static int fs_server = 0;       // 服务进程的 PID
static char *fs_buf = NULL;     // 与服务共享的缓冲区
static int fs_owner = 0;        // 建立这条连接的进程：fork 出来的子进程要自己重新建立

/** 确保当前进程与文件服务之间有共享缓冲区 */
static bool fs_connect(void) {
    int self = getpid();
    if (fs_buf && fs_owner == self) {
        return true;
    }

    int server = name_wait(FS_SERVICE_NAME);
    if (server <= 0) {
        return false;
    }
    char *buf = (char *)mmap(NULL, FS_BUF_SIZE, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED) {
        return false;
    }
    // 服务从内核发出的授予通知里得知缓冲区在它那边的位置
    if (mem_grant(server, buf, FS_BUF_SIZE) != 0) {
        munmap(buf, FS_BUF_SIZE);
        return false;
    }

    fs_server = server;
    fs_buf = buf;
    fs_owner = self;
    return true;
}

/** 发一个请求，返回应答的 data[0]；m 里带回完整应答 */
static long fs_request(struct ipc_msg *m) {
    if (ipc_call(fs_server, m) != 0) {
        return -1;
    }
    return (long)(int64_t)m->data[0];
}

/** 把文件名放进共享缓冲区 */
static bool put_name(const char *name) {
    if (!name || name[0] == '\0' || strlen(name) >= FS_NAME_MAX || !fs_connect()) {
        return false;
    }
    strcpy(fs_buf, name);
    return true;
}

int fs_open(const char *name, int flags) {
    if (!put_name(name)) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = FS_OPEN;
    m.data[0] = (uint64_t)flags;
    return (int)fs_request(&m);
}

int fs_close(int fd) {
    if (!fs_connect()) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = FS_CLOSE;
    m.data[0] = (uint64_t)fd;
    return (int)fs_request(&m);
}

long fs_read(int fd, uint32_t offset, void *buf, size_t len) {
    if (!fs_connect()) {
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
        long n = fs_request(&m);
        if (n < 0) {
            return done > 0 ? (long)done : -1;
        }
        memcpy((char *)buf + done, fs_buf, (size_t)n);
        done += (size_t)n;
        if ((size_t)n < chunk) {
            break;      // 文件末尾
        }
    }
    return (long)done;
}

long fs_write(int fd, uint32_t offset, const void *buf, size_t len) {
    if (!fs_connect()) {
        return -1;
    }
    size_t done = 0;
    while (done < len) {
        size_t chunk = len - done > FS_BUF_SIZE ? FS_BUF_SIZE : len - done;
        memcpy(fs_buf, (const char *)buf + done, chunk);
        struct ipc_msg m = {};
        m.label = FS_WRITE;
        m.data[0] = (uint64_t)fd;
        m.data[1] = offset + done;
        m.data[2] = chunk;
        long n = fs_request(&m);
        if (n <= 0) {
            return done > 0 ? (long)done : -1;
        }
        done += (size_t)n;
    }
    return (long)done;
}

int fs_unlink(const char *name) {
    if (!put_name(name)) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = FS_UNLINK;
    return (int)fs_request(&m);
}

int fs_list(int index, char *name, uint32_t *size) {
    if (!fs_connect()) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = FS_LIST;
    m.data[0] = (uint64_t)index;
    if (fs_request(&m) != 0) {
        return -1;
    }
    fs_buf[FS_NAME_MAX - 1] = '\0';
    strcpy(name, fs_buf);
    if (size) {
        *size = (uint32_t)m.data[1];
    }
    return 0;
}
