/**
 * 服务进程的客户表：每个客户一块共享缓冲区
 */

#include <clients.h>

#define MAX_CLIENTS 16

static struct {
    int pid;            // 0 表示空闲
    char *buf;
} clients[MAX_CLIENTS];

static size_t buf_size;
static void (*drop_hook)(int pid);

void clients_init(size_t size, void (*on_drop)(int pid)) {
    buf_size = size;
    drop_hook = on_drop;
}

static int find(int pid) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].pid == pid) {
            return i;
        }
    }
    return -1;
}

char *clients_buf(int pid) {
    int i = pid > 0 ? find(pid) : -1;
    return i >= 0 ? clients[i].buf : NULL;
}

void clients_attach(const struct ipc_msg *grant) {
    int pid = (int)grant->sender;
    char *buf = (char *)(uintptr_t)grant->data[0];
    size_t size = (size_t)grant->data[1];

    if (size != buf_size) {
        munmap(buf, size);
        return;
    }

    int i = find(pid);
    if (i >= 0) {
        munmap(clients[i].buf, buf_size);   // 同一个客户换了缓冲区
        clients[i].buf = buf;
        return;
    }

    // 顺便清掉已经退出的客户，再找空位
    for (int j = 0; j < MAX_CLIENTS; j++) {
        if (clients[j].pid != 0 && kill(clients[j].pid, 0) != 0) {
            if (drop_hook) {
                drop_hook(clients[j].pid);
            }
            munmap(clients[j].buf, buf_size);
            clients[j].pid = 0;
        }
    }
    i = find(0);
    if (i < 0) {
        munmap(buf, size);
        return;
    }
    clients[i].pid = pid;
    clients[i].buf = buf;
}
