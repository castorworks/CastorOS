// ============================================================================
// kernel_shell.c - 内核 Shell 实现
// ============================================================================

#include <kernel/kernel_shell.h>
#include <kernel/version.h>
#include <kernel/task.h>
#include <kernel/io.h>
#include <kernel/system.h>

#include <drivers/vga.h>
#include <drivers/keyboard.h>
#include <drivers/timer.h>

#include <mm/pmm.h>
#include <mm/heap.h>

#include <fs/vfs.h>

#include <lib/kprintf.h>
#include <lib/string.h>

#include <net/net.h>

#include <drivers/framebuffer.h>
#include <drivers/pci.h>

// ============================================================================
// Shell 状态
// ============================================================================

static shell_state_t shell_state;

// ============================================================================
// 图形兼容层 - 自动选择 VGA 或 FB 输出
// ============================================================================

/**
 * 设置控制台颜色（自动选择 VGA 或 FB）
 */
static void shell_set_color(vga_color_t fg, vga_color_t bg) {
    if (fb_is_initialized()) {
        fb_terminal_set_vga_color((uint8_t)fg, (uint8_t)bg);
    } else {
        vga_set_color(fg, bg);
    }
}

/**
 * 清空控制台屏幕（自动选择 VGA 或 FB）
 */
static void shell_clear_screen(void) {
    if (fb_is_initialized()) {
        fb_terminal_clear();
    } else {
        vga_clear();
    }
}

// ============================================================================
// 内建命令声明
// ============================================================================

static int cmd_help(int argc, char **argv);
static int cmd_clear(int argc, char **argv);
static int cmd_echo(int argc, char **argv);
static int cmd_version(int argc, char **argv);
static int cmd_uptime(int argc, char **argv);
static int cmd_free(int argc, char **argv);
static int cmd_ps(int argc, char **argv);
static int cmd_reboot(int argc, char **argv);
static int cmd_poweroff(int argc, char **argv);
static int cmd_exit(int argc, char **argv);

// 文件操作命令
static int cmd_ls(int argc, char **argv);
static int cmd_cat(int argc, char **argv);
static int cmd_touch(int argc, char **argv);
static int cmd_rm(int argc, char **argv);
static int cmd_mkdir(int argc, char **argv);
static int cmd_rmdir(int argc, char **argv);
static int cmd_pwd(int argc, char **argv);
static int cmd_cd(int argc, char **argv);
static int cmd_write(int argc, char **argv);

// 网络命令
static int cmd_ifconfig(int argc, char **argv);
static int cmd_ping(int argc, char **argv);
static int cmd_arp(int argc, char **argv);

// 图形和设备命令
static int cmd_lspci(int argc, char **argv);
static int cmd_fbinfo(int argc, char **argv);
static int cmd_gfxdemo(int argc, char **argv);

// ============================================================================
// 命令表
// ============================================================================

static const shell_command_t commands[] = {
    // 基础命令
    {"help",     "Show available commands",           "help [command]",    cmd_help},
    {"clear",    "Clear screen",                      "clear",             cmd_clear},
    {"echo",     "Print text to screen",              "echo [text...]",    cmd_echo},
    {"version",  "Show kernel version",               "version",           cmd_version},
    {"exit",     "Exit shell",                        "exit",              cmd_exit},
    
    // 系统信息命令
    {"uptime",   "Show system uptime",                "uptime",            cmd_uptime},
    
    // 内存管理命令
    {"free",     "Display memory usage",              "free",              cmd_free},
    
    // 进程管理命令
    {"ps",       "List running processes",            "ps",                cmd_ps},
    
    // 系统控制命令
    {"reboot",   "Reboot the system",                 "reboot",            cmd_reboot},
    {"poweroff", "Power off the system",              "poweroff",          cmd_poweroff},
    
    // 文件操作命令
    {"ls",       "List directory contents",            "ls [path]",          cmd_ls},
    {"cat",      "Display file contents",              "cat <file>",         cmd_cat},
    {"touch",    "Create an empty file",              "touch <file>",        cmd_touch},
    {"write",    "Write text to file",                 "write <file> <text...>", cmd_write},
    {"rm",       "Remove a file",                     "rm <file>",           cmd_rm},
    {"mkdir",    "Create a directory",                "mkdir <dir>",        cmd_mkdir},
    {"rmdir",    "Remove a directory",                "rmdir <dir>",         cmd_rmdir},
    {"pwd",      "Print working directory",           "pwd",                 cmd_pwd},
    {"cd",       "Change directory",                  "cd [path]",           cmd_cd},
    
    // 网络命令
    {"ifconfig", "Configure network interface",      "ifconfig [iface] [ip netmask gw]", cmd_ifconfig},
    {"ping",     "Send ICMP echo requests",          "ping [-c count] host", cmd_ping},
    {"arp",      "Show/manage ARP cache",            "arp [-a] [-d ip]",    cmd_arp},
    
    // 图形和设备命令
    {"lspci",    "List PCI devices",                 "lspci",               cmd_lspci},
    {"fbinfo",   "Display framebuffer info",         "fbinfo",              cmd_fbinfo},
    {"gfxdemo",  "Run graphics demo",                "gfxdemo",             cmd_gfxdemo},
    
    // 结束标记
    {NULL, NULL, NULL, NULL}
};

