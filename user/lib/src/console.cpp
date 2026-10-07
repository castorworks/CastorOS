// 控制台输入的客户端：向 console 服务读字节，在此之上提供按行读取

#include <console.h>
#include <syscall.h>
#include <string.h>
#include <names.h>

static int driver = 0;                  // console 服务的 PID
static char input[CONSOLE_READ_MAX];    // 服务一次给的字节，还没被取走的部分
static size_t input_len = 0;
static size_t input_pos = 0;

long console_read(char *buf, size_t len, uint32_t timeout_ms) {
    if (len == 0) {
        return 0;
    }
    if (input_pos == input_len) {
        if (driver <= 0) {
            driver = name_lookup(CONSOLE_SERVICE_NAME);
        }
        struct ipc_msg m = {};
        m.label = CONSOLE_READ;
        m.data[0] = timeout_ms;
        if (driver <= 0 || ipc_call(driver, &m) != 0) {
            driver = 0;         // 服务不在了：下次重新按名字找（它可能被重启了）
            return -1;
        }
        if ((long)m.data[0] < 0) {
            return -1;
        }
        input_len = (size_t)m.data[0] <= CONSOLE_READ_MAX ? (size_t)m.data[0] : 0;
        input_pos = 0;
        memcpy(input, &m.data[1], input_len);
        if (input_len == 0) {
            return 0;
        }
    }
    size_t n = input_len - input_pos < len ? input_len - input_pos : len;
    memcpy(buf, input + input_pos, n);
    input_pos += n;
    return (long)n;
}

long console_read_line(char *buf, size_t size) {
    size_t len = 0;
    for (;;) {
        char c;
        if (console_read(&c, 1, 0) != 1) {
            break;
        }
        if (c == '\r' || c == '\n') {
            console_write("\n", 1);
            buf[len] = '\0';
            return (long)len;
        }
        if (c == CONSOLE_CTRL_D) {
            if (len == 0) {
                return -1;
            }
            break;              // 行中间的 Ctrl-D：把已经输入的部分交出去
        }
        if (c == 0x7F || c == '\b') {
            if (len > 0) {
                len--;
                console_write("\b \b", 3);
            }
        } else if ((unsigned char)c >= 0x20 && len + 1 < size) {
            buf[len++] = c;
            console_write(&c, 1);
        }
    }
    buf[len] = '\0';
    return len > 0 ? (long)len : -1;
}

long console_input(const char *chars, size_t n) {
    static int console = 0;     // console 服务的 PID
    if (n > CONSOLE_READ_MAX) {
        return -1;
    }
    // 记着的那个进程不在了就按名字再找一次：console 重启之后，第一批字符不该因为我们还
    // 记着旧的 PID 而丢掉
    for (int attempt = 0; attempt < 2; attempt++) {
        if (console <= 0) {
            console = name_lookup(CONSOLE_SERVICE_NAME);
        }
        struct ipc_msg m = {};
        m.label = CONSOLE_INPUT;
        m.data[0] = n;
        memcpy(&m.data[1], chars, n);
        if (console > 0 && ipc_call(console, &m) == 0) {
            return (long)m.data[0] == 0 ? 0 : -1;
        }
        console = 0;
    }
    return -1;
}

int console_debug_exit(const char *name) {
    int pid = name_lookup(name);
    struct ipc_msg m = {};
    m.label = CONSOLE_DEBUG_EXIT;
    if (pid <= 0 || ipc_call(pid, &m) != 0) {
        return -1;
    }
    if (pid == driver) {
        driver = 0;             // 我们自己读输入用的也是它：下次重新找
        input_len = input_pos = 0;
    }
    return 0;
}
