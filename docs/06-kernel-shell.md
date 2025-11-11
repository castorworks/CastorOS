# 阶段 6: 内核 Shell

## 概述

本阶段为 CastorOS 实现了一个交互式内核 Shell，作为用户与操作系统交互的主要界面。内核 Shell 利用键盘驱动和 VGA 驱动，提供命令行界面，允许用户查询系统信息、查看进程、管理内存等。

---

## 目标

- ✅ 实现命令行解析器（command parser）
- ✅ 实现基础内建命令（builtins）
- ✅ 提供交互式输入和回显功能

---

## 技术背景

### 什么是 Shell？

**Shell（壳层）** 是操作系统的命令解释器，为用户提供与内核交互的接口。

**Shell 的功能**：
- **命令解析**：解析用户输入的命令字符串
- **命令执行**：调用相应的系统功能
- **输入输出**：处理标准输入输出和重定向
- **脚本执行**：批量执行命令序列
- **环境管理**：管理环境变量和工作目录

**Shell 类型**：
1. **内核 Shell**：运行在内核态，直接调用内核函数（本项目）
2. **用户 Shell**：运行在用户态，通过系统调用与内核交互（如 bash、zsh）

---

### 内核 Shell vs 用户 Shell

| 特性 | 内核 Shell | 用户 Shell |
|------|-----------|-----------|
| **运行权限** | 内核态（Ring 0） | 用户态（Ring 3） |
| **功能访问** | 直接调用内核函数 | 通过系统调用 |
| **安全性** | 低（可直接访问硬件） | 高（受保护） |
| **用途** | 调试、系统管理 | 日常用户交互 |
| **示例** | GRUB shell、BIOS shell | bash、zsh、cmd.exe |

**内核 Shell 的优势**：
- 启动早，可在系统初始化时使用
- 功能强大，可直接访问硬件和内核数据结构
- 便于调试和系统诊断

**内核 Shell 的劣势**：
- 安全风险高，错误命令可能导致系统崩溃
- 不适合普通用户使用

---

## 功能规划

### 核心组件

#### 1. **命令行输入模块**（Line Editor）

**已实现功能**：
- 显示提示符（`CastorOS> `）
- 读取键盘输入并回显
- 支持基本编辑：
  - **退格（Backspace）**：删除前一个字符
  - **Enter**：提交命令
- 命令历史记录（内部存储）

**未实现功能**：
- 光标移动（左右箭头）
- 历史浏览（上下箭头）
- Tab 补全
- 多行编辑

---

#### 2. **命令解析模块**（Command Parser）

**功能**：
- 将输入字符串解析为命令和参数
- 处理空格和引号
- 支持特殊字符（如 `|`、`>`、`&`）

**示例**：
```c
// 输入："ps -a"
// 解析结果：
// argv[0] = "ps"
// argv[1] = "-a"
// argc = 2
```

**数据结构**：
```c
typedef struct {
    char *name;           // 命令名
    int argc;             // 参数个数
    char **argv;          // 参数数组
} command_t;
```

---

#### 3. **命令执行模块**（Command Executor）

**功能**：
- 在命令表中查找命令
- 调用命令处理函数
- 传递参数
- 返回执行结果

**命令表**：
```c
typedef int (*cmd_handler_t)(int argc, char **argv);

typedef struct {
    const char *name;        // 命令名
    const char *description; // 命令描述
    const char *usage;       // 使用方法
    cmd_handler_t handler;   // 处理函数
} shell_command_t;

// 命令表示例
static const shell_command_t commands[] = {
    {"help", "Show available commands", "help [command]", cmd_help},
    {"clear", "Clear screen", "clear", cmd_clear},
    {"echo", "Print text", "echo [text...]", cmd_echo},
    // ...
    {NULL, NULL, NULL, NULL}  // 结束标记
};
```

---

### 已实现的内建命令

#### 基础命令

| 命令 | 功能 | 用法 |
|------|------|------|
| **help** | 显示可用命令列表 | `help` 或 `help <command>` |
| **clear** | 清空屏幕 | `clear` |
| **echo** | 打印文本到屏幕 | `echo <text...>` |
| **version** | 显示内核版本信息 | `version` |
| **exit** | 退出 Shell | `exit` |

