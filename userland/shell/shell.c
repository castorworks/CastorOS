// shell.c - CastorOS 用户态 Shell
// 参考内核 shell 实现，使用系统调用接口

#include <syscall.h>
#include <string.h>
#include <stdio.h>
#include <types.h>
#include <time.h>

// ============================================================================
// 常量定义
// ============================================================================

#define SHELL_MAX_INPUT_LENGTH      256
#define SHELL_MAX_ARGS              16
#define SHELL_MAX_PATH_LENGTH       256
#define SHELL_MAX_HISTORY           50
#define SHELL_PROMPT                "root@CastorOS:~$ "
#define SHELL_VERSION               "0.1.2"
#define SHELL_CAT_ZERO_PREVIEW      4096
#define SHELL_CTRL_C                0x03
#define SHELL_CTRL_L                0x0C
#define SHELL_INPUT_INTERRUPTED    (-2)

// 文件打开标志（需要与内核一致）
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_TRUNC     0x0200
#define O_APPEND    0x0400

// 权限标志
#define FS_PERM_READ  0x01
#define FS_PERM_WRITE 0x02
#define FS_PERM_EXEC  0x04

// 信号定义（标准 POSIX 信号）
#define SIGTERM  15
#define SIGKILL  9
#define SIGINT   2
#define SIGHUP   1

// ============================================================================
// 数据结构
// ============================================================================

typedef struct {
    char input_buffer[SHELL_MAX_INPUT_LENGTH];
    size_t input_len;
    int argc;
    char *argv[SHELL_MAX_ARGS];
    char cwd[SHELL_MAX_PATH_LENGTH];
    int running;
    // 历史记录
    char history[SHELL_MAX_HISTORY][SHELL_MAX_INPUT_LENGTH];
    int history_count;      // 当前历史记录数量
    int history_index;      // 当前浏览的历史索引（-1 表示不在浏览历史）
    char temp_buffer[SHELL_MAX_INPUT_LENGTH];  // 临时缓冲区，用于保存当前输入
} shell_state_t;

typedef struct {
    const char *name;
    const char *description;
    const char *usage;
    int (*handler)(int argc, char **argv);
} shell_command_t;

// ============================================================================
// 全局变量
// ============================================================================

static shell_state_t shell_state;

// 辅助函数：清除当前行并重新显示
static void shell_redraw_line(const char *buffer, size_t len, size_t old_len) {
    // 如果新内容比旧内容短，需要清除多余的字符
    // 先用退格删除所有旧内容
    for (size_t j = 0; j < old_len; j++) {
        print("\b");
    }
    
    // 用空格覆盖旧内容
    for (size_t j = 0; j < old_len; j++) {
        print(" ");
    }
    
    // 再次退格到起始位置
    for (size_t j = 0; j < old_len; j++) {
        print("\b");
    }
    
    // 显示新内容
    if (len > 0) {
        write(STDOUT_FILENO, buffer, len);
    }
}

// 从 stdin 读取一行
static int shell_read_line(char *buffer, size_t size) {
    size_t i = 0;
    char c;
    
    // 重置历史索引
    shell_state.history_index = -1;
    
    while (i < size - 1) {
        int ret = read(STDIN_FILENO, &c, 1);
        if (ret <= 0) break;
        
        if ((unsigned char)c == SHELL_CTRL_C) {
            buffer[0] = '\0';
            i = 0;
            print("^C\n");
            return SHELL_INPUT_INTERRUPTED;
        } else if ((unsigned char)c == SHELL_CTRL_L) {
            print("\033[2J\033[H");
            print(SHELL_PROMPT);
            if (i > 0) {
                write(STDOUT_FILENO, buffer, i);
            }
            continue;
        } else if (c == '\n') {
            buffer[i] = '\0';
            print("\n");
            return (int)i;
        } else if (c == '\b' || c == 127) {  // 退格
            if (i > 0) {
                i--;
                print("\b \b");  // 回显退格
            }
        } else if ((unsigned char)c == 0x1B) {  // ESC 键，可能是方向键
            // 读取下一个字符
            char c2;
            ret = read(STDIN_FILENO, &c2, 1);
            if (ret <= 0 || c2 != '[') {
                continue;  // 不是方向键序列
            }
            
            // 读取方向键
            char c3;
            ret = read(STDIN_FILENO, &c3, 1);
            if (ret <= 0) continue;
            
            if (c3 == 'A') {  // 上键
                // 第一次按上键时，保存当前输入
                if (shell_state.history_index == -1) {
                    memcpy(shell_state.temp_buffer, buffer, i);
                    shell_state.temp_buffer[i] = '\0';
                }
                
                // 向前浏览历史
                if (shell_state.history_count > 0) {
                    size_t old_len = i;  // 保存旧长度
                    
                    if (shell_state.history_index == -1) {
                        shell_state.history_index = shell_state.history_count - 1;
                    } else if (shell_state.history_index > 0) {
                        shell_state.history_index--;
                    }
                    
                    // 复制历史记录到缓冲区
                    const char *hist = shell_state.history[shell_state.history_index];
                    i = 0;
                    while (hist[i] != '\0' && i < size - 1) {
                        buffer[i] = hist[i];
                        i++;
                    }
                    buffer[i] = '\0';
                    
                    // 重新显示行
                    shell_redraw_line(buffer, i, old_len);
                }
            } else if (c3 == 'B') {  // 下键
                if (shell_state.history_index != -1) {
                    size_t old_len = i;  // 保存旧长度
                    
                    shell_state.history_index++;
                    
                    if (shell_state.history_index >= shell_state.history_count) {
                        // 恢复临时缓冲区的内容
                        shell_state.history_index = -1;
                        i = 0;
                        while (shell_state.temp_buffer[i] != '\0' && i < size - 1) {
                            buffer[i] = shell_state.temp_buffer[i];
                            i++;
                        }
                        buffer[i] = '\0';
                    } else {
                        // 复制历史记录到缓冲区
                        const char *hist = shell_state.history[shell_state.history_index];
                        i = 0;
                        while (hist[i] != '\0' && i < size - 1) {
                            buffer[i] = hist[i];
                            i++;
                        }
                        buffer[i] = '\0';
                    }
                    
                    // 重新显示行
                    shell_redraw_line(buffer, i, old_len);
                }
            }
            continue;
        } else if (c >= 32 && c <= 126) {  // 可打印字符
            buffer[i++] = c;
            write(STDOUT_FILENO, &c, 1);  // 回显
            // 如果用户在浏览历史时输入新字符，退出历史模式
            shell_state.history_index = -1;
        }
    }
    
    buffer[i] = '\0';
    return (int)i;
}

// 命令解析
static int shell_parse_command(char *line, int *argc, char **argv) {
    *argc = 0;
    
    // 跳过前导空格
    while (*line && isspace(*line)) line++;
    
    if (*line == '\0') return 0;
    
    // 分割参数
    while (*line && *argc < SHELL_MAX_ARGS) {
        while (*line && isspace(*line)) line++;
        if (*line == '\0') break;
        
        argv[*argc] = line;
        (*argc)++;
        
        while (*line && !isspace(*line)) line++;
        
        if (*line) {
            *line = '\0';
            line++;
        }
    }
    
    return 0;
}

