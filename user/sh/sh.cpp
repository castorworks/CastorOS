// sh - 一个很小的命令行
//
// 由 init 启动（非特权）。输入来自 uart 驱动。除了几个内置命令，一行的第一个词
// 被当作文件服务里的程序：读出它的 ELF 映像，fork 之后带着这一行的参数 exec。
// 行尾加 & 让程序在后台运行；前台程序运行期间按 Ctrl-C 终止它。
// cmd < in > out 2> err 把程序的标准输入/输出/错误输出换成文件，cmd1 | cmd2 把前一个的输出
// 接到后一个的输入；引号里的内容原样作为参数。
// 不是 ELF 映像的文件当作脚本，逐行执行；启动时先执行脚本 "rc"。
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
    const char *err_file;       // 2> file 或 2>> file
    bool err_append;
    void *image;
    size_t image_size;
    int pid;
    int status;
    bool exited;
};

// 在 fork 出来的子进程里：换成这一段的程序。标准输入/输出不是控制台时，
// 在参数最后附上说明（格式见 <stdio.h>），由新程序的启动代码去设置
static void exec_stage(struct stage *st, int in_pid, int out_pid) {
    static char spec[3 * FS_NAME_MAX + 12];
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
    char err[FS_NAME_MAX + 2] = "";
    if (st->err_file) {
        snprintf(err, sizeof(err), "%c%s", st->err_append ? 'a' : 'f', st->err_file);
    }
    if (in[0] || out[0] || err[0]) {
        snprintf(spec, sizeof(spec), "%c%s%c%s%c%s", STDIO_ARG_MARK, in, STDIO_ARG_MARK, out,
                 STDIO_ARG_MARK, err);
        st->argv[st->argc] = spec;
        st->argv[st->argc + 1] = NULL;
    }
    exec(st->image, st->image_size, st->argv);
    printf("%s: not an executable\n", st->argv[0]);
    exit(126);
}

// 运行一条管道（只有一段时就是一个程序）
// 前台程序被 Ctrl-C 打断过：正在执行的脚本据此停下来，不再执行后面的行
static bool interrupted_by_user = false;

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
                interrupted_by_user = true;
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

// ============================================================================
// 解析命令行
// ============================================================================

static bool is_script(const char *name);
static void run_script_command(char **argv, int argc, bool background);

#define MAX_TOKENS 64

// 一行切出来的一个单位：一个词，或者一个运算符
enum {
    TOK_WORD,
    TOK_PIPE,           // |
    TOK_BACKGROUND,     // &
    TOK_IN,             // <
    TOK_OUT,            // >
    TOK_OUT_APPEND,     // >>
    TOK_ERR,            // 2>
    TOK_ERR_APPEND,     // 2>>
};

struct token {
    int kind;
    const char *text;   // TOK_WORD：词的内容（引号已经去掉）
};

/**
 * 把一行切成词和运算符。词的内容复制到 text 里（每个词以 NUL 结尾）。
 *
 * 引号（"..." 或 '...'）里的内容原样成为词的一部分：空格不分词，| & < > 不是运算符。
 * 引号可以出现在词的中间（a"b c"d 是一个词 ab cd），两个引号紧挨着是一个空的词。
 * 没有转义字符，也没有变量。
 *
 * @param text 至少 strlen(line) + MAX_TOKENS 字节
 * @return 切出来的个数；引号没有配对或者太多时返回 -1
 */
static int tokenize(const char *line, struct token *tokens, char *text) {
    int count = 0;
    const char *src = line;

    for (;;) {
        while (*src == ' ' || *src == '\t') {
            src++;
        }
        if (*src == '\0') {
            return count;
        }
        if (count == MAX_TOKENS) {
            return -1;
        }
        struct token *tok = &tokens[count++];
        tok->text = NULL;

        // 运算符。"2>" 只有在一个词的开头才是运算符（a2>b 里的 2 属于前面的词）
        if (src[0] == '2' && src[1] == '>') {
            tok->kind = src[2] == '>' ? TOK_ERR_APPEND : TOK_ERR;
            src += src[2] == '>' ? 3 : 2;
            continue;
        }
        if (*src == '|' || *src == '&' || *src == '<' || *src == '>') {
            tok->kind = *src == '|' ? TOK_PIPE : *src == '&' ? TOK_BACKGROUND : *src == '<' ? TOK_IN
                      : src[1] == '>' ? TOK_OUT_APPEND : TOK_OUT;
            src += tok->kind == TOK_OUT_APPEND ? 2 : 1;
            continue;
        }

        // 一个词：读到没有被引号括住的空白或运算符为止
        tok->kind = TOK_WORD;
        tok->text = text;
        char quote = 0;
        for (; *src; src++) {
            char c = *src;
            if (quote) {
                if (c == quote) {
                    quote = 0;
                } else {
                    *text++ = c;
                }
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == ' ' || c == '\t' || c == '|' || c == '&' || c == '<' || c == '>') {
                break;
            } else {
                *text++ = c;
            }
        }
        if (quote) {
            return -1;
        }
        *text++ = '\0';
    }
}

