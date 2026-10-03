#ifndef _KERNEL_SHELL_H_
#define _KERNEL_SHELL_H_

#include <types.h>

/**
 * 内核 Shell
 * 
 * 提供交互式命令行界面，用于系统管理和调试
 */

/* Shell 配置常量 */
#define SHELL_MAX_INPUT_LENGTH    256   // 最大输入长度
#define SHELL_MAX_ARGS            32    // 最大参数个数
#define SHELL_HISTORY_SIZE        16    // 历史记录大小
#define SHELL_MAX_PATH_LENGTH     256   // 最大路径长度
#define SHELL_PROMPT              "CastorOS> "

/* 命令处理函数类型 */
typedef int (*shell_cmd_handler_t)(int argc, char **argv);

/* 命令定义结构 */
typedef struct {
    const char *name;                    // 命令名
    const char *description;             // 命令描述
    const char *usage;                   // 使用方法
    shell_cmd_handler_t handler;         // 处理函数
} shell_command_t;

/* Shell 状态 */
typedef struct {
    char input_buffer[SHELL_MAX_INPUT_LENGTH];  // 输入缓冲区
    size_t cursor_pos;                          // 光标位置
    size_t input_len;                           // 当前输入长度
    
    char *argv[SHELL_MAX_ARGS];                 // 参数数组
    int argc;                                   // 参数个数
    
    char history[SHELL_HISTORY_SIZE][SHELL_MAX_INPUT_LENGTH]; // 历史记录
    int history_count;                          // 历史记录数量
    int history_index;                          // 当前历史索引
    
    char cwd[SHELL_MAX_PATH_LENGTH];            // 当前工作目录
    
    bool running;                               // Shell 是否运行中
} shell_state_t;

namespace kernel {

/**
 * @brief 内核调试 Shell
 */
class Shell {
public:
    /**
     * 初始化内核 Shell
     */
    static void init();

    /**
     * 启动 Shell 主循环
     * 该函数会阻塞，直到用户输入 "exit" 或系统关闭
     */
    static void run();

    /**
     * 显示提示符
     */
    static void print_prompt();

    /**
     * 解析命令行
     * @param line 输入行
     * @param argc 输出：参数个数
     * @param argv 输出：参数数组
     * @return 0 成功，-1 失败
     */
    static int parse_command(char *line, int *argc, char **argv);

    /**
     * 执行命令
     * @param argc 参数个数
     * @param argv 参数数组
     * @return 命令返回值
     */
    static int execute_command(int argc, char **argv);

    /**
     * 查找命令
     * @param name 命令名
     * @return 命令结构指针，未找到返回 NULL
     */
    static const shell_command_t *find_command(const char *name);

    /**
     * 添加历史记录
     * @param line 命令行
     */
    static void add_history(const char *line);

    /**
     * 清空输入缓冲区
     */
    static void clear_input();
};

} // namespace kernel

#endif // _KERNEL_SHELL_H_
