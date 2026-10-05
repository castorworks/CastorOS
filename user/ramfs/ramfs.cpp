// ramfs - 内存文件系统服务
//
// 非特权的用户态服务，以 "fs" 登记，实现 fs.h 里的协议。文件内容放在自己
// mmap 来的内存里；命名空间是平的。每个客户有一块共享缓冲区（客户用 mem_grant
// 送来），文件名和读写的数据都经过它。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <names.h>
#include <fs.h>

#define MAX_FILES       32
#define MAX_HANDLES     64
#define MAX_CLIENTS     16
#define MAX_FILE_SIZE   (4u * 1024 * 1024)

struct file {
    bool used;
    char name[FS_NAME_MAX];
    char *data;             // mmap 来的，capacity 字节
    uint32_t size;
    uint32_t capacity;
};

struct handle {
    int owner;              // 打开它的客户 PID，0 表示空闲
    int file;               // files[] 的下标
};

struct client {
    int pid;                // 0 表示空闲
    char *buf;              // 共享缓冲区在本进程里的地址
};

static struct file files[MAX_FILES];
static struct handle handles[MAX_HANDLES];
static struct client clients[MAX_CLIENTS];

// ============================================================================
// 客户
// ============================================================================

static struct client *find_client(int pid) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].pid == pid) {
            return &clients[i];
        }
    }
    return NULL;
}

static void drop_client(struct client *c) {
    for (int i = 0; i < MAX_HANDLES; i++) {
        if (handles[i].owner == c->pid) {
            handles[i].owner = 0;
        }
    }
    munmap(c->buf, FS_BUF_SIZE);
    c->pid = 0;
}

// 客户送来了共享缓冲区（内核担保的授予通知）
static void attach_client(int pid, char *buf, size_t size) {
    if (size != FS_BUF_SIZE) {
        munmap(buf, size);
        return;
    }

    struct client *c = find_client(pid);
    if (c) {
        munmap(c->buf, FS_BUF_SIZE);    // 同一个客户换了缓冲区
        c->buf = buf;
        return;
    }

    // 顺便清掉已经退出的客户，再找空位
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].pid != 0 && kill(clients[i].pid, 0) != 0) {
            drop_client(&clients[i]);
        }
    }
    c = find_client(0);
    if (!c) {
        munmap(buf, size);
        return;
    }
    c->pid = pid;
    c->buf = buf;
}

// ============================================================================
// 文件
// ============================================================================

static int find_file(const char *name) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int create_file(const char *name) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (!files[i].used) {
            memset(&files[i], 0, sizeof(files[i]));
            files[i].used = true;
            strcpy(files[i].name, name);
            return i;
        }
    }
    return -1;
}