// ============================================================================
// 工具函数
// ============================================================================

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
    
    snprintf(buffer, size, "%u days, %u hours, %u minutes, %u seconds",
             days, hours, minutes, seconds);
}

/**
 * 规范化路径，处理 . 和 ..
 * @param path 输入路径
 * @param normalized 输出：规范化后的路径缓冲区
 * @param size 缓冲区大小
 * @return 0 成功，-1 失败
 */
static int shell_normalize_path(const char *path, char *normalized, size_t size) {
    if (!path || !normalized || size == 0) {
        return -1;
    }
    
    // 路径组件栈（最多支持 64 层）
    const size_t MAX_COMPONENTS = 64;
    char components[MAX_COMPONENTS][128];
    size_t component_count = 0;
    
    const char *p = path;
    char current_component[128];
    size_t comp_len = 0;
    bool is_absolute = (path[0] == '/');
    
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
            continue;  // 空组件，跳过
        } else if (strcmp(current_component, ".") == 0) {
            // 当前目录，忽略
            continue;
        } else if (strcmp(current_component, "..") == 0) {
            // 父目录
            if (component_count > 0) {
                // 有组件可以回退
                component_count--;
            } else if (!is_absolute) {
                // 相对路径中，.. 在根目录时保持在根目录
                // 不添加组件即可
            }
            // 绝对路径中，.. 在根目录时保持在根目录
        } else {
            // 普通组件，添加到栈中
            if (component_count >= MAX_COMPONENTS) {
                return -1;  // 路径太深
            }
            strncpy(components[component_count], current_component, 127);
            components[component_count][127] = '\0';
            component_count++;
        }
    }
    
    // 构建规范化路径
    size_t pos = 0;
    
    // 绝对路径以 '/' 开头
    if (is_absolute) {
        if (pos < size - 1) {
            normalized[pos++] = '/';
        }
    }
    
    // 添加所有组件
    for (size_t i = 0; i < component_count; i++) {
        size_t comp_len = strlen(components[i]);
        
        // 添加 '/'（除了第一个组件在绝对路径时）
        if (pos > 0 && normalized[pos - 1] != '/') {
            if (pos < size - 1) {
                normalized[pos++] = '/';
            }
        }
        
        // 添加组件名
        for (size_t j = 0; j < comp_len && pos < size - 1; j++) {
            normalized[pos++] = components[i][j];
        }
    }
    
    // 如果路径为空，至少要有 '/'
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
 * 将相对路径转换为绝对路径，支持 . 和 ..
 * @param path 输入路径（相对或绝对）
 * @param abs_path 输出：绝对路径缓冲区
 * @param size 缓冲区大小
 * @return 0 成功，-1 失败
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
    size_t cwd_len = strlen(shell_state.cwd);
    size_t path_len = strlen(path);
    
    // 计算需要的总长度
    size_t total_len = cwd_len + 1 + path_len;  // cwd + '/' + path
    
    if (total_len >= sizeof(temp_path)) {
        return -1;
    }
    
    // 构建临时绝对路径
    strncpy(temp_path, shell_state.cwd, sizeof(temp_path) - 1);
    temp_path[sizeof(temp_path) - 1] = '\0';
    
    size_t current_len = strlen(temp_path);
    
    // 如果 cwd 不是根目录，添加 '/'
    if (cwd_len > 1 && current_len < sizeof(temp_path) - 1) {
        temp_path[current_len] = '/';
        current_len++;
        temp_path[current_len] = '\0';
    }
    
    // 手动拼接路径
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
    
    // 复制到输出缓冲区
    if (strlen(normalized) >= size) {
        return -1;
    }
    strncpy(abs_path, normalized, size - 1);
    abs_path[size - 1] = '\0';
    
    return 0;
}

// ============================================================================
// Shell 核心函数
// ============================================================================

/**
 * 初始化内核 Shell
 */
void kernel_shell_init(void) {
    memset(&shell_state, 0, sizeof(shell_state_t));
    shell_state.running = false;
    strcpy(shell_state.cwd, "/");  // 初始化当前工作目录为根目录
}