// ============================================================================
// 路径处理函数
// ============================================================================

/**
 * 规范化路径，处理 . 和 ..
 */
static int shell_normalize_path(const char *path, char *normalized, size_t size) {
    if (!path || !normalized || size == 0) {
        return -1;
    }
    
    const size_t MAX_COMPONENTS = 64;
    char components[MAX_COMPONENTS][128];
    size_t component_count = 0;
    
    const char *p = path;
    char current_component[128];
    size_t comp_len = 0;
    int is_absolute = (path[0] == '/');
    
    // 解析路径组件
    while (*p != '\0') {
        // 跳过连续的 '/'
        while (*p == '/') {
            p++;
        }
        
        if (*p == '\0') {
            break;
        }
        
        // 提取一个组件
        comp_len = 0;
        while (*p != '\0' && *p != '/' && comp_len < 127) {
            current_component[comp_len++] = *p++;
        }
        current_component[comp_len] = '\0';
        
        // 处理组件
        if (comp_len == 0) {
            continue;
        } else if (strcmp(current_component, ".") == 0) {
            // 当前目录，忽略
            continue;
        } else if (strcmp(current_component, "..") == 0) {
            // 父目录
            if (component_count > 0) {
                component_count--;
            }
        } else {
            // 普通组件
            if (component_count >= MAX_COMPONENTS) {
                return -1;
            }
            strncpy(components[component_count], current_component, 127);
            components[component_count][127] = '\0';
            component_count++;
        }
    }
    
    // 构建规范化路径
    size_t pos = 0;
    
    if (is_absolute) {
        if (pos < size - 1) {
            normalized[pos++] = '/';
        }
    }
    
    for (size_t i = 0; i < component_count; i++) {
        size_t comp_len = strlen(components[i]);
        
        if (pos > 0 && normalized[pos - 1] != '/') {
            if (pos < size - 1) {
                normalized[pos++] = '/';
            }
        }
        
        for (size_t j = 0; j < comp_len && pos < size - 1; j++) {
            normalized[pos++] = components[i][j];
        }
    }
    
    if (pos == 0) {
        if (is_absolute) {
            normalized[pos++] = '/';
        } else {
            normalized[pos++] = '.';
        }
    }
    
    normalized[pos] = '\0';
    return 0;
}

/**
 * 将相对路径转换为绝对路径
 */
static int shell_resolve_path(const char *path, char *abs_path, size_t size) {
    if (!path || !abs_path || size == 0) {
        return -1;
    }
    
    char normalized[SHELL_MAX_PATH_LENGTH];
    char temp_path[SHELL_MAX_PATH_LENGTH];
    
    // 如果是绝对路径，直接规范化
    if (path[0] == '/') {
        if (shell_normalize_path(path, normalized, sizeof(normalized)) != 0) {
            return -1;
        }
        if (strlen(normalized) >= size) {
            return -1;
        }
        strncpy(abs_path, normalized, size - 1);
        abs_path[size - 1] = '\0';
        return 0;
    }
    
    // 相对路径：需要结合当前工作目录
    char cwd[SHELL_MAX_PATH_LENGTH];
    if (getcwd(cwd, sizeof(cwd)) == 0) {
        strcpy(cwd, "/");  // 如果获取失败，使用根目录
    }
    
    size_t cwd_len = strlen(cwd);
    size_t path_len = strlen(path);
    
    if (cwd_len + 1 + path_len >= sizeof(temp_path)) {
        return -1;
    }
    
    // 构建临时绝对路径
    strncpy(temp_path, cwd, sizeof(temp_path) - 1);
    temp_path[sizeof(temp_path) - 1] = '\0';
    
    size_t current_len = strlen(temp_path);
    
    if (cwd_len > 1 && current_len < sizeof(temp_path) - 1) {
        temp_path[current_len] = '/';
        current_len++;
        temp_path[current_len] = '\0';
    }
    
    size_t i = 0;
    while (path[i] != '\0' && current_len < sizeof(temp_path) - 1) {
        temp_path[current_len] = path[i];
        current_len++;
        i++;
    }
    temp_path[current_len] = '\0';
    
    // 规范化路径
    if (shell_normalize_path(temp_path, normalized, sizeof(normalized)) != 0) {
        return -1;
    }
    
    if (strlen(normalized) >= size) {
        return -1;
    }
    strncpy(abs_path, normalized, size - 1);
    abs_path[size - 1] = '\0';
    
    return 0;
}

// ============================================================================
// 系统调用封装
// ============================================================================

/**
 * unlink 系统调用封装
 */
static int shell_unlink(const char *path) {
    return (int)syscall1(SYS_UNLINK, (uint32_t)path);
}

/**
 * kill 系统调用封装
 */
static int shell_kill(int pid, int signal) {
    return (int)syscall2(SYS_KILL, (uint32_t)pid, (uint32_t)signal);
}

// ============================================================================
// 工具函数
// ============================================================================

/**
 * 输出到屏幕和串口（用于重要信息）
 * 这个函数会将输出同时发送到 STDOUT（VGA）和 /dev/serial（串口）
 */
static void print_tee(const char *msg) {
    // 输出到 STDOUT（屏幕）
    print(msg);
    
    // 同时输出到串口
    int serial_fd = open("/dev/serial", O_WRONLY, 0);
    if (serial_fd >= 0) {
        size_t len = strlen(msg);
        write(serial_fd, msg, (uint32_t)len);
        close(serial_fd);
    }
}

/**
 * 格式化时间（毫秒转为天/时/分/秒）
 */
static void format_uptime(uint32_t ms, char *buffer, size_t size) {
    uint32_t seconds = ms / 1000;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;
    uint32_t days = hours / 24;
    
    seconds %= 60;
    minutes %= 60;
    hours %= 24;
    
    // 使用标准库的 snprintf
    size_t pos = 0;
    if (days > 0) {
        pos += (size_t)snprintf(buffer + pos, size - pos, "%u days, ", days);
    }
    if (hours > 0 || days > 0) {
        pos += (size_t)snprintf(buffer + pos, size - pos, "%u hours, ", hours);
    }
    if (minutes > 0 || hours > 0 || days > 0) {
        pos += (size_t)snprintf(buffer + pos, size - pos, "%u minutes, ", minutes);
    }
    snprintf(buffer + pos, size - pos, "%u seconds", seconds);
}

/**
 * 格式化内存大小（字节转为 KB/MB）
 * 注意：目前未使用，保留用于将来系统调用支持时使用
 */
static void __attribute__((unused)) format_memory_size(uint32_t bytes, char *buffer, size_t size) {
    if (bytes >= 1024 * 1024) {
        snprintf(buffer, size, "%u MB", bytes / (1024 * 1024));
    } else if (bytes >= 1024) {
        snprintf(buffer, size, "%u KB", bytes / 1024);
    } else {
        snprintf(buffer, size, "%u bytes", bytes);
    }
}

// ============================================================================
// 历史记录函数
// ============================================================================

/**
 * 添加命令到历史记录
 */
