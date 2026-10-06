// 标准输入和标准输出：控制台、文件或者管道（说明见 <stdio.h>）

#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <console.h>
#include <fs.h>

enum { IO_CONSOLE, IO_FILE, IO_PIPE };

struct stream {
    int kind;
    int fd;             // IO_FILE: 文件句柄
    uint32_t offset;    // IO_FILE: 下一次读写的位置
    int peer;           // IO_PIPE: 管道另一头的进程
};

static struct stream in;
static struct stream out;
static struct stream err;

// 已经从输入来源取来、还没交给程序的字节
static char pending[256];
static size_t pending_len = 0;
static size_t pending_pos = 0;

static void fail(const char *what, const char *name) {
    // 标准输出可能正是打不开的那个文件：直接写控制台
    console_write(what, strlen(what));
    console_write(name, strlen(name));
    console_write("\n", 1);
    exit(1);
}

// "f<名字>" / "a<名字>" / "p<pid>" / 空
static void open_stream(struct stream *s, const char *spec, size_t len, bool output) {
    static char name[FS_NAME_MAX];
    if (len == 0 || len - 1 >= sizeof(name)) {
        return;
    }
    memcpy(name, spec + 1, len - 1);
    name[len - 1] = '\0';

    if (spec[0] == 'p') {
        s->kind = IO_PIPE;
        s->peer = atoi(name);
    } else if (spec[0] == 'f' || (spec[0] == 'a' && output)) {
        int flags = !output ? 0 : spec[0] == 'a' ? FS_O_CREATE : FS_O_CREATE | FS_O_TRUNC;
        s->fd = fs_open(name, flags);
        if (s->fd < 0) {
            fail(output ? "cannot write " : "cannot read ", name);
        }
        long size = spec[0] == 'a' ? fs_size(s->fd) : 0;
        s->offset = size > 0 ? (uint32_t)size : 0;
        s->kind = IO_FILE;
    }
}

void stdio_setup(const char *spec) {
    if (spec[0] != STDIO_ARG_MARK) {
        return;
    }
    const char *input = spec + 1;
    const char *sep = strchr(input, STDIO_ARG_MARK);
    if (!sep) {
        return;
    }
    const char *output = sep + 1;
    const char *sep2 = strchr(output, STDIO_ARG_MARK);
    const char *sep3 = sep2 ? strchr(sep2 + 1, STDIO_ARG_MARK) : NULL;
    // 先换到命令行的当前目录：下面要打开的文件名可能是相对它写的
    if (sep3) {
        fs_set_cwd_spec(sep3 + 1);
    }
    open_stream(&in, input, (size_t)(sep - input), false);
    open_stream(&out, output, sep2 ? (size_t)(sep2 - output) : strlen(output), true);
    if (sep2 && sep2[1] != 'p') {
        open_stream(&err, sep2 + 1, sep3 ? (size_t)(sep3 - sep2 - 1) : strlen(sep2 + 1), true);
    }
}

static long write_stream(struct stream *s, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t done = 0;

    switch (s->kind) {
    case IO_FILE: {
        long n = fs_write(s->fd, s->offset, buf, len);
        if (n > 0) {
            s->offset += (uint32_t)n;
        }
        return n == (long)len ? n : -1;
    }
    case IO_PIPE:
        while (done < len) {
            struct ipc_msg m = {};
            size_t chunk = len - done > STDIO_DATA_MAX ? STDIO_DATA_MAX : len - done;
            m.label = STDIO_DATA;
            m.data[0] = chunk;
            memcpy(&m.data[1], p + done, chunk);
            if (ipc_send(s->peer, &m) != 0) {
                exit(1);        // 没有人读了：再写下去没有意义
            }
            done += chunk;
        }
        return (long)len;
    default:
        // 内核单次最多接受 4096 字节，分块写
        while (done < len) {
            size_t chunk = len - done > 4096 ? 4096 : len - done;
            if (console_write(p + done, chunk) <= 0) {
                return -1;
            }
            done += chunk;
        }
        return (long)len;
    }
}

long write_out(const void *buf, size_t len) {
    return write_stream(&out, buf, len);
}

long write_err(const void *buf, size_t len) {
    return write_stream(&err, buf, len);
}

// pending 空了就从输入来源再取一些。@return 还有没有输入
static bool fill_pending(void) {
    if (pending_pos < pending_len) {
        return true;
    }
    long n = 0;
    switch (in.kind) {
    case IO_FILE:
        n = fs_read(in.fd, in.offset, pending, sizeof(pending));
        if (n > 0) {
            in.offset += (uint32_t)n;
        }
        break;
    case IO_PIPE: {
        struct ipc_msg m;
        // 只认管道那一头发来的数据；它退出后 ipc_recv 失败，就是输入结束
        while (n == 0 && ipc_recv(in.peer, &m) == 0) {
            if (m.label == STDIO_DATA && m.data[0] <= STDIO_DATA_MAX) {
                n = (long)m.data[0];
                memcpy(pending, &m.data[1], (size_t)n);
            }
        }
        break;
    }
    default:
        // 键盘：一次取一行，补上换行符
        n = console_read_line(pending, sizeof(pending) - 1);
        if (n >= 0) {
            pending[n++] = '\n';
        }
        break;
    }
    pending_pos = 0;
    pending_len = n > 0 ? (size_t)n : 0;
    return pending_len > 0;
}

long read_input(char *buf, size_t len) {
    if (len == 0 || !fill_pending()) {
        return 0;
    }
    size_t n = pending_len - pending_pos < len ? pending_len - pending_pos : len;
    memcpy(buf, pending + pending_pos, n);
    pending_pos += n;
    return (long)n;
}

long read_line(char *buf, size_t size) {
    size_t len = 0;
    while (len + 1 < size) {
        if (!fill_pending()) {
            if (len == 0) {
                return -1;
            }
            break;              // 最后一行没有换行符
        }
        char c = pending[pending_pos++];
        if (c == '\n') {
            break;
        }
        buf[len++] = c;
    }
    buf[len] = '\0';
    return (long)len;
}