/**
 * 显示欢迎信息
 */
static void shell_print_welcome(void) {
    shell_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    kprintf("================================================================================\n");
    kprintf("     ____          _              ___  ____\n");
    kprintf("    / ___|__ _ ___| |_ ___  _ __ / _ \\/ ___|\n");
    kprintf("   | |   / _` / __| __/ _ \\| '__| | | \\___ \\\n");
    kprintf("   | |__| (_| \\__ \\ || (_) | |  | |_| |___) |\n");
    kprintf("    \\____\\__,_|___/\\__\\___/|_|   \\___/|____/\n");
    kprintf("\n");
    shell_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    kprintf("          CastorOS Kernel Shell v%s\n", KERNEL_VERSION);
    kprintf("\n");
    shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    kprintf("          Welcome to CastorOS!\n");
    kprintf("          Type 'help' for available commands\n");
    kprintf("\n");
    kprintf("================================================================================\n");
    shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

/**
 * 显示提示符
 */
void shell_print_prompt(void) {
    shell_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    kprintf(SHELL_PROMPT);
    shell_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

/**
 * 清空输入缓冲区
 */
void shell_clear_input(void) {
    memset(shell_state.input_buffer, 0, SHELL_MAX_INPUT_LENGTH);
    shell_state.input_len = 0;
    shell_state.cursor_pos = 0;
}

/**
 * 解析命令行
 */
int shell_parse_command(char *line, int *argc, char **argv) {
    *argc = 0;
    
    // 跳过前导空格
    while (*line && (*line == ' ' || *line == '\t')) {
        line++;
    }
    
    // 如果是空行，返回
    if (*line == '\0') {
        return 0;
    }
    
    // 分割参数
    while (*line && *argc < SHELL_MAX_ARGS) {
        // 跳过空格
        while (*line && (*line == ' ' || *line == '\t')) {
            line++;
        }
        
        if (*line == '\0') {
            break;
        }
        
        // 记录参数起始位置
        argv[*argc] = line;
        (*argc)++;
        
        // 查找参数结束位置
        while (*line && *line != ' ' && *line != '\t') {
            line++;
        }
        
        // 添加字符串结束符
        if (*line) {
            *line = '\0';
            line++;
        }
    }
    
    return 0;
}

/**
 * 查找命令
 */
const shell_command_t *shell_find_command(const char *name) {
    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(commands[i].name, name) == 0) {
            return &commands[i];
        }
    }
    return NULL;
}

/**
 * 执行命令
 */