static void shell_add_history(const char *line) {
    // 忽略空行
    if (!line || line[0] == '\0') {
        return;
    }
    
    // 忽略与上一条相同的命令
    if (shell_state.history_count > 0) {
        int last_idx = shell_state.history_count - 1;
        if (strcmp(shell_state.history[last_idx], line) == 0) {
            return;
        }
    }
    
    // 如果历史记录已满，移除最旧的记录
    if (shell_state.history_count >= SHELL_MAX_HISTORY) {
        // 将所有历史记录向前移动一位
        for (int i = 0; i < SHELL_MAX_HISTORY - 1; i++) {
            strcpy(shell_state.history[i], shell_state.history[i + 1]);
        }
        shell_state.history_count = SHELL_MAX_HISTORY - 1;
    }
    
    // 添加新的历史记录
    strncpy(shell_state.history[shell_state.history_count], line, 
            SHELL_MAX_INPUT_LENGTH - 1);
    shell_state.history[shell_state.history_count][SHELL_MAX_INPUT_LENGTH - 1] = '\0';
    shell_state.history_count++;
}

// ============================================================================
// 内建命令声明
// ============================================================================

static int cmd_help(int argc, char **argv);
static int cmd_echo(int argc, char **argv);
static int cmd_version(int argc, char **argv);
static int cmd_exit(int argc, char **argv);
static int cmd_pwd(int argc, char **argv);
static int cmd_cd(int argc, char **argv);
static int cmd_clear(int argc, char **argv);
static int cmd_history(int argc, char **argv);

// 系统信息命令
static int cmd_uptime(int argc, char **argv);
static int cmd_free(int argc, char **argv);
static int cmd_ps(int argc, char **argv);
static int cmd_reboot(int argc, char **argv);
static int cmd_poweroff(int argc, char **argv);
static int cmd_exec(int argc, char **argv);
static int cmd_kill(int argc, char **argv);
static int cmd_wait(int argc, char **argv);

// 文件操作命令
static int cmd_ls(int argc, char **argv);
static int cmd_cat(int argc, char **argv);
static int cmd_touch(int argc, char **argv);
static int cmd_write(int argc, char **argv);
static int cmd_rm(int argc, char **argv);
static int cmd_mkdir(int argc, char **argv);
static int cmd_rmdir(int argc, char **argv);

static uint32_t shell_parse_meminfo_value(const char *line);

static uint32_t shell_parse_meminfo_value(const char *line) {
    if (!line) return 0;
    
    while (*line && !isdigit((unsigned char)*line)) {
        line++;
    }
    
    uint32_t value = 0;
    while (*line && isdigit((unsigned char)*line)) {
        value = value * 10 + (uint32_t)(*line - '0');
        line++;
    }
    return value;
}

// ============================================================================
// 命令表
// ============================================================================

static const shell_command_t commands[] = {
    // 基础命令
    {"help",     "Show available commands",        "help [command]",    cmd_help},
    {"echo",     "Print text to screen",           "echo [text...]",    cmd_echo},
    {"version",  "Show shell version",             "version",           cmd_version},
    {"clear",    "Clear screen",                   "clear",             cmd_clear},
    {"exit",     "Exit shell",                     "exit",              cmd_exit},
    {"history",  "Show command history",           "history",           cmd_history},
    
    // 运行时间
    {"uptime",   "Show system uptime",             "uptime",            cmd_uptime},
    
    // 内存管理命令
    {"free",     "Display memory usage",           "free",              cmd_free},
    
    // 进程管理命令
    {"ps",       "List running processes",         "ps",                cmd_ps},
    {"exec",     "Execute a user program",         "exec <path> [&]",   cmd_exec},
    {"kill",     "Send signal to process",         "kill [-signal] <pid>", cmd_kill},
    {"wait",     "Wait for child process",         "wait <pid>",        cmd_wait},
    
    // 系统控制命令
    {"reboot",   "Reboot the system",              "reboot",            cmd_reboot},
    {"poweroff", "Power off the system",           "poweroff",          cmd_poweroff},
    
    // 目录操作命令
    {"pwd",      "Print working directory",        "pwd",               cmd_pwd},
    {"cd",       "Change directory",               "cd [path]",         cmd_cd},
    
    // 文件操作命令
    {"ls",       "List directory contents",         "ls [path]",         cmd_ls},
    {"cat",      "Display file contents",           "cat <file>",        cmd_cat},
    {"touch",    "Create an empty file",           "touch <file>",       cmd_touch},
    {"write",    "Write text to file",             "write <file> <text...>", cmd_write},
    {"rm",       "Remove a file",                  "rm <file>",         cmd_rm},
    {"mkdir",    "Create a directory",             "mkdir <dir>",       cmd_mkdir},
    {"rmdir",    "Remove a directory",             "rmdir <dir>",        cmd_rmdir},
    
    {0, 0, 0, 0}
};

// ============================================================================
// 命令实现
// ============================================================================

static int cmd_help(int argc, char **argv) {
    if (argc == 1) {
        printf("Available commands:\n");
        printf("================================================================================\n");
        
        for (int i = 0; commands[i].name; i++) {
            printf("  %-12s - %s\n", commands[i].name, commands[i].description);
        }
        
        printf("\nType 'help <command>' for more information.\n");
    } else {
        // 查找特定命令
        int found = 0;
        for (int i = 0; commands[i].name; i++) {
            if (strcmp(commands[i].name, argv[1]) == 0) {
                printf("Command: %s\n", commands[i].name);
                printf("Description: %s\n", commands[i].description);
                printf("Usage: %s\n", commands[i].usage);
                found = 1;
                break;
            }
        }
        if (!found) {
            printf("Error: Unknown command '%s'\n", argv[1]);
            return -1;
        }
    }
    return 0;
}

static int cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        print(argv[i]);
        if (i < argc - 1) print(" ");
    }
    print("\n");
    return 0;
}

static int cmd_version(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("CastorOS User Shell Version %s\n", SHELL_VERSION);
    printf("A simple POSIX-like shell for CastorOS\n");
    return 0;
}

static int cmd_clear(int argc, char **argv) {
    (void)argc;
    (void)argv;
    // 使用 ANSI 转义序列清屏（如果终端支持）
    print("\033[2J\033[H");
    return 0;
}

static int cmd_history(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    if (shell_state.history_count == 0) {
        printf("No command history.\n");
        return 0;
    }
    
    printf("Command History:\n");
    printf("================================================================================\n");
    for (int i = 0; i < shell_state.history_count; i++) {
        printf("%4d  %s\n", i + 1, shell_state.history[i]);
    }
    printf("================================================================================\n");
    printf("Total: %d command(s)\n", shell_state.history_count);
    printf("Tip: Use UP/DOWN arrow keys to browse history\n");
    return 0;
}

static int cmd_pwd(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    char buffer[SHELL_MAX_PATH_LENGTH];
    // 使用 getcwd 系统调用
    if (getcwd(buffer, sizeof(buffer)) != 0) {
        printf("%s\n", buffer);
        return 0;
    } else {
        printf("Error: Failed to get current directory\n");
        return -1;
    }
}

static int cmd_cd(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "/";
    
    // 使用 chdir 系统调用
    if (chdir(path) != 0) {
        printf("Error: Failed to change directory to '%s'\n", path);
        return -1;
    }
    return 0;
}