static void run_command(char *line) {
    static struct token tokens[MAX_TOKENS];
    static struct stage stages[MAX_STAGES];
    static char text[256 + MAX_TOKENS];

    while (*line == ' ') {
        line++;
    }
    if (*line == '#') {
        return;
    }
    int ntok = strlen(line) <= 256 ? tokenize(line, tokens, text) : -1;
    if (ntok == 0) {
        return;
    }

    // 把词和运算符组装成管道的各段。重定向的文件名是运算符后面的那个词；
    // 输入重定向只能在第一段，输出重定向只能在最后一段，& 只能在行尾
    memset(stages, 0, sizeof(stages));
    int count = 1;
    bool background = false;
    bool ok = ntok > 0;        // -1：引号没有配对
    for (int i = 0; ok && i < ntok; i++) {
        struct stage *st = &stages[count - 1];
        const char *file = (i + 1 < ntok && tokens[i + 1].kind == TOK_WORD) ? tokens[i + 1].text : NULL;
        switch (tokens[i].kind) {
        case TOK_WORD:
            if (st->argc < MAX_ARGS) {
                st->argv[st->argc++] = (char *)tokens[i].text;
            }
            break;
        case TOK_PIPE:
            ok = st->argc > 0 && !st->out_file && count < MAX_STAGES;
            count++;
            break;
        case TOK_BACKGROUND:
            ok = i == ntok - 1;
            background = true;
            break;
        case TOK_IN:
            ok = file && count == 1;
            st->in_file = file;
            i++;
            break;
        case TOK_OUT:
        case TOK_OUT_APPEND:
            ok = file != NULL;
            st->out_file = file;
            st->append = tokens[i].kind == TOK_OUT_APPEND;
            i++;
            break;
        case TOK_ERR:
        case TOK_ERR_APPEND:
            ok = file != NULL;
            st->err_file = file;
            st->err_append = tokens[i].kind == TOK_ERR_APPEND;
            i++;
            break;
        }
    }
    for (int i = 0; ok && i < count; i++) {
        struct stage *st = &stages[i];
        ok = st->argc > 0 &&
             (!st->in_file || strlen(st->in_file) < FS_NAME_MAX) &&
             (!st->out_file || strlen(st->out_file) < FS_NAME_MAX) &&
             (!st->err_file || strlen(st->err_file) < FS_NAME_MAX);
        st->argv[st->argc] = NULL;
    }
    if (!ok) {
        printf("sh: syntax error\n");
        return;
    }

    // 内置命令
    if (count == 1) {
        struct stage *st = &stages[0];
        if (strcmp(st->argv[0], "help") == 0) {
            printf("builtins: help, jobs, kill <pid>\n");
            printf("anything else runs a program from the file service with the rest of the\n");
            printf("line as its arguments, e.g.: ls, cat <file>, write <file>, ping <ip>, hello\n");
            printf("  \"two words\"            quotes (\" or ') keep spaces and | & < > in an argument\n");
            printf("  cmd &                  run in the background; Ctrl-C stops the foreground program\n");
            printf("  cmd < file > file      read input from / write output to a file (>> appends)\n");
            printf("  cmd 2> file            write error messages to a file\n");
            printf("  cmd | cmd              feed one program's output to the next, e.g.: ls | grep sh | wc\n");
            printf("a text file is run as a script, one command per line; $1-$9 are its arguments\n");
            return;
        }
        if (strcmp(st->argv[0], "jobs") == 0) {
            cmd_jobs();
            return;
        }
        if (strcmp(st->argv[0], "kill") == 0) {
            if (st->argc == 2) {
                cmd_kill(st->argv[1]);
            } else {
                printf("usage: kill <pid>\n");
            }
            return;
        }
    }

    // 不是 ELF 映像的文件当作脚本：由命令行自己逐行执行，而不是交给内核去 exec
    for (int i = 0; i < count; i++) {
        struct stage *st = &stages[i];
        if (!is_script(st->argv[0])) {
            continue;
        }
        if (count > 1 || st->in_file || st->out_file || st->err_file) {
            printf("sh: %s is a script: it cannot be piped or redirected\n", st->argv[0]);
        } else {
            run_script_command(st->argv, st->argc, background);
        }
        return;
    }
    run_pipeline(stages, count, background);
}