int shell_execute_command(int argc, char **argv) {
    if (argc == 0) {
        return 0;
    }
    
    const shell_command_t *cmd = shell_find_command(argv[0]);
    if (cmd == NULL) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Unknown command '%s'\n", argv[0]);
        kprintf("Type 'help' for a list of available commands.\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    return cmd->handler(argc, argv);
}

/**
 * 添加历史记录
 */
void shell_add_history(const char *line) {
    // 如果历史记录已满，移除最旧的
    if (shell_state.history_count >= SHELL_HISTORY_SIZE) {
        for (int i = 0; i < SHELL_HISTORY_SIZE - 1; i++) {
            strcpy(shell_state.history[i], shell_state.history[i + 1]);
        }
        shell_state.history_count = SHELL_HISTORY_SIZE - 1;
    }
    
    // 添加新记录
    strcpy(shell_state.history[shell_state.history_count], line);
    shell_state.history_count++;
    shell_state.history_index = shell_state.history_count;
}

/**
 * 读取带回显的输入行
 */
static size_t shell_read_line(char *buffer, size_t size) {
    size_t i = 0;
    
    while (i < size - 1) {
        char c = keyboard_getchar();
        
        if (c == '\n') {
            buffer[i] = '\0';
            kprintf("\n");  // 回显换行
            return i;
        } else if (c == '\b') {
            // 处理退格
            if (i > 0) {
                i--;
                // 回显退格：输出退格、空格（覆盖字符）、再退格
                kprintf("\b \b");
            }
        } else if (c >= 32 && c <= 126) {
            // 只处理可打印字符
            buffer[i++] = c;
            kprintf("%c", c);  // 回显字符
        }
    }
    
    buffer[i] = '\0';
    return i;
}

/**
 * Shell 主循环
 */
void kernel_shell_run(void) {
    shell_state.running = true;
    
    // 显示欢迎信息
    shell_print_welcome();
    
    // 主循环
    while (shell_state.running) {
        // 显示提示符
        shell_print_prompt();
        
        // 清空输入缓冲区
        shell_clear_input();
        
        // 读取一行输入（带回显）
        shell_read_line(shell_state.input_buffer, SHELL_MAX_INPUT_LENGTH);
        
        // 解析命令
        if (shell_parse_command(shell_state.input_buffer, 
                                &shell_state.argc, 
                                shell_state.argv) == 0) {
            // 如果不是空命令，添加到历史记录
            if (shell_state.argc > 0) {
                // 不将 exit 命令加入历史
                if (strcmp(shell_state.argv[0], "exit") != 0) {
                    shell_add_history(shell_state.input_buffer);
                }
                
                // 执行命令
                shell_execute_command(shell_state.argc, shell_state.argv);
            }
        }
    }
}

// ============================================================================
// 内建命令实现
// ============================================================================

/**
 * help 命令 - 显示帮助信息
 */
static int cmd_help(int argc, char **argv) {
    if (argc == 1) {
        // 显示所有命令
        shell_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        kprintf("Available commands:\n");
        kprintf("================================================================================\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        
        for (int i = 0; commands[i].name != NULL; i++) {
            kprintf("  %-12s - %s\n", commands[i].name, commands[i].description);
        }
        
        kprintf("\n");
        shell_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        kprintf("Type 'help <command>' for more information on a specific command.\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    } else {
        // 显示特定命令的帮助
        const shell_command_t *cmd = shell_find_command(argv[1]);
        if (cmd == NULL) {
            shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            kprintf("Error: Unknown command '%s'\n", argv[1]);
            shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            return -1;
        }
        
        shell_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        kprintf("Command: %s\n", cmd->name);
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        kprintf("Description: %s\n", cmd->description);
        kprintf("Usage: %s\n", cmd->usage);
    }
    
    return 0;
}

/**
 * clear 命令 - 清空屏幕
 */
static int cmd_clear(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    shell_clear_screen();
    return 0;
}

/**
 * echo 命令 - 打印文本
 */
static int cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        kprintf("%s", argv[i]);
        if (i < argc - 1) {
            kprintf(" ");
        }
    }
    kprintf("\n");
    return 0;
}

/**
 * version 命令 - 显示版本信息
 */
static int cmd_version(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    shell_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    kprintf("CastorOS Kernel Version %s\n", KERNEL_VERSION);
    shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    kprintf("Compiled on: %s %s\n", __DATE__, __TIME__);
    kprintf("Architecture: i686 (x86 32-bit)\n");
    return 0;
}

/**
 * uptime 命令 - 显示系统运行时间
 */
static int cmd_uptime(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    char uptime_str[128];
    uint32_t uptime_ms = timer_get_ticks() * 10;  // 假设 100 Hz
    format_uptime(uptime_ms, uptime_str, sizeof(uptime_str));
    
    kprintf("System uptime: %s\n", uptime_str);
    return 0;
}

/**
 * free 命令 - 显示内存使用情况
 */
static int cmd_free(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    // 获取物理内存信息
    pmm_info_t pmm_info = pmm_get_info();
    uint32_t total_mem = pmm_info.total_frames * PAGE_SIZE;
    uint32_t used_mem = pmm_info.used_frames * PAGE_SIZE;
    uint32_t free_mem = pmm_info.free_frames * PAGE_SIZE;
    
    shell_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    kprintf("Memory Usage\n");
    kprintf("================================================================================\n");
    shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    kprintf("              Total          Used          Free\n");
    kprintf("Physical:     %-12u  %-12u  %-12u\n", 
            total_mem, used_mem, free_mem);
    kprintf("              (%-8u KB) (%-8u KB) (%-8u KB)\n",
            total_mem / 1024, used_mem / 1024, free_mem / 1024);
    
    return 0;
}

/**
 * ps 命令 - 显示进程列表
 */
static int cmd_ps(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    shell_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    kprintf("Process List\n");
    kprintf("================================================================================\n");
    shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    kprintf("PID   Name              State       Priority  Runtime (ms)\n");
    kprintf("--------------------------------------------------------------------------------\n");
    
    // 遍历所有任务
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        task_t *task = task_get_by_pid(i);
        if (task == NULL || task->state == TASK_UNUSED) {
            continue;
        }
        
        const char *state_str;
        switch (task->state) {
            case TASK_READY:      state_str = "READY";      break;
            case TASK_RUNNING:    state_str = "RUNNING";    break;
            case TASK_BLOCKED:    state_str = "BLOCKED";    break;
            case TASK_TERMINATED: state_str = "TERMINATED"; break;
            default:              state_str = "UNKNOWN";    break;
        }
        
        kprintf("%-5u %-17s %-11s %-9u %llu\n",
                task->pid,
                task->name,
                state_str,
                task->priority,
                task->runtime_ms);  // 运行时间（毫秒）
    }
    
    return 0;
}