static int cmd_exit(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("Exiting shell...\n");
    shell_state.running = 0;
    return 0;
}

// ============================================================================
// 系统信息命令实现
// ============================================================================

/**
 * uptime 命令 - 显示系统运行时间
 */
static int cmd_uptime(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    // 使用 time() 系统调用获取系统运行时间（秒）
    time_t uptime_sec = time(NULL);
    
    if (uptime_sec == (time_t)-1) {
        printf("Error: Failed to get system uptime\n");
        return -1;
    }
    
    // 转换为毫秒并格式化
    char uptime_str[128];
    format_uptime((uint32_t)(uptime_sec * 1000), uptime_str, sizeof(uptime_str));
    
    printf("System uptime: %s\n", uptime_str);
    return 0;
}

/**
 * free 命令 - 显示内存使用情况（紧凑格式）
 * 输出同时发送到屏幕和串口
 */
static int cmd_free(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    int fd = open("/proc/meminfo", O_RDONLY, 0);
    if (fd < 0) {
        print_tee("Error: Failed to open /proc/meminfo\n");
        return -1;
    }
    
    char buffer[1024];
    int bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    
    if (bytes_read <= 0) {
        print_tee("Error: Failed to read /proc/meminfo\n");
        return -1;
    }
    buffer[bytes_read] = '\0';
    
    // 解析所有字段
    uint32_t mem_total = 0, mem_free = 0, mem_used = 0, mem_reserved = 0;
    uint32_t mem_kernel = 0, mem_bitmap = 0;
    uint32_t page_size = 0, page_total = 0, page_free = 0, page_used = 0;
    uint32_t heap_total = 0, heap_used = 0, heap_free = 0;
    uint32_t heap_blocks = 0, heap_used_blocks = 0, heap_free_blocks = 0;
    
    char *line = buffer;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next) {
            *next = '\0';
            next++;
        }
        
        if (strncmp(line, "MemTotal:", 9) == 0) {
            mem_total = shell_parse_meminfo_value(line);
        } else if (strncmp(line, "MemFree:", 8) == 0) {
            mem_free = shell_parse_meminfo_value(line);
        } else if (strncmp(line, "MemUsed:", 8) == 0) {
            mem_used = shell_parse_meminfo_value(line);
        } else if (strncmp(line, "MemReserved:", 12) == 0) {
            mem_reserved = shell_parse_meminfo_value(line);
        } else if (strncmp(line, "MemKernel:", 10) == 0) {
            mem_kernel = shell_parse_meminfo_value(line);
        } else if (strncmp(line, "MemBitmap:", 10) == 0) {
            mem_bitmap = shell_parse_meminfo_value(line);
        } else if (strncmp(line, "PageSize:", 9) == 0) {
            page_size = shell_parse_meminfo_value(line);
        } else if (strncmp(line, "PageTotal:", 10) == 0) {
            page_total = shell_parse_meminfo_value(line);
        } else if (strncmp(line, "PageFree:", 9) == 0) {
            page_free = shell_parse_meminfo_value(line);
        } else if (strncmp(line, "PageUsed:", 9) == 0) {
            page_used = shell_parse_meminfo_value(line);
        } else if (strncmp(line, "HeapTotal:", 10) == 0) {
            heap_total = shell_parse_meminfo_value(line);
        } else if (strncmp(line, "HeapUsed:", 9) == 0) {
            heap_used = shell_parse_meminfo_value(line);
        } else if (strncmp(line, "HeapFree:", 9) == 0) {
            heap_free = shell_parse_meminfo_value(line);
        } else if (strncmp(line, "HeapBlocks:", 11) == 0) {
            heap_blocks = shell_parse_meminfo_value(line);
        } else if (strncmp(line, "HeapUsedBlocks:", 15) == 0) {
            heap_used_blocks = shell_parse_meminfo_value(line);
        } else if (strncmp(line, "HeapFreeBlocks:", 15) == 0) {
            heap_free_blocks = shell_parse_meminfo_value(line);
        }
        
        if (!next) {
            break;
        }
        line = next;
    }
    
    if (mem_total == 0) {
        print_tee("Error: Invalid data from /proc/meminfo\n");
        return -1;
    }
    
    // 计算使用率
    uint32_t mem_usage_percent = (mem_used * 100) / mem_total;
    uint32_t heap_usage_percent = heap_total > 0 ? (heap_used * 100) / heap_total : 0;
    
    // 计算碎片率（如果有多个空闲块，说明有碎片）
    uint32_t frag_percent = 0;
    if (heap_blocks > 0 && heap_free_blocks > 1) {
        // 简单估算：空闲块越多，碎片越严重
        frag_percent = (heap_free_blocks * 100) / heap_blocks;
        if (frag_percent > 50) frag_percent = 50;  // 最多显示 50%
    }
    
    // 紧凑格式输出（严格控制在 10 行以内）
    // 使用 print_tee 同时输出到屏幕和串口
    char line_buf[256];
    
    print_tee("Memory Usage\n");
    print_tee("================================================================================\n");
    print_tee("               Total        Used        Free    Reserved  Usage\n");
    
    snprintf(line_buf, sizeof(line_buf), 
             "Physical  %7u KB  %7u KB  %7u KB   %6u KB   %3u%%\n", 
             mem_total, mem_used, mem_free, mem_reserved, mem_usage_percent);
    print_tee(line_buf);
    
    if (heap_total > 0) {
        snprintf(line_buf, sizeof(line_buf),
                 "Heap      %7u KB  %7u KB  %7u KB        - KB   %3u%%\n",
                 heap_total, heap_used, heap_free, heap_usage_percent);
        print_tee(line_buf);
    }
    
    print_tee("--------------------------------------------------------------------------------\n");
    
    snprintf(line_buf, sizeof(line_buf),
             "Pages: %u total, %u used, %u free (%u bytes/page)\n",
             page_total, page_used, page_free, page_size);
    print_tee(line_buf);
    
    snprintf(line_buf, sizeof(line_buf),
             "Kernel: %u KB  |  Bitmap: %u KB\n", mem_kernel, mem_bitmap);
    print_tee(line_buf);
    
    if (heap_blocks > 0) {
        snprintf(line_buf, sizeof(line_buf),
                 "Heap: %u blocks (%u used, %u free)  |  Fragmentation: %u%%\n",
                 heap_blocks, heap_used_blocks, heap_free_blocks, frag_percent);
        print_tee(line_buf);
    }
    
    print_tee("================================================================================\n");
    return 0;
}

/**
 * 解析 /proc/[pid]/status 文件内容
 */
