// shell.c - CastorOS 用户态 Shell
// 参考内核 shell 实现，使用系统调用接口

#include <syscall.h>
#include <string.h>
#include <stdio.h>
#include <types.h>

// ============================================================================
// 常量定义
// ============================================================================

#define SHELL_MAX_INPUT_LENGTH  256
#define SHELL_MAX_ARGS          16
#define SHELL_MAX_PATH_LENGTH   256
#define SHELL_PROMPT            "CastorOS> "
#define SHELL_VERSION           "0.0.9"

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

// 从 stdin 读取一行
static int shell_read_line(char *buffer, size_t size) {
    size_t i = 0;
    char c;
    
    while (i < size - 1) {
        int ret = read(STDIN_FILENO, &c, 1);
        if (ret <= 0) break;
        
        if (c == '\n') {
            buffer[i] = '\0';
            print("\n");
            return (int)i;
        } else if (c == '\b' || c == 127) {  // 退格
            if (i > 0) {
                i--;
                print("\b \b");  // 回显退格
            }
        } else if (c >= 32 && c <= 126) {  // 可打印字符
            buffer[i++] = c;
            write(STDOUT_FILENO, &c, 1);  // 回显
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

// 文件操作命令
static int cmd_ls(int argc, char **argv);
static int cmd_cat(int argc, char **argv);
static int cmd_touch(int argc, char **argv);
static int cmd_write(int argc, char **argv);
static int cmd_rm(int argc, char **argv);
static int cmd_mkdir(int argc, char **argv);
static int cmd_rmdir(int argc, char **argv);

// ============================================================================
// 命令表
// ============================================================================

static const shell_command_t commands[] = {
    {"help",     "Show available commands",        "help [command]",    cmd_help},
    {"echo",     "Print text to screen",           "echo [text...]",    cmd_echo},
    {"version",  "Show shell version",             "version",           cmd_version},
    {"clear",    "Clear screen",                   "clear",             cmd_clear},
    {"pwd",      "Print working directory",        "pwd",               cmd_pwd},
    {"cd",       "Change directory",               "cd [path]",         cmd_cd},
    {"exit",     "Exit shell",                     "exit",              cmd_exit},
    
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
    
    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        // 输出内容（只输出可打印字符）
        for (int i = 0; i < bytes_read; i++) {
            if (buffer[i] >= 32 && buffer[i] <= 126) {
                printf("%c", buffer[i]);
            } else if (buffer[i] == '\n') {
                printf("\n");
            } else if (buffer[i] == '\t') {
                printf("    ");  // Tab 转换为空格
            }
        }
    }
    
    printf("\n");
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
}

static void shell_print_welcome(void) {
    printf("================================================================================\n");
    printf("     ____          _              ___  ____\n");
    printf("    / ___|__ _ ___| |_ ___  _ __ / _ \\/ ___|\n");
    printf("   | |   / _` / __| __/ _ \\| '__| | | \\___ \\\n");
    printf("   | |__| (_| \\__ \\ || (_) | |  | |_| |___) |\n");
    printf("    \\____\\__,_|___/\\__\\___/|_|   \\___/|____/\n");
    printf("\n");
    printf("          CastorOS User Shell v%s\n", SHELL_VERSION);
    printf("\n");
    printf("          Welcome to CastorOS!\n");
    printf("          Type 'help' for available commands\n");
    printf("\n");
    printf("================================================================================\n");
}

static void shell_run(void) {
    shell_print_welcome();
    
    while (shell_state.running) {
        // 显示提示符
        print(SHELL_PROMPT);
        
        // 读取输入
        memset(shell_state.input_buffer, 0, SHELL_MAX_INPUT_LENGTH);
        shell_read_line(shell_state.input_buffer, SHELL_MAX_INPUT_LENGTH);
        
        // 解析命令
        if (shell_parse_command(shell_state.input_buffer, 
                                &shell_state.argc, 
                                shell_state.argv) == 0) {
            if (shell_state.argc > 0) {
                shell_execute_command(shell_state.argc, shell_state.argv);
            }
        }
    }
}

// ============================================================================
// 程序入口
// ============================================================================

void _start(void) {
    shell_init();
    shell_run();
    exit(0);
}