/**
 * reboot 命令 - 重启系统
 */
static int cmd_reboot(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    shell_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    kprintf("Rebooting system...\n");
    shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    system_reboot();
    return 0;
}

/**
 * poweroff 命令 - 关闭系统
 */
static int cmd_poweroff(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    shell_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    kprintf("Powering off system...\n");
    shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    system_poweroff();
    return 0;
}

/**
 * exit 命令 - 退出 Shell
 */
static int cmd_exit(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    shell_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    kprintf("Exiting shell...\n");
    shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    shell_state.running = false;
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
        path = shell_state.cwd;
    } else {
        if (shell_resolve_path(argv[1], abs_path, sizeof(abs_path)) != 0) {
            shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            kprintf("Error: Invalid path\n");
            shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            return -1;
        }
        path = abs_path;
    }
    
    // 查找目录节点
    fs_node_t *dir = vfs_path_to_node(path);
    if (!dir) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Directory '%s' not found\n", path);
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    if (dir->type != FS_DIRECTORY) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: '%s' is not a directory\n", path);
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vfs_release_node(dir);  // 释放节点
        return -1;
    }
    
    // 列出目录内容
    shell_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    kprintf("Directory: %s\n", path);
    kprintf("================================================================================\n");
    shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    uint32_t index = 0;
    struct dirent *entry;
    int count = 0;
    
    while ((entry = vfs_readdir(dir, index++)) != NULL) {
        // 使用 d_type 字段判断文件类型（如果可用）
        if (entry->d_type == DT_DIR) {
            shell_set_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
            kprintf("%-20s <DIR>\n", entry->d_name);
        } else if (entry->d_type == DT_REG) {
            // 对于常规文件，查找节点以获取大小
            fs_node_t *node = vfs_finddir(dir, entry->d_name);
            if (node) {
                shell_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                kprintf("%-20s %u bytes\n", entry->d_name, node->size);
                vfs_release_node(node);  // 释放节点
            } else {
                shell_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                kprintf("%-20s\n", entry->d_name);
            }
        } else if (entry->d_type == DT_CHR) {
            shell_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
            kprintf("%-20s <CHR>\n", entry->d_name);
        } else if (entry->d_type == DT_BLK) {
            shell_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
            kprintf("%-20s <BLK>\n", entry->d_name);
        } else if (entry->d_type == DT_LNK) {
            shell_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
            kprintf("%-20s <LNK>\n", entry->d_name);
        } else {
            // 未知类型或 d_type 未设置，回退到旧方法
            fs_node_t *node = vfs_finddir(dir, entry->d_name);
            if (node) {
                if (node->type == FS_DIRECTORY) {
                    shell_set_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
                    kprintf("%-20s <DIR>\n", entry->d_name);
                } else {
                    shell_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                    kprintf("%-20s %u bytes\n", entry->d_name, node->size);
                }
                vfs_release_node(node);  // 释放节点
            } else {
                shell_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                kprintf("%-20s\n", entry->d_name);
            }
        }
        count++;
    }
    
    if (count == 0) {
        kprintf("(empty)\n");
    }
    
    shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vfs_release_node(dir);  // 释放目录节点
    return 0;
}

/**
 * cat 命令 - 显示文件内容
 */
static int cmd_cat(int argc, char **argv) {
    if (argc < 2) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Usage: cat <file>\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    char abs_path[SHELL_MAX_PATH_LENGTH];
    if (shell_resolve_path(argv[1], abs_path, sizeof(abs_path)) != 0) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Invalid path\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    // 查找文件节点
    fs_node_t *file = vfs_path_to_node(abs_path);
    if (!file) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: File '%s' not found\n", abs_path);
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    // 检查文件类型：支持常规文件和设备文件
    if (file->type != FS_FILE && file->type != FS_CHARDEVICE && file->type != FS_BLOCKDEVICE) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: '%s' is not a readable file or device\n", abs_path);
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vfs_release_node(file);  // 释放节点
        return -1;
    }
    
    // 打开文件
    vfs_open(file, 0);
    
    // 读取并显示文件内容
    uint8_t buffer[512];
    uint32_t offset = 0;
    uint32_t max_read = 0;
    bool is_device = (file->type == FS_CHARDEVICE || file->type == FS_BLOCKDEVICE);
    
    // 对于设备文件，限制读取次数以避免无限循环
    if (is_device) {
        max_read = 1024;  // 最多读取 1024 次（约 512KB）
    }
    
    uint32_t read_count = 0;
    
    while (true) {
        uint32_t to_read;
        
        if (is_device) {
            // 设备文件：每次读取固定大小
            to_read = sizeof(buffer);
            if (read_count >= max_read) {
                break;  // 防止无限循环
            }
            read_count++;
        } else {
            // 常规文件：根据文件大小读取
            if (offset >= file->size) {
                break;
            }
            to_read = (file->size - offset > sizeof(buffer)) 
                      ? sizeof(buffer) : (file->size - offset);
        }
        
        uint32_t read = vfs_read(file, offset, to_read, buffer);
        
        if (read == 0) {
            break;  // 没有更多数据
        }
        
        // 输出内容（只输出可打印字符）
        for (uint32_t i = 0; i < read; i++) {
            if (buffer[i] >= 32 && buffer[i] <= 126) {
                kprintf("%c", buffer[i]);
            } else if (buffer[i] == '\n') {
                kprintf("\n");
            } else if (buffer[i] == '\t') {
                kprintf("    ");  // Tab 转换为空格
            }
        }
        
        offset += read;
        
        // 对于设备文件，如果读取的数据少于请求的大小，可能已经到达末尾
        if (is_device && read < to_read) {
            break;
        }
    }
    
    kprintf("\n");
    
    // 关闭文件
    vfs_close(file);
    vfs_release_node(file);  // 释放节点
    
    return 0;
}

