/**
 * 文件服务的服务端骨架：句柄和请求分发。客户的缓冲区在 clients 里，存储交给 fs_backend。
 */

#include <fs_server.h>
#include <fs.h>
#include <names.h>
#include <clients.h>
#include <syscall.h>
#include <string.h>

#define MAX_HANDLES     64

struct handle {
    int owner;              // 打开它的客户 PID，0 表示空闲
    int file;               // 后端的文件号
};

static struct handle handles[MAX_HANDLES];
static const struct fs_backend *backend;

// 客户被回收时关掉它留下的句柄
static void drop_handles(int pid) {
    for (int i = 0; i < MAX_HANDLES; i++) {
        if (handles[i].owner == pid) {
            handles[i].owner = 0;
        }
    }
}

/** 句柄 fd 属于 pid 时返回它指向的文件号，否则 -1 */
static int file_of(int pid, uint64_t fd) {
    if (fd >= MAX_HANDLES || handles[fd].owner != pid) {
        return -1;
    }
    return handles[fd].file;
}

static int64_t do_open(int pid, char *buf, uint64_t flags) {
    buf[FS_NAME_MAX - 1] = '\0';
    const char *name = buf;
    if (name[0] == '\0') {
        return -1;
    }

    int file = backend->find(name);
    if (file < 0) {
        if (!(flags & FS_O_CREATE)) {
            return -1;
        }
        file = backend->create(name);
        if (file < 0) {
            return -1;
        }
    }
    if ((flags & FS_O_TRUNC) && backend->truncate(file) != 0) {
        return -1;
    }

    for (int i = 0; i < MAX_HANDLES; i++) {
        if (handles[i].owner == 0) {
            handles[i].owner = pid;
            handles[i].file = file;
            return i;
        }
    }
    return -1;
}

static int64_t do_unlink(char *buf) {
    buf[FS_NAME_MAX - 1] = '\0';
    int file = backend->find(buf);
    if (file < 0 || backend->remove(file) != 0) {
        return -1;
    }
    // 指向它的句柄一并失效
    for (int i = 0; i < MAX_HANDLES; i++) {
        if (handles[i].owner != 0 && handles[i].file == file) {
            handles[i].owner = 0;
        }
    }
    return 0;
}

int fs_serve(const char *service_name, const struct fs_backend *ops) {
    backend = ops;
    clients_init(FS_BUF_SIZE, drop_handles);
    if (name_register(service_name) != 0) {
        return -1;
    }

    struct ipc_msg m;
    for (;;) {
        if (ipc_recv(IPC_ANY, &m) != 0) {
            continue;
        }

        if (m.label == IPC_LABEL_GRANT) {
            clients_attach(&m);
            continue;       // 授予通知是单向的，不应答
        }

        struct ipc_msg reply = {};
        reply.label = m.label;
        int64_t result = -1;

        int pid = (int)m.sender;
        char *buf = clients_buf(pid);
        if (buf) {
            int file = file_of(pid, m.data[0]);         // 对不带句柄的请求没有意义
            switch (m.label) {
                case FS_OPEN:
                    result = do_open(pid, buf, m.data[0]);
                    break;
                case FS_CLOSE:
                    if (file >= 0) {
                        handles[m.data[0]].owner = 0;
                        result = 0;
                    }
                    break;
                case FS_READ:
                    if (file >= 0 && m.data[2] <= FS_BUF_SIZE && m.data[1] <= 0xFFFFFFFFu) {
                        result = backend->read(file, (uint32_t)m.data[1], buf, (uint32_t)m.data[2]);
                    }
                    break;
                case FS_WRITE:
                    if (file >= 0 && m.data[2] <= FS_BUF_SIZE && m.data[1] <= 0xFFFFFFFFu - FS_BUF_SIZE) {
                        result = backend->write(file, (uint32_t)m.data[1], buf, (uint32_t)m.data[2]);
                    }
                    break;
                case FS_SIZE:
                    if (file >= 0) {
                        result = backend->size(file);
                    }
                    break;
                case FS_UNLINK:
                    result = do_unlink(buf);
                    break;
                case FS_LIST: {
                    uint32_t size = 0;
                    if (m.data[0] <= 0x7FFFFFFF) {
                        result = backend->list((int)m.data[0], buf, &size);
                        reply.data[1] = size;
                    }
                    break;
                }
            }
        }

        reply.data[0] = (uint64_t)result;
        ipc_reply(m.sender, &reply);
    }
}
