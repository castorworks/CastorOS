// ============================================================================
// kernel_shell.c - 内核 Shell 实现
// ============================================================================

#include <kernel/kernel_shell.h>
#include <kernel/version.h>
#include <kernel/task.h>
#include <kernel/io.h>

#include <drivers/vga.h>
#include <drivers/keyboard.h>
#include <drivers/timer.h>

#include <mm/pmm.h>
#include <mm/heap.h>

#include <lib/kprintf.h>
#include <lib/string.h>

// ============================================================================
// Shell 状态
// ============================================================================

static shell_state_t shell_state;

// ============================================================================
// 内建命令声明
// ============================================================================

static int cmd_help(int argc, char **argv);
static int cmd_clear(int argc, char **argv);
static int cmd_echo(int argc, char **argv);
static int cmd_version(int argc, char **argv);
static int cmd_sysinfo(int argc, char **argv);
static int cmd_uptime(int argc, char **argv);
static int cmd_free(int argc, char **argv);
static int cmd_ps(int argc, char **argv);
static int cmd_reboot(int argc, char **argv);
static int cmd_exit(int argc, char **argv);

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
    {"sysinfo",  "Display system information",        "sysinfo",           cmd_sysinfo},
    {"uptime",   "Show system uptime",                "uptime",            cmd_uptime},
    
    // 内存管理命令
    {"free",     "Display memory usage",              "free",              cmd_free},
    
    // 进程管理命令
    {"ps",       "List running processes",            "ps",                cmd_ps},
    
    // 系统控制命令
    {"reboot",   "Reboot the system",                 "reboot",            cmd_reboot},
    
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
 * 格式化内存大小（字节转为 KB/MB）
 */
static void format_memory_size(uint32_t bytes, char *buffer, size_t size) {
    if (bytes >= 1024 * 1024) {
        snprintf(buffer, size, "%u MB", bytes / (1024 * 1024));
    } else if (bytes >= 1024) {
        snprintf(buffer, size, "%u KB", bytes / 1024);
    } else {
        snprintf(buffer, size, "%u bytes", bytes);
    }
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
}

/**
 * 显示欢迎信息
 */
static void shell_print_welcome(void) {
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    kprintf("================================================================================\n");
    kprintf("     ____          _              ___  ____\n");
    kprintf("    / ___|__ _ ___| |_ ___  _ __ / _ \\/ ___|\n");
    kprintf("   | |   / _` / __| __/ _ \\| '__| | | \\___ \\\n");
    kprintf("   | |__| (_| \\__ \\ || (_) | |  | |_| |___) |\n");
    kprintf("    \\____\\__,_|___/\\__\\___/|_|   \\___/|____/\n");
    kprintf("\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    kprintf("          CastorOS Kernel Shell v%s\n", KERNEL_VERSION);
    kprintf("\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    kprintf("          Welcome to CastorOS!\n");
    kprintf("          Type 'help' for available commands\n");
    kprintf("\n");
    kprintf("================================================================================\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

/**
 * 显示提示符
 */
void shell_print_prompt(void) {
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    kprintf(SHELL_PROMPT);
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
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
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        kprintf("Error: Unknown command '%s'\n", argv[0]);
        kprintf("Type 'help' for a list of available commands.\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
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
        vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        kprintf("Available commands:\n");
        kprintf("================================================================================\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        
        for (int i = 0; commands[i].name != NULL; i++) {
            kprintf("  %-12s - %s\n", commands[i].name, commands[i].description);
        }
        
        kprintf("\n");
        vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        kprintf("Type 'help <command>' for more information on a specific command.\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    } else {
        // 显示特定命令的帮助
        const shell_command_t *cmd = shell_find_command(argv[1]);
        if (cmd == NULL) {
            vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            kprintf("Error: Unknown command '%s'\n", argv[1]);
            vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            return -1;
        }
        
        vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        kprintf("Command: %s\n", cmd->name);
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
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
    
    vga_clear();
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
    
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    kprintf("CastorOS Kernel Version %s\n", KERNEL_VERSION);
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    kprintf("Compiled on: %s %s\n", __DATE__, __TIME__);
    kprintf("Architecture: i686 (x86 32-bit)\n");
    return 0;
}

/**
 * sysinfo 命令 - 显示系统信息
 */
static int cmd_sysinfo(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    char uptime_str[128];
    char mem_total_str[32], mem_used_str[32], mem_free_str[32];
    
    // 获取运行时间
    uint32_t uptime_ms = timer_get_ticks() * 10;  // 假设 100 Hz
    format_uptime(uptime_ms, uptime_str, sizeof(uptime_str));
    
    // 获取内存信息
    pmm_info_t pmm_info = pmm_get_info();
    uint32_t total_mem = pmm_info.total_frames * PAGE_SIZE;
    uint32_t used_mem = pmm_info.used_frames * PAGE_SIZE;
    uint32_t free_mem = pmm_info.free_frames * PAGE_SIZE;
    
    format_memory_size(total_mem, mem_total_str, sizeof(mem_total_str));
    format_memory_size(used_mem, mem_used_str, sizeof(mem_used_str));
    format_memory_size(free_mem, mem_free_str, sizeof(mem_free_str));
    
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    kprintf("System Information\n");
    kprintf("================================================================================\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    kprintf("Kernel:          CastorOS v%s\n", KERNEL_VERSION);
    kprintf("Architecture:    i686 (x86 32-bit)\n");
    kprintf("Uptime:          %s\n", uptime_str);
    kprintf("\n");
    kprintf("Total Memory:    %s\n", mem_total_str);
    kprintf("Used Memory:     %s\n", mem_used_str);
    kprintf("Free Memory:     %s\n", mem_free_str);
    
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
    
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    kprintf("Memory Usage\n");
    kprintf("================================================================================\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
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
    
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    kprintf("Process List\n");
    kprintf("================================================================================\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
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
                task->total_runtime * 10);  // 转换为毫秒
    }
    
    return 0;
}

/**
 * reboot 命令 - 重启系统
 */
static int cmd_reboot(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    kprintf("Rebooting system...\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    // 使用键盘控制器重启
    uint8_t temp;
    
    // 禁用中断
    __asm__ volatile ("cli");
    
    // 清空键盘缓冲区
    do {
        temp = inb(0x64);
        if (temp & 0x01) {
            inb(0x60);
        }
    } while (temp & 0x02);
    
    // 发送重启命令
    outb(0x64, 0xFE);
    
    // 如果上面的方法失败，使用三重故障
    __asm__ volatile ("int $0x00");
    
    // 永远不会到达这里
    while (1) {
        __asm__ volatile ("hlt");
    }
    
    return 0;
}

/**
 * exit 命令 - 退出 Shell
 */
static int cmd_exit(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    kprintf("Exiting shell...\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    shell_state.running = false;
    return 0;
}