/**
 * touch 命令 - 创建空文件
 */
static int cmd_touch(int argc, char **argv) {
    if (argc < 2) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Usage: touch <file>\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    char abs_path[SHELL_MAX_PATH_LENGTH];
    if (shell_resolve_path(argv[1], abs_path, sizeof(abs_path)) != 0) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Invalid path\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    // 创建文件
    if (vfs_create(abs_path) != 0) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Failed to create file '%s'\n", abs_path);
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    kprintf("File '%s' created\n", abs_path);
    return 0;
}

/**
 * rm 命令 - 删除文件
 */
static int cmd_rm(int argc, char **argv) {
    if (argc < 2) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Usage: rm <file>\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    char abs_path[SHELL_MAX_PATH_LENGTH];
    if (shell_resolve_path(argv[1], abs_path, sizeof(abs_path)) != 0) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Invalid path\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    // 检查文件是否存在且是文件
    fs_node_t *file = vfs_path_to_node(abs_path);
    if (!file) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: File '%s' not found\n", abs_path);
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    if (file->type != FS_FILE) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: '%s' is not a file (use rmdir for directories)\n", abs_path);
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vfs_release_node(file);  // 释放节点
        return -1;
    }
    
    vfs_release_node(file);  // 验证完成，释放节点
    
    // 删除文件
    if (vfs_unlink(abs_path) != 0) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Failed to remove file '%s'\n", abs_path);
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    kprintf("File '%s' removed\n", abs_path);
    return 0;
}

/**
 * mkdir 命令 - 创建目录
 */
static int cmd_mkdir(int argc, char **argv) {
    if (argc < 2) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Usage: mkdir <dir>\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    char abs_path[SHELL_MAX_PATH_LENGTH];
    if (shell_resolve_path(argv[1], abs_path, sizeof(abs_path)) != 0) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Invalid path\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    // 创建目录（权限：读写执行）
    if (vfs_mkdir(abs_path, FS_PERM_READ | FS_PERM_WRITE | FS_PERM_EXEC) != 0) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Failed to create directory '%s'\n", abs_path);
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    kprintf("Directory '%s' created\n", abs_path);
    return 0;
}

/**
 * rmdir 命令 - 删除目录
 */
static int cmd_rmdir(int argc, char **argv) {
    if (argc < 2) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Usage: rmdir <dir>\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    char abs_path[SHELL_MAX_PATH_LENGTH];
    if (shell_resolve_path(argv[1], abs_path, sizeof(abs_path)) != 0) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Invalid path\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    // 不能删除根目录
    if (strcmp(abs_path, "/") == 0) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Cannot remove root directory\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    // 检查目录是否存在且是目录
    fs_node_t *dir = vfs_path_to_node(abs_path);
    if (!dir) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Directory '%s' not found\n", abs_path);
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    if (dir->type != FS_DIRECTORY) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: '%s' is not a directory\n", abs_path);
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vfs_release_node(dir);  // 释放节点
        return -1;
    }
    
    // 检查目录是否为空
    struct dirent *entry = vfs_readdir(dir, 0);
    if (entry != NULL) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Directory '%s' is not empty\n", abs_path);
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vfs_release_node(dir);  // 释放节点
        return -1;
    }
    
    vfs_release_node(dir);  // 验证完成，释放节点
    
    // 删除目录
    if (vfs_unlink(abs_path) != 0) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Failed to remove directory '%s'\n", abs_path);
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    kprintf("Directory '%s' removed\n", abs_path);
    return 0;
}