#### 系统信息命令

| 命令 | 功能 | 用法 |
|------|------|------|
| **sysinfo** | 显示完整系统信息 | `sysinfo` |
| **uptime** | 显示系统运行时间 | `uptime` |

**sysinfo 输出示例**：
```
System Information
================================================================================
Kernel:          CastorOS v0.0.5
Architecture:    i686 (x86 32-bit)
Uptime:          0 days, 0 hours, 5 minutes, 23 seconds

Total Memory:    32 MB
Used Memory:     4 MB
Free Memory:     28 MB
```

#### 内存管理命令

| 命令 | 功能 | 用法 |
|------|------|------|
| **free** | 显示物理内存使用情况 | `free` |

**free 输出示例**：
```
Memory Usage
================================================================================
              Total          Used          Free
Physical:     33554432       4194304       29360128
              (32768 KB)     (4096 KB)     (28672 KB)
```

#### 进程管理命令

| 命令 | 功能 | 用法 |
|------|------|------|
| **ps** | 显示所有进程列表 | `ps` |

**ps 输出示例**：
```
Process List
================================================================================
PID   Name              State       Priority  Runtime (ms)
--------------------------------------------------------------------------------
0     idle              RUNNING     0         12345
1     test_task         READY       10        523
```

#### 系统控制命令

| 命令 | 功能 | 用法 |
|------|------|------|
| **reboot** | 重启系统 | `reboot` |

---

## 实现细节

### 核心文件

- **头文件**: `src/include/kernel/kernel_shell.h`
- **实现**: `src/kernel/kernel_shell.c`

### Shell 状态管理

```c
typedef struct {
    char input_buffer[SHELL_MAX_INPUT_LENGTH];  // 输入缓冲区
    size_t input_len;                            // 当前输入长度
    size_t cursor_pos;                           // 光标位置
    
    char history[SHELL_HISTORY_SIZE][SHELL_MAX_INPUT_LENGTH];  // 历史记录
    int history_count;                           // 历史记录数量
    int history_index;                           // 当前历史索引
    
    int argc;                                    // 命令参数个数
    char *argv[SHELL_MAX_ARGS];                  // 命令参数数组
    
    bool running;                                // Shell 运行状态
} shell_state_t;
```

### 命令注册机制

所有命令通过静态数组注册：

```c
typedef int (*cmd_handler_t)(int argc, char **argv);

typedef struct {
    const char *name;        // 命令名
    const char *description; // 命令描述
    const char *usage;       // 使用方法
    cmd_handler_t handler;   // 处理函数
} shell_command_t;

static const shell_command_t commands[] = {
    {"help",    "Show available commands",    "help [command]",  cmd_help},
    {"clear",   "Clear screen",               "clear",           cmd_clear},
    {"echo",    "Print text to screen",       "echo [text...]",  cmd_echo},
    // ...
    {NULL, NULL, NULL, NULL}  // 结束标记
};
```

### Shell 主循环

```c
void kernel_shell_run(void) {
    shell_state.running = true;
    shell_print_welcome();
    
    while (shell_state.running) {
        shell_print_prompt();           // 显示提示符
        shell_clear_input();            // 清空输入缓冲区
        shell_read_line(...);           // 读取一行输入
        shell_parse_command(...);       // 解析命令
        shell_execute_command(...);     // 执行命令
    }
}
```

### 命令解析

命令解析器将输入字符串分割为命令和参数：

```c
int shell_parse_command(char *line, int *argc, char **argv) {
    // 跳过前导空格
    // 分割参数（按空格和制表符）
    // 填充 argc 和 argv
}
```

### kprintf 格式化支持

为了实现整齐的表格输出，`kprintf` 已支持左对齐格式化：

```c
// 左对齐输出
kprintf("%-5u %-17s %-11s\n", pid, name, state);

// 支持的格式修饰符：
// - : 左对齐
// 0 : 零填充（数字）
// width : 最小字段宽度
```