static int parse_proc_status(const char *status_content, uint32_t *pid, char *name, 
                              char *state, uint32_t *priority, uint64_t *runtime_ms) {
    if (!status_content || !pid || !name || !state || !priority || !runtime_ms) {
        return -1;
    }
    
    // 初始化
    *pid = 0;
    name[0] = '\0';
    state[0] = '\0';
    *priority = 0;
    *runtime_ms = 0;
    
    // 简单的解析实现（逐行解析）
    const char *p = status_content;
    while (*p != '\0') {
        // 解析 Name:
        if (strncmp(p, "Name:\t", 6) == 0) {
            p += 6;
            int i = 0;
            while (*p != '\n' && *p != '\0' && i < 31) {
                name[i++] = *p++;
            }
            name[i] = '\0';
        }
        // 解析 Pid:
        else if (strncmp(p, "Pid:\t", 5) == 0) {
            p += 5;
            *pid = 0;
            while (*p >= '0' && *p <= '9') {
                *pid = *pid * 10 + (*p - '0');
                p++;
            }
        }
        // 解析 State:
        else if (strncmp(p, "State:\t", 7) == 0) {
            p += 7;
            if (*p != '\n' && *p != '\0') {
                state[0] = *p;
                state[1] = '\0';
            }
        }
        // 解析 Priority:
        else if (strncmp(p, "Priority:\t", 10) == 0) {
            p += 10;
            *priority = 0;
            while (*p >= '0' && *p <= '9') {
                *priority = *priority * 10 + (*p - '0');
                p++;
            }
        }
        // 解析 Runtime:
        else if (strncmp(p, "Runtime:\t", 9) == 0) {
            p += 9;
            *runtime_ms = 0;
            while (*p >= '0' && *p <= '9') {
                *runtime_ms = *runtime_ms * 10 + (*p - '0');
                p++;
            }
            // 跳过 " ms"
            while (*p == ' ' || *p == 'm' || *p == 's') p++;
        }
        
        // 移动到下一行
        while (*p != '\n' && *p != '\0') p++;
        if (*p == '\n') p++;
    }
    
    return 0;
}

/**
 * ps 命令 - 显示进程列表（通过 /proc 文件系统）
 * 输出同时发送到屏幕和串口
 */
static int cmd_ps(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    // 打开 /proc 目录
    int proc_fd = open("/proc", O_RDONLY, 0);
    if (proc_fd < 0) {
        print_tee("Error: Cannot open /proc directory\n");
        return -1;
    }
    
    // 用于格式化输出的缓冲区
    char line_buf[256];
    
    // 显示表头
    print_tee("Process List\n");
    print_tee("================================================================================\n");
    print_tee("PID   Name              State       Priority  Runtime (ms)\n");
    print_tee("--------------------------------------------------------------------------------\n");
    
    // 读取 /proc 目录中的所有 PID 目录
    struct dirent entry;
    uint32_t index = 0;
    int process_count = 0;
    
    while (getdents(proc_fd, index, &entry) == 0) {
        // 跳过 . 和 ..
        if (strcmp(entry.d_name, ".") == 0 || strcmp(entry.d_name, "..") == 0) {
            index++;
            continue;
        }
        
        // 检查是否是数字目录（PID）
        uint32_t pid = 0;
        const char *p = entry.d_name;
        while (*p >= '0' && *p <= '9') {
            pid = pid * 10 + (*p - '0');
            p++;
        }
        
        // 如果是有效的 PID 目录
        if (*p == '\0' && pid > 0) {
            // 构建 status 文件路径
            char status_path[64];
            // 使用 snprintf 需要包含 stdio.h，这里使用简单的方式
            size_t len = 0;
            status_path[len++] = '/';
            status_path[len++] = 'p';
            status_path[len++] = 'r';
            status_path[len++] = 'o';
            status_path[len++] = 'c';
            status_path[len++] = '/';
            
            const char *pid_str = entry.d_name;
            while (*pid_str != '\0' && len < 63) {
                status_path[len++] = *pid_str++;
            }
            status_path[len++] = '/';
            status_path[len++] = 's';
            status_path[len++] = 't';
            status_path[len++] = 'a';
            status_path[len++] = 't';
            status_path[len++] = 'u';
            status_path[len++] = 's';
            status_path[len] = '\0';
            
            // 打开并读取 status 文件
            int status_fd = open(status_path, O_RDONLY, 0);
            if (status_fd >= 0) {
                char status_buf[512];
                int bytes_read = read(status_fd, status_buf, sizeof(status_buf) - 1);
                close(status_fd);
                
                if (bytes_read > 0) {
                    status_buf[bytes_read] = '\0';
                    
                    // 解析 status 文件内容
                    uint32_t proc_pid;
                    char proc_name[32];
                    char proc_state[8];
                    uint32_t proc_priority;
                    uint64_t proc_runtime;
                    
                    if (parse_proc_status(status_buf, &proc_pid, proc_name, 
                                          proc_state, &proc_priority, &proc_runtime) == 0) {
                        // 转换状态字符串
                        const char *state_str;
                        if (strcmp(proc_state, "R") == 0) {
                            state_str = "RUNNING";
                        } else if (strcmp(proc_state, "S") == 0) {
                            state_str = "BLOCKED";
                        } else if (strcmp(proc_state, "Z") == 0) {
                            state_str = "TERMINATED";
                        } else {
                            state_str = "UNKNOWN";
                        }
                        
                        snprintf(line_buf, sizeof(line_buf),
                                 "%-5u %-17s %-11s %-9u %llu\n",
                                 proc_pid,
                                 proc_name,
                                 state_str,
                                 proc_priority,
                                 (unsigned long long)proc_runtime);
                        print_tee(line_buf);
                        process_count++;
                    }
                }
            }
        }
        
        index++;
    }
    
    close(proc_fd);
    
    if (process_count == 0) {
        print_tee("(No processes found)\n");
    }
    
    return 0;
}

/**
 * reboot 命令 - 重启系统
 */
static int cmd_reboot(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    printf("Rebooting system...\n");
    
    int ret = reboot();
    if (ret < 0) {
        printf("Error: reboot system call failed (code=%d)\n", ret);
    }
    return ret;
}

/**
 * poweroff 命令 - 关闭系统
 */
static int cmd_poweroff(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    printf("Powering off system...\n");
    
    int ret = poweroff();
    if (ret < 0) {
        printf("Error: poweroff system call failed (code=%d)\n", ret);
    }
    return ret;
}

/**
 * exec 命令 - 执行 ELF 程序
 * 用法: exec <path> [&]
 * 如果最后一个参数是 &，则在后台运行（不等待进程结束）
 */
static int cmd_exec(int argc, char **argv) {
    if (argc < 2) {
        printf("Error: Usage: exec <path> [&]\n");
        printf("  Add '&' to run in background\n");
        return -1;
    }
    
    // 检查是否要在后台运行
    int background = 0;
    int path_arg_index = 1;
    
    if (argc >= 3 && strcmp(argv[argc - 1], "&") == 0) {
        background = 1;
    }
    
    char abs_path[SHELL_MAX_PATH_LENGTH];
    if (shell_resolve_path(argv[path_arg_index], abs_path, sizeof(abs_path)) != 0) {
        printf("Error: Invalid path\n");
        return -1;
    }
    
    // 简单检查文件是否存在
    int fd = open(abs_path, O_RDONLY, 0);
    if (fd < 0) {
        printf("Error: Cannot access '%s'\n", abs_path);
        return -1;
    }
    close(fd);
    
    int pid = fork();
    if (pid < 0) {
        printf("Error: fork failed\n");
        return -1;
    }
    
    if (pid == 0) {
        // 子进程：执行程序
        int ret = exec(abs_path);
        printf("Error: exec failed for '%s' (code=%d)\n", abs_path, ret);
        exit(-1);
    }
    
    // 父进程
    if (background) {
        // 后台运行：不等待子进程
        printf("Started background process PID %d: %s\n", pid, abs_path);
        printf("Use 'ps' to check status, 'kill %d' to terminate\n", pid);
        return 0;
    } else {
        // 前台运行：等待子进程完成
        printf("Started process PID %d: %s\n", pid, abs_path);
        
        // 使用 waitpid 等待子进程退出
        int status = 0;
        int waited_pid = waitpid(pid, &status, 0);
        
        if (waited_pid < 0) {
            printf("Error: waitpid failed\n");
            return -1;
        }
        
        // 解析退出状态
        if (WIFEXITED(status)) {
            int exit_code = WEXITSTATUS(status);
            printf("Process %d exited with code %d\n", pid, exit_code);
        } else if (WIFSIGNALED(status)) {
            int signal = WTERMSIG(status);
            printf("Process %d terminated by signal %d\n", pid, signal);
        } else {
            printf("Process %d completed with status %d\n", pid, status);
        }
        
        return 0;
    }
}

