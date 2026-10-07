// console - 终端输入服务
//
// 终端的输入都汇到这里，再通过 IPC 交给读者。读者有两个：终端的主人（命令行）和它指定的
// 前台进程，协议见 <console.h>。输出不经过这里：那是内核的 console_write。
//
// 本进程不碰硬件。输入是输入设备的驱动送来的（CONSOLE_INPUT）：串口的驱动 uart，PC 上
// 还有键盘的驱动 kbd。两边来的字符进同一个缓冲区，从这里起没有区别。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <names.h>
#include <console.h>

// 环形缓冲区
#define RING_SIZE 256
struct ring {
    char buf[RING_SIZE];
    uint32_t head;      // 下一个写入位置
    uint32_t tail;      // 下一个读取位置
};

static bool ring_empty(const struct ring *r) {
    return r->head == r->tail;
}

static void ring_put(struct ring *r, char c) {
    uint32_t next = (r->head + 1) % RING_SIZE;
    if (next != r->tail) {      // 满了就丢弃
        r->buf[r->head] = c;
        r->head = next;
    }
}

static char ring_get(struct ring *r) {
    char c = r->buf[r->tail];
    r->tail = (r->tail + 1) % RING_SIZE;
    return c;
}

// 在等输入的读者
struct reader {
    int pid;            // 0 表示没有人在等
    uint64_t deadline;  // 最晚等到什么时候（开机以来的毫秒数），0 表示一直等
};

static int owner = 0;           // 终端的主人（命令行）
static int foreground = 0;      // 主人指定的前台进程，0 表示没有

static struct ring owner_input;         // 给主人的输入
static struct ring program_input;       // 给前台进程的输入
static struct reader owner_reader;
static struct reader program_reader;

// 一个新到的字节该给谁：有前台进程时归它，但 Ctrl-C 总是给主人
static void route(char c) {
    ring_put(foreground != 0 && c != CONSOLE_CTRL_C ? &program_input : &owner_input, c);
}

static void reply_value(int pid, long value) {
    struct ipc_msg m = {};
    m.data[0] = (uint64_t)value;
    ipc_reply(pid, &m);
}

// 有数据时应答在等的读者。by_line：一次最多给到一行的结尾（换行或 Ctrl-D）为止，
// 这样程序读完自己要的那几行就退出时，后面的输入还在我们这里，可以还给主人
static void serve(struct reader *reader, struct ring *input, bool by_line) {
    if (reader->pid == 0 || ring_empty(input)) {
        return;
    }
    struct ipc_msg m = {};
    m.label = CONSOLE_READ;
    char *out = (char *)&m.data[1];
    uint32_t n = 0;
    while (n < CONSOLE_READ_MAX && !ring_empty(input)) {
        char c = ring_get(input);
        out[n++] = c;
        if (by_line && (c == '\n' || c == '\r' || c == CONSOLE_CTRL_D)) {
            break;
        }
    }
    m.data[0] = n;
    ipc_reply(reader->pid, &m);
    reader->pid = 0;
}

// 读者等到时间了就告诉它没有输入。@return 它还要等多少毫秒，0 表示不用为它定时
static uint64_t check_deadline(struct reader *reader, uint64_t now) {
    if (reader->pid == 0 || reader->deadline == 0) {
        return 0;
    }
    if (now >= reader->deadline) {
        reply_value(reader->pid, 0);
        reader->pid = 0;
        return 0;
    }
    return reader->deadline - now;
}

static void set_foreground(int pid) {
    if (pid != 0) {
        // 主人还没读走的输入是敲给这个程序的；Ctrl-C 留给主人
        struct ring rest = {};
        while (!ring_empty(&owner_input)) {
            char c = ring_get(&owner_input);
            ring_put(c == CONSOLE_CTRL_C ? &rest : &program_input, c);
        }
        owner_input = rest;
    } else {
        // 前台进程结束了：它没读完的输入还给主人，在等的读者（多半已经不在了）不再等
        if (program_reader.pid != 0) {
            reply_value(program_reader.pid, -1);
            program_reader.pid = 0;
        }
        owner_input = program_input;
        program_input = {};
    }
    foreground = pid;
}

