// sh - 一个很小的命令行
//
// 由 init 启动（非特权）。输入来自 uart 驱动。除了几个内置命令，一行的第一个词
// 被当作文件服务里的程序：读出它的 ELF 映像，fork 之后带着这一行的参数 exec。
// 行尾加 & 让程序在后台运行；前台程序运行期间按 Ctrl-C 终止它。
// cmd < in > out 把程序的标准输入/输出换成文件，cmd1 | cmd2 把前一个的输出接到后一个的输入。
// 启动时先执行文件 "rc" 里的每一行。
//
// sh 是终端的主人（见 <console.h>）：程序在前台运行期间，键盘输入归那个程序，
// sh 只收到 Ctrl-C。
//
// sh 从不长时间阻塞在别处：等前台程序时它带着超时去读串口，这样既能看到
// Ctrl-C，又能及时发现程序已经退出。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <names.h>
#include <fs.h>
#include <console.h>

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

#define CTRL_C      CONSOLE_CTRL_C
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
    if (ipc_call(uart, &m) != 0 || (long)m.data[0] < 0) {
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

/** 告诉驱动谁在前台（0 = 没有）。设置之前，把已经读来还没处理的输入退回去留给它 */
static void set_foreground(int pid) {
    if (uart <= 0) {
        return;
    }
    struct ipc_msg m;
    for (size_t done = 0; pid != 0 && done < pending_len; ) {
        size_t n = pending_len - done < UART_READ_MAX ? pending_len - done : UART_READ_MAX;
        m = {};
        m.label = UART_UNREAD;
        m.data[0] = n;
        memcpy(&m.data[1], pending + done, n);
        ipc_call(uart, &m);
        done += n;
    }
    if (pid != 0) {
        pending_len = 0;
    }
    m = {};
    m.label = UART_SET_FOREGROUND;
    m.data[0] = (uint64_t)pid;
    ipc_call(uart, &m);
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

#define MAX_STAGES 4

// 管道里的一段：一个程序，加上它的标准输入/输出来自哪里
struct stage {
    char *argv[MAX_ARGS + 2];   // argv[0] 是程序名，也是文件服务里的文件名；留一格给输入输出说明
    int argc;
    const char *in_file;        // < file
    const char *out_file;       // > file 或 >> file
    bool append;
    void *image;
    size_t image_size;
    int pid;
    int status;
    bool exited;
};

// 在 fork 出来的子进程里：换成这一段的程序。标准输入/输出不是控制台时，
// 在参数最后附上说明（格式见 <stdio.h>），由新程序的启动代码去设置
static void exec_stage(struct stage *st, int in_pid, int out_pid) {
    static char spec[2 * FS_NAME_MAX + 8];
    char in[FS_NAME_MAX + 2] = "";
    char out[FS_NAME_MAX + 2] = "";
    if (st->in_file) {
        snprintf(in, sizeof(in), "f%s", st->in_file);
    } else if (in_pid != 0) {
        snprintf(in, sizeof(in), "p%d", in_pid);
    }
    if (st->out_file) {
        snprintf(out, sizeof(out), "%c%s", st->append ? 'a' : 'f', st->out_file);
    } else if (out_pid != 0) {
        snprintf(out, sizeof(out), "p%d", out_pid);
    }
    if (in[0] || out[0]) {
        snprintf(spec, sizeof(spec), "%c%s%c%s", STDIO_ARG_MARK, in, STDIO_ARG_MARK, out);
        st->argv[st->argc] = spec;
        st->argv[st->argc + 1] = NULL;
    }
    exec(st->image, st->image_size, st->argv);
    printf("%s: not an executable\n", st->argv[0]);
    exit(126);
}

// 运行一条管道（只有一段时就是一个程序）
static void run_pipeline(struct stage *stages, int count, bool background) {
    // 先把每一段的程序都读进来：有一个找不到就什么都不运行
    for (int i = 0; i < count; i++) {
        stages[i].image = load_file(stages[i].argv[0], &stages[i].image_size);
        if (!stages[i].image) {
            printf("%s: unknown command (try help)\n", stages[i].argv[0]);
            count = i;
            background = false;
            goto unload;
        }
    }

    if (background) {
        int free_slots = 0;
        for (int i = 0; i < MAX_JOBS; i++) {
            free_slots += jobs[i].pid == 0;
        }
        if (free_slots < count) {
            printf("%s: too many background jobs\n", stages[0].argv[0]);
            background = false;
            goto unload;
        }
    }

    // 每一段一个子进程。管道两头要知道对方的 PID，而后面的进程这时还没创建，
    // 所以子进程先等我们把相邻两段的 PID 发过去，再 exec
    for (int i = 0; i < count; i++) {
        int pid = fork();
        if (pid == 0) {
            struct ipc_msg m = {};
            if (count > 1 && ipc_recv(getppid(), &m) != 0) {
                exit(126);
            }
            exec_stage(&stages[i], (int)m.data[0], (int)m.data[1]);
        }
        if (pid < 0) {
            printf("%s: fork failed\n", stages[i].argv[0]);
            for (int j = 0; j < i; j++) {
                kill(stages[j].pid, SIGKILL);
                waitpid(stages[j].pid, NULL, 0);
            }
            background = false;
            goto unload;
        }
        stages[i].pid = pid;
    }
    for (int i = 0; count > 1 && i < count; i++) {
        struct ipc_msg m = {};
        m.data[0] = i > 0 ? (uint64_t)stages[i - 1].pid : 0;
        m.data[1] = i + 1 < count ? (uint64_t)stages[i + 1].pid : 0;
        ipc_send(stages[i].pid, &m);
    }

    if (background) {
        for (int i = 0, slot = 0; i < count; i++, slot++) {
            while (jobs[slot].pid != 0) {
                slot++;
            }
            jobs[slot].pid = stages[i].pid;
            strncpy(jobs[slot].name, stages[i].argv[0], sizeof(jobs[slot].name) - 1);
            jobs[slot].name[sizeof(jobs[slot].name) - 1] = '\0';
            printf("[%d] %s\n", stages[i].pid, stages[i].argv[0]);
        }
    } else {
        // 前台：等它们都结束。键盘输入这段时间归第一段；我们短暂地去读串口，只会读到 Ctrl-C
        set_foreground(stages[0].pid);
        bool interrupted = false;
        for (;;) {
            int running = 0;
            for (int i = 0; i < count; i++) {
                if (!stages[i].exited) {
                    stages[i].exited = waitpid(stages[i].pid, &stages[i].status, uart > 0 ? WNOHANG : 0)
                                       == stages[i].pid;
                    running += !stages[i].exited;
                }
            }
            if (running == 0) {
                break;
            }
            if (uart > 0 && !fetch_input(20)) {
                uart = 0;           // 驱动没了：退回到单纯地等
            }
            if (uart > 0 && !interrupted && take_ctrl_c()) {
                printf("^C\n");
                for (int i = 0; i < count; i++) {
                    if (!stages[i].exited) {
                        kill(stages[i].pid, SIGINT);
                    }
                }
                interrupted = true;
            }
        }
        set_foreground(0);
        for (int i = 0; i < count; i++) {
            report_exit(stages[i].argv[0], stages[i].status);
        }
    }

unload:
    for (int i = 0; i < count; i++) {
        if (stages[i].image) {
            munmap(stages[i].image, stages[i].image_size);
        }
    }
}

// 把管道的一段按空格切成参数，摘出重定向（< file、> file、>> file，文件名可以紧跟符号）。
// @return 写得对不对
static bool parse_stage(char *text, struct stage *st) {
    char *p = text;
    while (*p == ' ') {
        p++;
    }
    const char **target = NULL;     // 上一个词是重定向符号：这个词是它的文件名
    while (*p) {
        char *word = p;
        while (*p && *p != ' ') {
            p++;
        }
        while (*p == ' ') {
            *p++ = '\0';
        }

        if (target) {
            *target = word;
            target = NULL;
        } else if (word[0] == '<' || word[0] == '>') {
            bool output = word[0] == '>';
            if (output && word[1] == '>') {
                st->append = true;
                word++;
            } else if (output) {
                st->append = false;
            }
            target = output ? &st->out_file : &st->in_file;
            if (word[1] != '\0') {
                *target = word + 1;
                target = NULL;
            }
        } else if (st->argc < MAX_ARGS) {
            st->argv[st->argc++] = word;
        }
    }
    st->argv[st->argc] = NULL;
    return st->argc > 0 && target == NULL &&
           (!st->in_file || strlen(st->in_file) < FS_NAME_MAX) &&
           (!st->out_file || strlen(st->out_file) < FS_NAME_MAX);
}

static void run_command(char *line) {
    while (*line == ' ') {
        line++;
    }
    if (*line == '\0' || *line == '#') {
        return;
    }

    if (strcmp(line, "help") == 0) {
        printf("builtins: help, jobs, kill <pid>\n");
        printf("anything else runs a program from the file service with the rest of the\n");
        printf("line as its arguments, e.g.: ls, cat <file>, write <file>, ping <ip>, hello\n");
        printf("  cmd &                  run in the background; Ctrl-C stops the foreground program\n");
        printf("  cmd < file > file      read input from / write output to a file (>> appends)\n");
        printf("  cmd | cmd              feed one program's output to the next, e.g.: ls | grep sh | wc\n");
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

    // 按 | 切成几段。重定向只能在两头：第一段的输入、最后一段的输出
    static struct stage stages[MAX_STAGES];
    memset(stages, 0, sizeof(stages));
    int count = 0;
    bool ok = true;
    for (char *text = line; ok && text; ) {
        char *bar = strchr(text, '|');
        if (bar) {
            *bar++ = '\0';
        }
        ok = count < MAX_STAGES && parse_stage(text, &stages[count]);
        count++;
        text = bar;
    }
    for (int i = 0; ok && i < count; i++) {
        ok = (i == 0 || !stages[i].in_file) && (i == count - 1 || !stages[i].out_file);
    }
    if (!ok) {
        printf("sh: syntax error\n");
        return;
    }

    if (count == 1 && strcmp(stages[0].argv[0], "kill") == 0) {
        if (stages[0].argc == 2) {
            cmd_kill(stages[0].argv[1]);
        } else {
            printf("usage: kill <pid>\n");
        }
        return;
    }
    run_pipeline(stages, count, background);
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
    uart = name_wait("uart");
    struct ipc_msg attach = {};
    attach.label = UART_ATTACH;
    if (uart <= 0 || ipc_call(uart, &attach) != 0 || attach.data[0] != 0) {
        printf("sh: cannot use the uart driver\n");
        return 1;
    }
    run_rc();
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
        } else if ((unsigned char)c >= 0x20 && len < sizeof(line) - 1) {
            line[len++] = c;
            console_write(&c, 1);
        }
    }
}