// ============================================================================
// 脚本
// ============================================================================
//
// 脚本是一个文本文件，每行一条命令，和在提示符下敲的一样（# 开头的行是注释）。
// 命令行里 $0 是脚本名，$1 - $9 是调用它时给的参数，没有给的是空的；替换是纯文本的，
// 发生在分词之前，所以引号里的也会被替换。开机时执行的 rc 就是一个脚本。

#define SCRIPT_MAX_DEPTH    4       // 脚本里可以再调脚本，最多这么多层
#define SCRIPT_MAX_ARGS     10      // $0 - $9
#define SCRIPT_LINE_MAX     256

static const char ELF_MAGIC[4] = { 0x7F, 'E', 'L', 'F' };
static int script_depth = 0;

/** 文件存在而且不是 ELF 映像 */
static bool is_script(const char *name) {
    int fd = fs_open(name, 0);
    if (fd < 0) {
        return false;
    }
    char head[4];
    long n = fs_read(fd, 0, head, sizeof(head));
    fs_close(fd);
    return n >= 0 && !(n == (long)sizeof(head) && memcmp(head, ELF_MAGIC, sizeof(head)) == 0);
}

/** 把一行里的 $0 - $9 换成参数，写进 out。太长的行截断 */
static void expand_args(const char *line, size_t len, char **argv, int argc, char *out, size_t out_size) {
    size_t n = 0;
    for (size_t i = 0; i < len && n + 1 < out_size; i++) {
        if (line[i] == '$' && i + 1 < len && line[i + 1] >= '0' && line[i + 1] <= '9') {
            int index = line[++i] - '0';
            const char *value = index < argc ? argv[index] : "";
            while (*value && n + 1 < out_size) {
                out[n++] = *value++;
            }
        } else {
            out[n++] = line[i];
        }
    }
    out[n] = '\0';
}

/** 逐行执行一个脚本。argv[0] 是脚本的文件名 */
static void run_script(char **argv, int argc) {
    if (script_depth == SCRIPT_MAX_DEPTH) {
        printf("sh: %s: scripts nested too deeply\n", argv[0]);
        return;
    }
    size_t size = 0;
    char *script = (char *)load_file(argv[0], &size);
    if (!script) {
        return;         // 空文件：没有什么可执行的
    }

    script_depth++;
    char line[SCRIPT_LINE_MAX];     // 在栈上：脚本可以嵌套
    for (size_t start = 0; start < size && !interrupted_by_user; ) {
        size_t end = start;
        while (end < size && script[end] != '\n') {
            end++;
        }
        expand_args(script + start, end - start, argv, argc, line, sizeof(line));
        run_command(line);
        start = end + 1;
    }
    script_depth--;
    munmap(script, size);
}

/** 运行一个脚本命令。参数先复制出来：执行脚本里的命令会覆盖解析用的缓冲区 */
static void run_script_command(char **argv, int argc, bool background) {
    char storage[SCRIPT_LINE_MAX];
    char *args[SCRIPT_MAX_ARGS];
    int count = 0;
    size_t used = 0;
    for (int i = 0; i < argc && count < SCRIPT_MAX_ARGS; i++) {
        size_t len = strlen(argv[i]) + 1;
        if (used + len > sizeof(storage)) {
            break;
        }
        memcpy(storage + used, argv[i], len);
        args[count++] = storage + used;
        used += len;
    }

    if (!background) {
        run_script(args, count);
        return;
    }

    // 后台：一个 sh 的副本去执行它。副本不是终端的主人，它启动的程序读不到键盘
    int slot = -1;
    for (int i = 0; i < MAX_JOBS && slot < 0; i++) {
        if (jobs[i].pid == 0) {
            slot = i;
        }
    }
    if (slot < 0) {
        printf("%s: too many background jobs\n", args[0]);
        return;
    }
    int pid = fork();
    if (pid == 0) {
        uart = 0;
        pending_len = 0;
        memset(jobs, 0, sizeof(jobs));
        run_script(args, count);
        exit(0);
    }
    if (pid < 0) {
        printf("%s: fork failed\n", args[0]);
        return;
    }
    jobs[slot].pid = pid;
    strncpy(jobs[slot].name, args[0], sizeof(jobs[slot].name) - 1);
    jobs[slot].name[sizeof(jobs[slot].name) - 1] = '\0';
    printf("[%d] %s\n", pid, args[0]);
}

int main() {
    uart = name_wait("uart");
    struct ipc_msg attach = {};
    attach.label = UART_ATTACH;
    if (uart <= 0 || ipc_call(uart, &attach) != 0 || attach.data[0] != 0) {
        printf("sh: cannot use the uart driver\n");
        return 1;
    }
    // 启动脚本
    if (is_script("rc")) {
        char rc_name[] = "rc";
        char *rc_argv[] = { rc_name };
        run_script(rc_argv, 1);
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
            interrupted_by_user = false;
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