/** sender 是输入设备的驱动吗？它们的名字是 init 留给它们的，别的进程登记不了 */
static bool is_input_driver(int sender) {
    static const char *const names[] = { UART_NAME, KBD_NAME };
    static int drivers[2];
    for (int i = 0; i < 2; i++) {
        if (sender == drivers[i]) {
            return true;
        }
    }
    // 第一次见到它，或者它被重启过
    for (int i = 0; i < 2; i++) {
        drivers[i] = name_lookup(names[i]);
        if (sender == drivers[i]) {
            return true;
        }
    }
    return false;
}

static void handle_request(const struct ipc_msg *m) {
    int sender = (int)m->sender;
    switch (m->label) {
    case CONSOLE_READ: {
        struct reader *reader = sender == owner ? &owner_reader
                              : sender == foreground ? &program_reader : NULL;
        if (!reader) {
            reply_value(sender, -1);
            return;
        }
        reader->pid = sender;
        reader->deadline = m->data[0] ? uptime_ms() + m->data[0] : 0;
        return;
    }
    case CONSOLE_ATTACH:
        if (owner != 0 && owner != sender && kill(owner, 0) == 0) {
            reply_value(sender, -1);
            return;
        }
        owner = sender;
        owner_reader.pid = 0;
        if (foreground != 0) {
            set_foreground(0);      // 原来的主人留下的前台进程：它的输入归新主人
        }
        reply_value(sender, 0);
        return;
    case CONSOLE_SET_FOREGROUND:
        if (sender != owner) {
            reply_value(sender, -1);
            return;
        }
        set_foreground((int)m->data[0]);
        reply_value(sender, 0);
        return;
    case CONSOLE_UNREAD: {
        if (sender != owner || m->data[0] > CONSOLE_READ_MAX) {
            reply_value(sender, -1);
            return;
        }
        const char *in = (const char *)&m->data[1];
        for (uint32_t i = 0; i < (uint32_t)m->data[0]; i++) {
            ring_put(in[i] == CONSOLE_CTRL_C ? &owner_input : &program_input, in[i]);
        }
        reply_value(sender, 0);
        return;
    }
    case CONSOLE_INPUT: {
        if (m->data[0] > CONSOLE_READ_MAX || !is_input_driver(sender)) {
            reply_value(sender, -1);
            return;
        }
        const char *in = (const char *)&m->data[1];
        for (uint32_t i = 0; i < (uint32_t)m->data[0]; i++) {
            route(in[i]);
        }
        reply_value(sender, 0);
        return;
    }
    case CONSOLE_DEBUG_EXIT:
        reply_value(sender, 0);
        printf("console: exiting on request (CONSOLE_DEBUG_EXIT)\n");
        exit(1);
    default:
        return;     // 不认识的请求：不应答
    }
}

int main() {
    if (name_register(CONSOLE_SERVICE_NAME) != 0) {
        printf("console: cannot register name\n");
        return 1;
    }
    printf("console: ready (pid %d)\n", getpid());

    struct ipc_msg m;
    for (;;) {
        if (ipc_recv(IPC_ANY, &m) != 0) {
            continue;
        }

        // 内核发来的只有定时器（IPC_LABEL_TIMER）：下面统一检查读者是否超时
        if (m.sender != IPC_KERNEL) {
            handle_request(&m);
        }

        serve(&owner_reader, &owner_input, false);
        serve(&program_reader, &program_input, true);

        // 带着超时在等的读者：到时间了就应答，否则让定时器到时候叫醒我们
        uint64_t now = uptime_ms();
        uint64_t a = check_deadline(&owner_reader, now);
        uint64_t b = check_deadline(&program_reader, now);
        uint64_t next = a != 0 && (b == 0 || a < b) ? a : b;
        if (next != 0) {
            timer_set((uint32_t)next);
        }
    }
}
