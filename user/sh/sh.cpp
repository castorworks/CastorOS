// sh - 一个很小的命令行
//
// 由 init 启动（非特权）。输入来自 uart 驱动。除了两个内置命令，一行的第一个词
// 被当作文件服务里的程序：读出它的 ELF 映像，fork 之后带着这一行的参数 exec。
// 启动时先执行文件 "rc" 里的每一行。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <names.h>
#include <fs.h>
#include <uart.h>

// ============================================================================
// 内置命令
// ============================================================================

// write <file> <text>：把这一行剩下的文字写进文件（命令行没有输出重定向，所以它留在这里）
static void cmd_write(char *args) {
    char *text = strchr(args, ' ');
    if (!text) {
        printf("usage: write <file> <text>\n");
        return;
    }
    *text++ = '\0';
    int fd = fs_open(args, FS_O_CREATE | FS_O_TRUNC);
    size_t len = strlen(text);
    if (fd < 0 || fs_write(fd, 0, text, len) != (long)len || fs_write(fd, (uint32_t)len, "\n", 1) != 1) {
        printf("write: %s: failed\n", args);
    }
    if (fd >= 0) {
        fs_close(fd);
    }
}

// ============================================================================
// 运行程序
// ============================================================================

// 把文件整个读进新映射的内存。成功返回地址并设置 *size，失败返回 NULL
static void *load_file(const char *name, size_t *size) {
    int fd = fs_open(name, 0);
    if (fd < 0) {
        return NULL;
    }
    long len = fs_size(fd);
    void *image = len > 0 ? mmap(NULL, (size_t)len, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
                          : MAP_FAILED;
    bool ok = image != MAP_FAILED && fs_read(fd, 0, image, (size_t)len) == len;
    fs_close(fd);
    if (!ok) {
        if (image != MAP_FAILED) {
            munmap(image, (size_t)len);
        }
        return NULL;
    }
    *size = (size_t)len;
    return image;
}

#define MAX_ARGS 16

// argv[0] 是程序名，同时也是文件服务里的文件名
static void run_program(char **argv) {
    const char *name = argv[0];
    size_t size = 0;
    void *image = load_file(name, &size);
    if (!image) {
        printf("%s: unknown command (try help)\n", name);
        return;
    }

    int pid = fork();
    if (pid == 0) {
        exec(image, size, argv);
        printf("%s: not an executable\n", name);
        exit(126);
    }
    munmap(image, size);
    if (pid < 0) {
        printf("%s: fork failed\n", name);
        return;
    }

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status)) {
        printf("%s: killed by signal %d\n", name, WTERMSIG(status));
    } else if (WEXITSTATUS(status) != 0 && WEXITSTATUS(status) != 126) {
        printf("%s: exit status %d\n", name, WEXITSTATUS(status));
    }
}

static void run_command(char *line) {
    while (*line == ' ') {
        line++;
    }
    if (*line == '\0' || *line == '#') {
        return;
    }

    if (strncmp(line, "write ", 6) == 0) {
        cmd_write(line + 6);
        return;
    }
    if (strcmp(line, "help") == 0) {
        printf("builtins: help, write <file> <text>\n");
        printf("anything else runs a program from the file service with the rest of the\n");
        printf("line as its arguments, e.g.: ls, cat <file>, rm <file>, echo <words>, hello\n");
        return;
    }

    // 按空格切成参数
    char *argv[MAX_ARGS + 1];
    int argc = 0;
    char *p = line;
    while (*p && argc < MAX_ARGS) {
        argv[argc++] = p;
        while (*p && *p != ' ') {
            p++;
        }
        while (*p == ' ') {
            *p++ = '\0';
        }
    }
    argv[argc] = NULL;
    run_program(argv);
}

// 执行启动脚本：文件 "rc" 里每行一条命令
static void run_rc(void) {
    size_t size = 0;
    char *script = (char *)load_file("rc", &size);
    if (!script) {
        return;
    }
    static char line[128];
    size_t len = 0;
    for (size_t i = 0; i <= size; i++) {
        char c = i < size ? script[i] : '\n';
        if (c == '\n') {
            line[len] = '\0';
            run_command(line);
            len = 0;
        } else if (len < sizeof(line) - 1) {
            line[len++] = c;
        }
    }
    munmap(script, size);
}

int main() {
    run_rc();

    int uart = name_wait("uart");
    if (uart <= 0) {
        printf("sh: cannot find the uart driver\n");
        return 1;
    }
    printf("sh: ready, reading commands from uart (pid %d); try help\n> ", uart);

    static char line[128];
    size_t len = 0;
    for (;;) {
        struct ipc_msg m = {};
        m.label = UART_READ;
        if (ipc_call(uart, &m) != 0) {
            printf("sh: uart driver is gone\n");
            return 1;
        }
        const char *in = (const char *)&m.data[1];
        for (size_t i = 0; i < (size_t)m.data[0]; i++) {
            char c = in[i];
            if (c == '\r' || c == '\n') {
                console_write("\n", 1);
                line[len] = '\0';
                run_command(line);
                len = 0;
                console_write("> ", 2);
            } else if (c == 0x7F || c == '\b') {
                if (len > 0) {
                    len--;
                    console_write("\b \b", 3);
                }
            } else if (len < sizeof(line) - 1) {
                line[len++] = c;
                console_write(&c, 1);
            }
        }
    }
}