/**
 * pwd 命令 - 显示当前工作目录
 */
static int cmd_pwd(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    kprintf("%s\n", shell_state.cwd);
    return 0;
}

/**
 * cd 命令 - 切换目录
 */
static int cmd_cd(int argc, char **argv) {
    const char *path;
    
    // 如果没有指定路径，切换到根目录
    if (argc == 1) {
        path = "/";
    } else {
        path = argv[1];
    }
    
    char abs_path[SHELL_MAX_PATH_LENGTH];
    if (shell_resolve_path(path, abs_path, sizeof(abs_path)) != 0) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Invalid path\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    // 查找目录节点
    fs_node_t *dir = vfs_path_to_node(abs_path);
    if (!dir) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Directory '%s' not found\n", abs_path);
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    if (dir->type != FS_DIRECTORY) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: '%s' is not a directory\n", abs_path);
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vfs_release_node(dir);  // 释放节点
        return -1;
    }
    
    vfs_release_node(dir);  // 验证完成，释放节点
    
    // 更新当前工作目录
    strncpy(shell_state.cwd, abs_path, SHELL_MAX_PATH_LENGTH - 1);
    shell_state.cwd[SHELL_MAX_PATH_LENGTH - 1] = '\0';
    
    return 0;
}

/**
 * write 命令 - 将文本写入文件
 */
static int cmd_write(int argc, char **argv) {
    if (argc < 3) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Usage: write <file> <text...>\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    char abs_path[SHELL_MAX_PATH_LENGTH];
    if (shell_resolve_path(argv[1], abs_path, sizeof(abs_path)) != 0) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Invalid path\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    // 检查文件是否存在
    fs_node_t *file = vfs_path_to_node(abs_path);
    
    // 如果是设备文件，必须已存在
    if (!file) {
        // 文件不存在，尝试创建（仅对常规文件）
        if (vfs_create(abs_path) != 0) {
            shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            kprintf("Error: File or device '%s' not found\n", abs_path);
            shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            return -1;
        }
        // 重新获取文件节点
        file = vfs_path_to_node(abs_path);
        if (!file) {
            shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            kprintf("Error: Failed to open file '%s'\n", abs_path);
            shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            return -1;
        }
    }
    
    // 检查文件类型：支持常规文件和设备文件
    if (file->type != FS_FILE && file->type != FS_CHARDEVICE && file->type != FS_BLOCKDEVICE) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: '%s' is not a writable file or device\n", abs_path);
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vfs_release_node(file);  // 释放节点
        return -1;
    }
    
    // 构建要写入的文本内容
    // 计算总长度
    size_t total_len = 0;
    for (int i = 2; i < argc; i++) {
        total_len += strlen(argv[i]);
        if (i < argc - 1) {
            total_len += 1;  // 空格
        }
    }
    total_len += 1;  // 换行符
    
    // 分配缓冲区
    char *buffer = (char *)kmalloc(total_len + 1);
    if (!buffer) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Out of memory\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vfs_release_node(file);  // 释放节点
        return -1;
    }
    
    // 构建文本内容
    size_t pos = 0;
    for (int i = 2; i < argc; i++) {
        size_t len = strlen(argv[i]);
        memcpy(buffer + pos, argv[i], len);
        pos += len;
        if (i < argc - 1) {
            buffer[pos++] = ' ';
        }
    }
    buffer[pos++] = '\n';
    buffer[pos] = '\0';
    
    // 打开文件（写入模式）
    vfs_open(file, 0);
    
    // 写入文件（覆盖模式：从偏移 0 开始写入）
    uint32_t written = vfs_write(file, 0, pos, (uint8_t *)buffer);
    
    // 关闭文件
    vfs_close(file);
    
    // 释放缓冲区
    kfree(buffer);
    
    if (written != pos) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Failed to write all data to file '%s'\n", abs_path);
        kprintf("Written: %u bytes, Expected: %u bytes\n", written, (uint32_t)pos);
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vfs_release_node(file);  // 释放节点
        return -1;
    }
    
    kprintf("Written %u bytes to '%s'\n", written, abs_path);
    vfs_release_node(file);  // 释放节点
    return 0;
}

// ============================================================================
// 网络命令实现
// ============================================================================

/**
 * ifconfig 命令 - 网络接口配置
 */