/**
 * kill 命令 - 向进程发送信号
 * 用法: kill [-signal] <pid>
 * 示例: kill 1234        (发送 SIGTERM)
 *       kill -9 1234     (发送 SIGKILL)
 *       kill -SIGTERM 1234 (发送 SIGTERM)
 */
static int cmd_kill(int argc, char **argv) {
    if (argc < 2) {
        printf("Error: Usage: kill [-signal] <pid>\n");
        printf("  Examples:\n");
        printf("    kill 1234          (send SIGTERM to PID 1234)\n");
        printf("    kill -9 1234       (send SIGKILL to PID 1234)\n");
        printf("    kill -SIGTERM 1234  (send SIGTERM to PID 1234)\n");
        return -1;
    }
    
    int signal = SIGTERM;  // 默认信号
    int pid_arg_index = 1;
    
    // 检查是否有信号参数
    if (argc >= 3 && argv[1][0] == '-') {
        const char *signal_str = argv[1] + 1;  // 跳过 '-'
        pid_arg_index = 2;
        
        // 尝试解析信号号或信号名
        if (strcmp(signal_str, "SIGTERM") == 0 || strcmp(signal_str, "TERM") == 0) {
            signal = SIGTERM;
        } else if (strcmp(signal_str, "SIGKILL") == 0 || strcmp(signal_str, "KILL") == 0) {
            signal = SIGKILL;
        } else if (strcmp(signal_str, "SIGINT") == 0 || strcmp(signal_str, "INT") == 0) {
            signal = SIGINT;
        } else if (strcmp(signal_str, "SIGHUP") == 0 || strcmp(signal_str, "HUP") == 0) {
            signal = SIGHUP;
        } else {
            // 尝试解析为数字
            int parsed_signal = 0;
            const char *p = signal_str;
            while (*p >= '0' && *p <= '9') {
                parsed_signal = parsed_signal * 10 + (*p - '0');
                p++;
            }
            if (*p == '\0' && parsed_signal > 0) {
                signal = parsed_signal;
            } else {
                printf("Error: Invalid signal '%s'\n", signal_str);
                printf("  Valid signals: SIGTERM (15), SIGKILL (9), SIGINT (2), SIGHUP (1)\n");
                return -1;
            }
        }
    }
    
    // 解析 PID
    if (pid_arg_index >= argc) {
        printf("Error: PID not specified\n");
        return -1;
    }
    
    int pid = 0;
    const char *pid_str = argv[pid_arg_index];
    const char *p = pid_str;
    while (*p >= '0' && *p <= '9') {
        pid = pid * 10 + (*p - '0');
        p++;
    }
    
    if (*p != '\0' || pid <= 0) {
        printf("Error: Invalid PID '%s'\n", pid_str);
        return -1;
    }
    
    // 发送信号
    int ret = shell_kill(pid, signal);
    if (ret != 0) {
        printf("Error: Failed to send signal %d to process %d (code=%d)\n", signal, pid, ret);
        return -1;
    }
    
    // 显示信号名称
    const char *signal_name = "UNKNOWN";
    if (signal == SIGTERM) signal_name = "SIGTERM";
    else if (signal == SIGKILL) signal_name = "SIGKILL";
    else if (signal == SIGINT) signal_name = "SIGINT";
    else if (signal == SIGHUP) signal_name = "SIGHUP";
    
    printf("Sent signal %s (%d) to process %d\n", signal_name, signal, pid);
    return 0;
}

/**
 * wait 命令 - 等待子进程退出并回收资源
 * 
 * 用法：
 *   wait <pid>     等待指定 PID 的子进程
 * 
 * 示例：
 *   wait 2         等待 PID 2 的子进程退出
 */
static int cmd_wait(int argc, char **argv) {
    if (argc < 2) {
        printf("Error: Usage: wait <pid>\n");
        printf("  Example: wait 2\n");
        return -1;
    }
    
    // 解析 PID
    int pid = 0;
    const char *pid_str = argv[1];
    const char *p = pid_str;
    while (*p >= '0' && *p <= '9') {
        pid = pid * 10 + (*p - '0');
        p++;
    }
    
    if (*p != '\0' || pid <= 0) {
        printf("Error: Invalid PID '%s'\n", pid_str);
        return -1;
    }
    
    printf("Waiting for process %d to exit...\n", pid);
    
    // 调用 waitpid 等待子进程退出
    int status = 0;
    int result = waitpid(pid, &status, 0);
    
    if (result < 0) {
        printf("Error: waitpid failed (code=%d)\n", result);
        printf("  Possible reasons:\n");
        printf("    - Process %d is not a child of this shell\n", pid);
        printf("    - Process %d does not exist\n", pid);
        return -1;
    }
    
    // 解析退出状态
    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        printf("Process %d exited with code %d\n", pid, exit_code);
    } else if (WIFSIGNALED(status)) {
        int signal = WTERMSIG(status);
        const char *signal_name = "UNKNOWN";
        if (signal == SIGTERM) signal_name = "SIGTERM";
        else if (signal == SIGKILL) signal_name = "SIGKILL";
        else if (signal == SIGINT) signal_name = "SIGINT";
        else if (signal == SIGHUP) signal_name = "SIGHUP";
        
        printf("Process %d terminated by signal %s (%d)\n", pid, signal_name, signal);
    } else {
        printf("Process %d status changed (status=0x%x)\n", pid, status);
    }
    
    return 0;
}

// ============================================================================
// 文件操作命令实现
// ============================================================================

/**
 * ls 命令 - 列出目录内容
 */
