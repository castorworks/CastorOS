/**
 * 文件服务的服务端骨架：句柄、请求分发和目录的规则。客户的缓冲区在 clients 里，存储交给 fs_backend。
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

// ============================================================================
// 路径和目录
// ============================================================================

/**
 * 把客户给的路径整理成后端里存的样子：去掉开头和结尾的 '/'。
 * 中间不能有空的一段（"a//b"），也不能有 "." 和 ".."：没有"当前目录"，路径总是从根写起。
 * 整理后是空串表示根目录。@return 路径不合法返回 false
 */
static bool normalize(char *path) {
    path[FS_NAME_MAX - 1] = '\0';
    size_t len = strlen(path);
    size_t start = 0;
    while (path[start] == '/') {
        start++;
    }
    while (len > start && path[len - 1] == '/') {
        len--;
    }
    memmove(path, path + start, len - start);
    path[len - start] = '\0';

    for (const char *part = path; *part; ) {
        const char *end = strchr(part, '/');
        size_t n = end ? (size_t)(end - part) : strlen(part);
        if (n == 0 || (n == 1 && part[0] == '.') || (n == 2 && part[0] == '.' && part[1] == '.')) {
            return false;
        }
        part += n + (end ? 1 : 0);
    }
    return true;
}

/** path 是根目录（空串）或者一个存在的目录 */
static bool is_directory(const char *path) {
    if (path[0] == '\0') {
        return true;
    }
    int file = backend->find(path);
    return file >= 0 && backend->is_dir(file);
}

/** path 所在的目录存在（path 本身不必存在） */
static bool parent_exists(const char *path) {
    char parent[FS_NAME_MAX];
    strcpy(parent, path);
    char *slash = NULL;
    for (char *p = parent; *p; p++) {
        if (*p == '/') {
            slash = p;
        }
    }
    if (!slash) {
        return true;        // 直接在根目录下
    }
    *slash = '\0';
    return is_directory(parent);
}

/** child 是不是目录 dir 的直接成员；是的话 *leaf 指向它的最后一段名字 */
static bool is_child(const char *dir, const char *child, const char **leaf) {
    size_t n = strlen(dir);
    if (n > 0) {
        if (strncmp(child, dir, n) != 0 || child[n] != '/') {
            return false;
        }
        child += n + 1;
    }
    if (child[0] == '\0' || strchr(child, '/')) {
        return false;       // 是它自己，或者在更深的子目录里
    }
    *leaf = child;
    return true;
}

/** 目录 dir 的第 index 个直接成员。@return 文件号，-1 没有这么多 */
static int child_at(const char *dir, int index, const char **leaf) {
    int file;
    for (int i = 0; (file = backend->entry(i)) >= 0; i++) {
        if (is_child(dir, backend->path(file), leaf) && index-- == 0) {
            return file;
        }
    }
    return -1;
}

// ============================================================================
// 请求
// ============================================================================

static int64_t do_open(int pid, char *path, uint64_t flags) {
    if (!normalize(path) || path[0] == '\0') {
        return -1;
    }

    int file = backend->find(path);
    if (file >= 0 && backend->is_dir(file)) {
        return -1;          // 目录不能当文件打开
    }
    if (file < 0) {
        if (!(flags & FS_O_CREATE) || !parent_exists(path)) {
            return -1;
        }
        file = backend->create(path, false);
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

static int64_t do_mkdir(char *path) {
    if (!normalize(path) || path[0] == '\0' || backend->find(path) >= 0 || !parent_exists(path)) {
        return -1;
    }
    return backend->create(path, true) >= 0 ? 0 : -1;
}

static int64_t do_unlink(char *path) {
    if (!normalize(path)) {
        return -1;
    }
    int file = backend->find(path);
    if (file < 0) {
        return -1;
    }
    const char *leaf;
    if (backend->is_dir(file) && child_at(path, 0, &leaf) >= 0) {
        return -1;          // 目录里还有东西
    }
    if (backend->remove(file) != 0) {
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

/** 列出目录 buf 的第 index 个成员：名字（最后一段）写回 buf */
static int64_t do_list(char *buf, uint64_t index, uint64_t *size, uint64_t *directory) {
    char dir[FS_NAME_MAX];
    if (!normalize(buf) || !is_directory(buf) || index > 0x7FFFFFFF) {
        return -1;
    }
    strcpy(dir, buf);
    const char *leaf;
    int file = child_at(dir, (int)index, &leaf);
    if (file < 0) {
        return -1;
    }
    *size = (uint64_t)backend->size(file);
    *directory = backend->is_dir(file);
    strcpy(buf, leaf);
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
                case FS_MKDIR:
                    result = do_mkdir(buf);
                    break;
                case FS_LIST:
                    result = do_list(buf, m.data[0], &reply.data[1], &reply.data[2]);
                    break;
            }
        }

        reply.data[0] = (uint64_t)result;
        ipc_reply(m.sender, &reply);
    }
}