static int cmd_ifconfig(int argc, char **argv) {
    if (argc == 1) {
        // 显示所有接口
        netdev_print_all();
        return 0;
    }
    
    if (argc == 2) {
        // 显示特定接口
        netdev_t *dev = netdev_get_by_name(argv[1]);
        if (!dev) {
            shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            kprintf("Error: Interface '%s' not found\n", argv[1]);
            shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            return -1;
        }
        netdev_print_info(dev);
        return 0;
    }
    
    // 配置接口 IP 地址
    // ifconfig eth0 192.168.1.100 255.255.255.0 192.168.1.1
    if (argc >= 5) {
        netdev_t *dev = netdev_get_by_name(argv[1]);
        if (!dev) {
            shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            kprintf("Error: Interface '%s' not found\n", argv[1]);
            shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            return -1;
        }
        
        if (net_configure(argv[2], argv[3], argv[4]) < 0) {
            return -1;
        }
        
        kprintf("Interface %s configured\n", argv[1]);
        return 0;
    }
    
    // 启用/禁用接口
    if (argc == 3) {
        netdev_t *dev = netdev_get_by_name(argv[1]);
        if (!dev) {
            shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            kprintf("Error: Interface '%s' not found\n", argv[1]);
            shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            return -1;
        }
        
        if (strcmp(argv[2], "up") == 0) {
            netdev_up(dev);
            kprintf("Interface %s is up\n", argv[1]);
            return 0;
        } else if (strcmp(argv[2], "down") == 0) {
            netdev_down(dev);
            kprintf("Interface %s is down\n", argv[1]);
            return 0;
        }
    }
    
    shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    kprintf("Usage: ifconfig [iface] [ip netmask gateway]\n");
    kprintf("       ifconfig iface up|down\n");
    shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    return -1;
}

/**
 * ping 命令 - 网络连通性测试
 */
static int cmd_ping(int argc, char **argv) {
    int count = 4;  // 默认 ping 4 次
    const char *host = NULL;
    
    // 解析参数
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            // 解析 count 参数
            count = 0;
            const char *p = argv[++i];
            while (*p >= '0' && *p <= '9') {
                count = count * 10 + (*p - '0');
                p++;
            }
            if (count <= 0) count = 1;
            if (count > 100) count = 100;
        } else {
            host = argv[i];
        }
    }
    
    if (!host) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Usage: ping [-c count] host\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    // 检查是否有网络设备
    netdev_t *dev = netdev_get_default();
    if (!dev) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: No network device available\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    if (dev->ip_addr == 0) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Network interface not configured\n");
        kprintf("Use 'ifconfig %s <ip> <netmask> <gateway>' to configure\n", dev->name);
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    return net_ping(host, count);
}

/**
 * arp 命令 - ARP 缓存管理
 */
static int cmd_arp(int argc, char **argv) {
    bool show_all = false;
    const char *delete_ip = NULL;
    
    // 解析参数
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0) {
            show_all = true;
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            delete_ip = argv[++i];
        }
    }
    
    if (delete_ip) {
        // 删除指定的 ARP 条目
        uint32_t ip;
        if (str_to_ip(delete_ip, &ip) < 0) {
            shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            kprintf("Error: Invalid IP address '%s'\n", delete_ip);
            shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            return -1;
        }
        
        if (arp_cache_delete(ip) == 0) {
            kprintf("ARP entry for %s deleted\n", delete_ip);
        } else {
            shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            kprintf("Error: ARP entry for %s not found\n", delete_ip);
            shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            return -1;
        }
        return 0;
    }
    
    // 默认显示所有条目
    if (argc == 1 || show_all) {
        arp_cache_dump();
        return 0;
    }
    
    shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    kprintf("Usage: arp [-a] [-d ip]\n");
    shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    return -1;
}

// ============================================================================
// 图形和设备命令实现
// ============================================================================

/**
 * lspci 命令 - 列出 PCI 设备
 */
static int cmd_lspci(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    pci_print_all_devices();
    return 0;
}

/**
 * fbinfo 命令 - 显示帧缓冲信息
 */
static int cmd_fbinfo(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    if (!fb_is_initialized()) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Framebuffer not initialized (text mode)\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    fb_print_info();
    return 0;
}

/**
 * gfxdemo 命令 - 运行图形演示
 */
static int cmd_gfxdemo(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    if (!fb_is_initialized()) {
        shell_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Framebuffer not initialized (text mode)\n");
        shell_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return -1;
    }
    
    kprintf("Running graphics demo...\n");
    kprintf("Press any key to return to shell.\n");
    
    // 等待用户确认
    keyboard_getchar();
    
    // 运行图形演示
    fb_demo();
    
    // 等待用户按键返回
    keyboard_getchar();
    
    // 恢复终端
    fb_terminal_init();
    shell_print_welcome();
    
    return 0;
}