static int cmd_ls(int argc, char **argv) {
    char abs_path[SHELL_MAX_PATH_LENGTH];
    const char *path;
    
    // 如果没有指定路径，使用当前目录
    if (argc == 1) {
        if (getcwd(abs_path, sizeof(abs_path)) == 0) {
            strcpy(abs_path, "/");
        }
        path = abs_path;
    } else {
        if (shell_resolve_path(argv[1], abs_path, sizeof(abs_path)) != 0) {
            printf("Error: Invalid path\n");
            return -1;
        }
        path = abs_path;
    }
    
    // 打开目录
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        printf("Error: Cannot open directory '%s'\n", path);
        return -1;
    }
    
    printf("Directory: %s\n", path);
    printf("================================================================================\n");
    
    // 读取目录项
    struct dirent entry;
    uint32_t index = 0;
    int count = 0;
    
    while (getdents(fd, index, &entry) == 0) {
        // 根据文件类型设置颜色和显示格式
        if (entry.d_type == DT_DIR) {
            printf("%-20s <DIR>\n", entry.d_name);
        } else if (entry.d_type == DT_REG) {
            // 对于常规文件，尝试获取文件大小
            // 注意：这里简化处理，不获取文件大小
            printf("%-20s\n", entry.d_name);
        } else if (entry.d_type == DT_CHR) {
            printf("%-20s <CHR>\n", entry.d_name);
        } else if (entry.d_type == DT_BLK) {
            printf("%-20s <BLK>\n", entry.d_name);
        } else if (entry.d_type == DT_LNK) {
            printf("%-20s <LNK>\n", entry.d_name);
        } else {
            printf("%-20s\n", entry.d_name);
        }
        count++;
        index++;
    }
    
    if (count == 0) {
        printf("(empty)\n");
    }
    
    close(fd);
    return 0;
}

/**
 * cat 命令 - 显示文件内容
 */
static int cmd_cat(int argc, char **argv) {
    if (argc < 2) {
        printf("Error: Usage: cat <file>\n");
        return -1;
    }
    
    char abs_path[SHELL_MAX_PATH_LENGTH];
    if (shell_resolve_path(argv[1], abs_path, sizeof(abs_path)) != 0) {
        printf("Error: Invalid path\n");
        return -1;
    }
    
    // 打开文件
    int fd = open(abs_path, O_RDONLY, 0);
    if (fd < 0) {
        printf("Error: Cannot open file '%s'\n", abs_path);
        return -1;
    }
    
    // 读取并显示文件内容
    char buffer[512];
    int bytes_read;
    int is_dev_zero = (strcmp(abs_path, "/dev/zero") == 0);
    int is_dev_console = (strcmp(abs_path, "/dev/console") == 0);
    size_t zero_bytes_previewed = 0;
    int reached_zero_limit = 0;
    int interrupted = 0;
    
    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        // 输出内容（只输出可打印字符）
        for (int i = 0; i < bytes_read; i++) {
            if (is_dev_console && (unsigned char)buffer[i] == SHELL_CTRL_C) {
                interrupted = 1;
                break;
            }
            
            if (buffer[i] >= 32 && buffer[i] <= 126) {
                printf("%c", buffer[i]);
            } else if (buffer[i] == '\n') {
                printf("\n");
            } else if (buffer[i] == '\t') {
                printf("    ");  // Tab 转换为空格
            }
        }

        if (interrupted) {
            break;
        }

        if (is_dev_zero) {
            if (zero_bytes_previewed + (size_t)bytes_read >= SHELL_CAT_ZERO_PREVIEW) {
                reached_zero_limit = 1;
                break;
            }
            zero_bytes_previewed += (size_t)bytes_read;
        }
    }
    
    if (interrupted) {
        printf("\n[cat] Interrupted by Ctrl+C\n");
    } else if (is_dev_zero && reached_zero_limit) {
        printf("\n[cat] /dev/zero produces infinite zero bytes. Stopped after %u bytes to keep the shell responsive.\n",
               (unsigned)SHELL_CAT_ZERO_PREVIEW);
    } else {
        printf("\n");
    }
    close(fd);
    return 0;
}

/**
 * touch 命令 - 创建空文件
 */
static int cmd_touch(int argc, char **argv) {
    if (argc < 2) {
        printf("Error: Usage: touch <file>\n");
        return -1;
    }
    
    char abs_path[SHELL_MAX_PATH_LENGTH];
    if (shell_resolve_path(argv[1], abs_path, sizeof(abs_path)) != 0) {
        printf("Error: Invalid path\n");
        return -1;
    }
    
    // 尝试创建文件（如果不存在）
    int fd = open(abs_path, O_CREAT | O_WRONLY, FS_PERM_READ | FS_PERM_WRITE);
    if (fd < 0) {
        printf("Error: Failed to create file '%s'\n", abs_path);
        return -1;
    }
    
    close(fd);
    printf("File '%s' created\n", abs_path);
    return 0;
}

/**
 * write 命令 - 将文本写入文件
 */
static int cmd_write(int argc, char **argv) {
    if (argc < 3) {
        printf("Error: Usage: write <file> <text...>\n");
        return -1;
    }
    
    char abs_path[SHELL_MAX_PATH_LENGTH];
    if (shell_resolve_path(argv[1], abs_path, sizeof(abs_path)) != 0) {
        printf("Error: Invalid path\n");
        return -1;
    }
    
    // 打开文件（创建或截断）
    int fd = open(abs_path, O_CREAT | O_WRONLY | O_TRUNC, FS_PERM_READ | FS_PERM_WRITE);
    if (fd < 0) {
        printf("Error: Cannot open file '%s' for writing\n", abs_path);
        return -1;
    }
    
    // 构建要写入的文本内容
    size_t total_len = 0;
    for (int i = 2; i < argc; i++) {
        total_len += strlen(argv[i]);
        if (i < argc - 1) {
            total_len += 1;  // 空格
        }
    }
    total_len += 1;  // 换行符
    
    // 分配缓冲区（在栈上）
    char buffer[512];
    if (total_len >= sizeof(buffer)) {
        printf("Error: Text too long (max %zu bytes)\n", sizeof(buffer) - 1);
        close(fd);
        return -1;
    }
    
    // 构建文本内容
    size_t pos = 0;
    for (int i = 2; i < argc; i++) {
        size_t len = strlen(argv[i]);
        if (pos + len >= sizeof(buffer) - 1) {
            break;
        }
        memcpy(buffer + pos, argv[i], len);
        pos += len;
        if (i < argc - 1) {
            buffer[pos++] = ' ';
        }
    }
    buffer[pos++] = '\n';
    buffer[pos] = '\0';
    
    // 写入文件
    int written = write(fd, buffer, pos);
    close(fd);
    
    if (written != (int)pos) {
        printf("Error: Failed to write all data to file '%s'\n", abs_path);
        printf("Written: %d bytes, Expected: %zu bytes\n", written, pos);
        return -1;
    }
    
    printf("Written %d bytes to '%s'\n", written, abs_path);
    return 0;
}

/**
 * rm 命令 - 删除文件
 */
static int cmd_rm(int argc, char **argv) {
    if (argc < 2) {
        printf("Error: Usage: rm <file>\n");
        return -1;
    }
    
    char abs_path[SHELL_MAX_PATH_LENGTH];
    if (shell_resolve_path(argv[1], abs_path, sizeof(abs_path)) != 0) {
        printf("Error: Invalid path\n");
        return -1;
    }
    
    // 不能删除根目录
    if (strcmp(abs_path, "/") == 0) {
        printf("Error: Cannot remove root directory\n");
        return -1;
    }
    
    // 删除文件
    if (shell_unlink(abs_path) != 0) {
        printf("Error: Failed to remove file '%s'\n", abs_path);
        return -1;
    }
    
    printf("File '%s' removed\n", abs_path);
    return 0;
}