// 保证文件至少能放下 size 字节
static bool reserve(struct file *f, uint32_t size) {
    if (size <= f->capacity) {
        return true;
    }
    uint32_t capacity = f->capacity ? f->capacity : 4096;
    while (capacity < size) {
        capacity *= 2;
    }
    char *data = (char *)mmap(NULL, capacity, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (data == MAP_FAILED) {
        return false;
    }
    if (f->data) {
        memcpy(data, f->data, f->size);
        munmap(f->data, f->capacity);
    }
    f->data = data;
    f->capacity = capacity;
    return true;
}

static void remove_file(int index) {
    for (int i = 0; i < MAX_HANDLES; i++) {
        if (handles[i].owner != 0 && handles[i].file == index) {
            handles[i].owner = 0;
        }
    }
    if (files[index].data) {
        munmap(files[index].data, files[index].capacity);
    }
    files[index].used = false;
}

// 句柄 fd 属于 pid 时返回它指向的文件
static struct file *file_of(int pid, uint64_t fd) {
    if (fd >= MAX_HANDLES || handles[fd].owner != pid) {
        return NULL;
    }
    return &files[handles[fd].file];
}

// ============================================================================
// 请求处理：返回值写进应答的 data[0]
// ============================================================================

static int64_t do_open(struct client *c, uint64_t flags) {
    c->buf[FS_NAME_MAX - 1] = '\0';
    const char *name = c->buf;
    if (name[0] == '\0') {
        return -1;
    }

    int index = find_file(name);
    if (index < 0) {
        if (!(flags & FS_O_CREATE)) {
            return -1;
        }
        index = create_file(name);
        if (index < 0) {
            return -1;
        }
    }
    if (flags & FS_O_TRUNC) {
        files[index].size = 0;
    }

    for (int i = 0; i < MAX_HANDLES; i++) {
        if (handles[i].owner == 0) {
            handles[i].owner = c->pid;
            handles[i].file = index;
            return i;
        }
    }
    return -1;
}

static int64_t do_read(struct client *c, uint64_t fd, uint64_t offset, uint64_t len) {
    struct file *f = file_of(c->pid, fd);
    if (!f || len > FS_BUF_SIZE) {
        return -1;
    }
    if (offset >= f->size) {
        return 0;
    }
    if (len > f->size - offset) {
        len = f->size - offset;
    }
    memcpy(c->buf, f->data + offset, (size_t)len);
    return (int64_t)len;
}

static int64_t do_write(struct client *c, uint64_t fd, uint64_t offset, uint64_t len) {
    struct file *f = file_of(c->pid, fd);
    if (!f || len > FS_BUF_SIZE || offset > MAX_FILE_SIZE || offset + len > MAX_FILE_SIZE ||
        !reserve(f, (uint32_t)(offset + len))) {
        return -1;
    }
    if (offset > f->size) {
        memset(f->data + f->size, 0, (size_t)(offset - f->size));   // 空洞填 0
    }
    memcpy(f->data + offset, c->buf, (size_t)len);
    if (offset + len > f->size) {
        f->size = (uint32_t)(offset + len);
    }
    return (int64_t)len;
}

static int64_t do_list(struct client *c, uint64_t index, uint64_t *size) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (!files[i].used) {
            continue;
        }
        if (index == 0) {
            strcpy(c->buf, files[i].name);
            *size = files[i].size;
            return 0;
        }
        index--;
    }
    return -1;
}

int main() {
    if (name_register(FS_SERVICE_NAME) != 0) {
        printf("ramfs: cannot register name\n");
        return 1;
    }
    printf("ramfs: ready (pid %d)\n", getpid());

    struct ipc_msg m;
    for (;;) {
        if (ipc_recv(IPC_ANY, &m) != 0) {
            continue;
        }

        if (m.label == IPC_LABEL_GRANT) {
            attach_client((int)m.sender, (char *)(uintptr_t)m.data[0], (size_t)m.data[1]);
            continue;       // 授予通知是单向的，不应答
        }

        struct ipc_msg reply = {};
        reply.label = m.label;
        int64_t result = -1;

        struct client *c = find_client((int)m.sender);
        if (c) {
            switch (m.label) {
                case FS_OPEN:
                    result = do_open(c, m.data[0]);
                    break;
                case FS_CLOSE:
                    if (file_of(c->pid, m.data[0])) {
                        handles[m.data[0]].owner = 0;
                        result = 0;
                    }
                    break;
                case FS_READ:
                    result = do_read(c, m.data[0], m.data[1], m.data[2]);
                    break;
                case FS_WRITE:
                    result = do_write(c, m.data[0], m.data[1], m.data[2]);
                    break;
                case FS_UNLINK: {
                    c->buf[FS_NAME_MAX - 1] = '\0';
                    int index = find_file(c->buf);
                    if (index >= 0) {
                        remove_file(index);
                        result = 0;
                    }
                    break;
                }
                case FS_LIST:
                    result = do_list(c, m.data[0], &reply.data[1]);
                    break;
            }
        }

        reply.data[0] = (uint64_t)result;
        ipc_reply(m.sender, &reply);
    }
}
