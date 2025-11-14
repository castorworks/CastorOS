// shell.c - CastorOS 用户态 Shell
// 参考内核 shell 实现，使用系统调用接口

#include <syscall.h>
#include <string.h>
#include <stdio.h>

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
// 内建命令声明
// ============================================================================

static int cmd_help(int argc, char **argv);
static int cmd_echo(int argc, char **argv);
static int cmd_version(int argc, char **argv);
static int cmd_exit(int argc, char **argv);
static int cmd_pwd(int argc, char **argv);
static int cmd_cd(int argc, char **argv);
static int cmd_clear(int argc, char **argv);

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