/**
 * mkdir 命令 - 创建目录
 */
static int cmd_mkdir(int argc, char **argv) {
    if (argc < 2) {
        printf("Error: Usage: mkdir <dir>\n");
        return -1;
    }
    
    char abs_path[SHELL_MAX_PATH_LENGTH];
    if (shell_resolve_path(argv[1], abs_path, sizeof(abs_path)) != 0) {
        printf("Error: Invalid path\n");
        return -1;
    }
    
    // 创建目录（直接调用系统调用，避免函数名冲突）
    int ret = (int)syscall2(SYS_MKDIR, (uint32_t)abs_path, 
                            FS_PERM_READ | FS_PERM_WRITE | FS_PERM_EXEC);
    if (ret != 0) {
        printf("Error: Failed to create directory '%s'\n", abs_path);
        return -1;
    }
    
    printf("Directory '%s' created\n", abs_path);
    return 0;
}

/**
 * rmdir 命令 - 删除目录
 */
static int cmd_rmdir(int argc, char **argv) {
    if (argc < 2) {
        printf("Error: Usage: rmdir <dir>\n");
        return -1;
    }
    
    char abs_path[SHELL_MAX_PATH_LENGTH];
    if (shell_resolve_path(argv[1], abs_path, sizeof(abs_path)) != 0) {
        printf("Error: Invalid path\n");
        return -1;
    }
    
    // 不能删除根目录
    if (strcmp(abs_path, "/") == 0) {
        printf("Error: Cannot remove root directory\n");
        return -1;
    }
    
    // 删除目录（使用 unlink，因为目录也是通过 unlink 删除的）
    if (shell_unlink(abs_path) != 0) {
        printf("Error: Failed to remove directory '%s'\n", abs_path);
        return -1;
    }
    
    printf("Directory '%s' removed\n", abs_path);
    return 0;
}

// ============================================================================
// Shell 核心函数
// ============================================================================

static const shell_command_t *shell_find_command(const char *name) {
    for (int i = 0; commands[i].name; i++) {
        if (strcmp(commands[i].name, name) == 0) {
            return &commands[i];
        }
    }
    return 0;
}

static int shell_execute_command(int argc, char **argv) {
    if (argc == 0) return 0;
    
    const shell_command_t *cmd = shell_find_command(argv[0]);
    if (!cmd) {
        printf("Error: Unknown command '%s'\n", argv[0]);
        printf("Type 'help' for a list of available commands.\n");
        return -1;
    }
    
    return cmd->handler(argc, argv);
}

static void shell_init(void) {
    memset(&shell_state, 0, sizeof(shell_state_t));
    shell_state.running = 1;
    strcpy(shell_state.cwd, "/");
    shell_state.history_count = 0;
    shell_state.history_index = -1;
}

// ANSI 颜色代码
#define ANSI_RESET      "\033[0m"
#define ANSI_BOLD       "\033[1m"
#define ANSI_RED        "\033[31m"
#define ANSI_GREEN      "\033[32m"
#define ANSI_YELLOW     "\033[33m"
#define ANSI_BLUE       "\033[34m"
#define ANSI_MAGENTA    "\033[35m"
#define ANSI_CYAN       "\033[36m"
#define ANSI_WHITE      "\033[37m"
#define ANSI_BRIGHT_RED     "\033[91m"
#define ANSI_BRIGHT_GREEN   "\033[92m"
#define ANSI_BRIGHT_YELLOW  "\033[93m"
#define ANSI_BRIGHT_BLUE    "\033[94m"
#define ANSI_BRIGHT_MAGENTA "\033[95m"
#define ANSI_BRIGHT_CYAN    "\033[96m"
#define ANSI_BRIGHT_WHITE   "\033[97m"

static void shell_print_welcome(void) {
    // 上边框 - 青色
    printf(ANSI_CYAN);
    printf("================================================================================\n");
    
    // ASCII Art Logo - 渐变色效果 (黄色到橙色/红色)
    printf(ANSI_BRIGHT_YELLOW);
    printf("     ____          _              ___  ____\n");
    printf(ANSI_YELLOW);
    printf("    / ___|__ _ ___| |_ ___  _ __ / _ \\/ ___|\n");
    printf(ANSI_BRIGHT_RED);
    printf("   | |   / _` / __| __/ _ \\| '__| | | \\___ \\\n");
    printf(ANSI_RED);
    printf("   | |__| (_| \\__ \\ || (_) | |  | |_| |___) |\n");
    printf(ANSI_BRIGHT_MAGENTA);
    printf("    \\____\\__,_|___/\\__\\___/|_|   \\___/|____/\n");
    
    printf(ANSI_RESET "\n");
    
    // 版本信息 - 亮绿色
    printf(ANSI_BRIGHT_GREEN "          CastorOS User Shell " ANSI_BRIGHT_CYAN "v%s\n" ANSI_RESET, SHELL_VERSION);
    printf("\n");
    
    // 欢迎信息 - 白色/亮白色
    printf(ANSI_BRIGHT_WHITE "          Welcome to " ANSI_BRIGHT_YELLOW "CastorOS" ANSI_BRIGHT_WHITE "!\n" ANSI_RESET);
    printf(ANSI_WHITE "          Type '" ANSI_BRIGHT_GREEN "help" ANSI_WHITE "' for available commands\n" ANSI_RESET);
    printf("\n");
    
    // 下边框 - 青色
    printf(ANSI_CYAN);
    printf("================================================================================\n");
    printf(ANSI_RESET);
}

static void shell_run(void) {
    shell_print_welcome();
    
    while (shell_state.running) {
        // 清理所有已退出的僵尸子进程（非阻塞）
        // 这会自动回收后台进程的资源
        int status;
        int zombie_pid;
        while ((zombie_pid = waitpid(-1, &status, WNOHANG)) > 0) {
            // 静默清理，不打印消息（除非想通知用户）
            // printf("[Background process %d exited]\n", zombie_pid);
        }
        
        // 显示提示符
        print(SHELL_PROMPT);
        
        // 读取输入
        memset(shell_state.input_buffer, 0, SHELL_MAX_INPUT_LENGTH);
        int read_status = shell_read_line(shell_state.input_buffer, SHELL_MAX_INPUT_LENGTH);
        if (read_status == SHELL_INPUT_INTERRUPTED) {
            continue;
        }
        
        // 先添加到历史记录（在解析之前，因为解析会修改 buffer）
        if (shell_state.input_buffer[0] != '\0') {
            shell_add_history(shell_state.input_buffer);
        }
        
        // 解析命令
        if (shell_parse_command(shell_state.input_buffer, 
                                &shell_state.argc, 
                                shell_state.argv) == 0) {
            if (shell_state.argc > 0) {
                // 执行命令
                shell_execute_command(shell_state.argc, shell_state.argv);
            }
        }
    }
}

// ============================================================================
// 程序入口
// ============================================================================

void _start(void) {
    // 移除直接 I/O 操作的测试，避免权限问题
    // 直接启动 shell
    shell_init();
    shell_run();
    exit(0);
}
