// sh - 一个很小的命令行
//
// 由 init 启动（非特权）。输入来自 uart 驱动。除了几个内置命令，一行的第一个词
// 被当作文件服务里的程序：读出它的 ELF 映像，fork 之后带着这一行的参数 exec。
// 行尾加 & 让程序在后台运行；前台程序运行期间按 Ctrl-C 终止它。
// 启动时先执行文件 "rc" 里的每一行。
//
// sh 从不长时间阻塞在别处：等前台程序时它带着超时去读串口，这样既能看到
// Ctrl-C，又能及时发现程序已经退出。

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
#define MAX_JOBS 8

#define CTRL_C      0x03
#define SIGINT      2
#define SIGKILL     9

static int uart = 0;            // uart 驱动的 PID

// ============================================================================
// 输入
// ============================================================================

// 已经从驱动读来、还没处理的字节（程序运行期间敲的字先存在这里）
static char pending[256];
static size_t pending_len = 0;

/** 从驱动读输入放进 pending，最多等 timeout_ms 毫秒（0 = 一直等）。@return 驱动还在不在 */
static bool fetch_input(uint32_t timeout_ms) {
    struct ipc_msg m = {};
    m.label = UART_READ;
    m.data[0] = timeout_ms;
    if (ipc_call(uart, &m) != 0) {
        return false;
    }
    const char *in = (const char *)&m.data[1];
    for (size_t i = 0; i < (size_t)m.data[0] && pending_len < sizeof(pending); i++) {
        pending[pending_len++] = in[i];
    }
    return true;
}

/** pending 里有 Ctrl-C 的话，把它和它之前的输入都丢掉 */
static bool take_ctrl_c(void) {
    for (size_t i = pending_len; i > 0; i--) {
        if (pending[i - 1] == CTRL_C) {
            memmove(pending, pending + i, pending_len - i);
            pending_len -= i;
            return true;
        }
    }
    return false;
}

// ============================================================================
// 后台任务
// ============================================================================

static struct {
    int pid;                    // 0 表示空闲
    char name[32];
} jobs[MAX_JOBS];

static void report_exit(const char *name, int status) {
    if (WIFSIGNALED(status)) {
        printf("%s: killed by signal %d\n", name, WTERMSIG(status));
    } else if (WEXITSTATUS(status) != 0 && WEXITSTATUS(status) != 126) {
        printf("%s: exit status %d\n", name, WEXITSTATUS(status));
    }
}

/** 回收已经结束的后台任务。@return 还有几个在运行 */
static int reap_jobs(void) {
    int running = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid == 0) {
            continue;
        }
        int status = 0;
        if (waitpid(jobs[i].pid, &status, WNOHANG) == jobs[i].pid) {
            printf("[%d] done  %s\n", jobs[i].pid, jobs[i].name);
            report_exit(jobs[i].name, status);
            jobs[i].pid = 0;
        } else {
            running++;
        }
    }
    return running;
}

static void cmd_jobs(void) {
    int count = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid != 0) {
            printf("[%d] running  %s\n", jobs[i].pid, jobs[i].name);
            count++;
        }
    }
    if (count == 0) {
        printf("(no background jobs)\n");
    }
}

static void cmd_kill(const char *arg) {
    int pid = atoi(arg);
    if (pid <= 0 || kill(pid, SIGKILL) != 0) {
        printf("kill: cannot kill %s\n", arg);
    }
}

// ============================================================================
// 运行程序
// ============================================================================

// argv[0] 是程序名，同时也是文件服务里的文件名
static void run_program(char **argv, bool background) {
    const char *name = argv[0];
    size_t size = 0;
    void *image = load_file(name, &size);
    if (!image) {
        printf("%s: unknown command (try help)\n", name);
        return;
    }

    int slot = -1;
    for (int i = 0; background && i < MAX_JOBS && slot < 0; i++) {
        if (jobs[i].pid == 0) {
            slot = i;
        }
    }
    if (background && slot < 0) {
        printf("%s: too many background jobs\n", name);
        munmap(image, size);
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

    if (background) {
        jobs[slot].pid = pid;
        strncpy(jobs[slot].name, name, sizeof(jobs[slot].name) - 1);
        jobs[slot].name[sizeof(jobs[slot].name) - 1] = '\0';
        printf("[%d] %s\n", pid, name);
        return;
    }

    // 前台：等它结束。期间短暂地去读串口，看有没有 Ctrl-C（别的输入留到它结束之后处理）
    int status = 0;
    bool interrupted = false;
    while (waitpid(pid, &status, WNOHANG) != pid) {
        if (uart > 0 && !fetch_input(20)) {
            uart = 0;           // 驱动没了：退回到单纯地等
        }
        if (uart <= 0) {
            waitpid(pid, &status, 0);
            break;
        }
        if (!interrupted && take_ctrl_c()) {
            printf("^C\n");
            kill(pid, SIGINT);
            interrupted = true;
        }
    }
    report_exit(name, status);
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
        printf("builtins: help, jobs, kill <pid>, write <file> <text>\n");
        printf("anything else runs a program from the file service with the rest of the\n");
        printf("line as its arguments, e.g.: ls, cat <file>, ping <ip>, http <host>, hello\n");
        printf("end a line with & to run it in the background; Ctrl-C stops the foreground program\n");
        return;
    }
    if (strcmp(line, "jobs") == 0) {
        cmd_jobs();
        return;
    }

    // 行尾的 &：后台运行
    bool background = false;
    size_t len = strlen(line);
    while (len > 0 && line[len - 1] == ' ') {
        line[--len] = '\0';
    }
    if (len > 0 && line[len - 1] == '&') {
        background = true;
        line[--len] = '\0';
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
    if (argc == 0) {
        return;
    }
    if (strcmp(argv[0], "kill") == 0) {
        if (argc == 2) {
            cmd_kill(argv[1]);
        } else {
            printf("usage: kill <pid>\n");
        }
        return;
    }
    run_program(argv, background);
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
    run_rc();       // 这时 uart 还是 0：rc 里的程序不能被 Ctrl-C 打断

    uart = name_wait("uart");
    if (uart <= 0) {
        printf("sh: cannot find the uart driver\n");
        return 1;
    }
    printf("sh: ready, reading commands from uart (pid %d); try help\n> ", uart);

    static char line[128];
    size_t len = 0;
    for (;;) {
        // 没有待处理的输入就去等：有后台任务时带着超时等，好及时报告它们结束
        if (pending_len == 0) {
            int running = reap_jobs();
            if (!fetch_input(running > 0 ? 200 : 0)) {
                printf("sh: uart driver is gone\n");
                return 1;
            }
            continue;
        }

        char c = pending[0];
        memmove(pending, pending + 1, --pending_len);

        if (c == '\r' || c == '\n') {
            console_write("\n", 1);
            line[len] = '\0';
            run_command(line);
            len = 0;
            reap_jobs();
            console_write("> ", 2);
        } else if (c == CTRL_C) {
            console_write("^C\n> ", 5);     // 放弃正在输入的这一行
            len = 0;
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
