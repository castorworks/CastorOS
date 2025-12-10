# CastorOS

> CastorOS is an operating system designed for learning and fun.

## 支持的架构

CastorOS 支持以下 CPU 架构：

| 架构 | 描述 | 状态 |
|------|------|------|
| i686 | Intel x86 32位 | ✅ 完整支持 |
| x86_64 | AMD64/Intel 64位 | ✅ 基础支持 |
| arm64 | ARM AArch64 | ✅ 基础支持 |

## 构建系统

### 安装开发环境

参考 [docs/00-environment.md](./docs/00-environment.md) 安装开发环境和交叉编译器。

### 基本构建命令

```bash
# 构建内核 (默认 i686 架构)
make

# 指定架构构建
make ARCH=i686      # x86 32位
make ARCH=x86_64    # x86 64位
make ARCH=arm64     # ARM64

# 运行内核
make run                    # 带 GUI
make run-silent             # 无 GUI (仅串口输出)

# 调试模式 (等待 GDB 连接)
make debug
make debug-silent

# 清理构建文件
make clean                  # 清理当前架构
make clean-all              # 清理所有架构

# 查看构建配置
make info

# 查看帮助
make help
```

### 用户空间程序

```bash
# 构建用户程序
make shell        # 用户 Shell
make hello        # Hello World 示例
make tests        # 用户态测试

# 创建可启动磁盘镜像
make disk

# 从磁盘镜像运行 (包含网络支持)
make run-disk
```

### IDE 支持

```bash
# 生成 compile_commands.json (用于 clangd 等 IDE 插件)
make compile-db
```

## 当前进展

+ [x] [开发环境搭建](./docs/00-environment.md)
+ [x] [系统引导](./docs/01-boot.md)
+ [x] [基础设施](./docs/02-infrastructure.md)
+ [x] [内存管理](./docs/03-mm.md)
+ [x] [补充驱动](./docs/04-drivers.md)
+ [x] [任务管理](./docs/05-task.md)
+ [x] [内核 Shell](./docs/06-kernel-shell.md)
+ [x] [文件系统](./docs/07-fs.md)
+ [x] [FAT32 文件系统](./docs/08-fat32.md)
+ [x] [用户模式](./docs/09-usermode.md)
+ [x] [同步机制](./docs/10-sync.md)
+ [x] [系统增强](./docs/11-system-enhancement.md)

## 下一步安排

+ [ ] [网络栈](./docs/12-network.md)
+ [ ] [intel E1000 网卡驱动](./docs/13-intel-e1000-network-card-driver.md)
+ [ ] [IBM ThinkPad T41 显卡驱动](./docs/14-ibm-thinkpad-t41-graphics.md)
+ [ ] [USB 1.1 驱动](./docs/15-usb-1.1-driver.md)

## Git 提交格式

+ `feat` 添加了新特性
+ `fix` 修复问题
+ `style` 无逻辑改动的代码风格调整
+ `perf` 性能/优化
+ `refactor` 重构
+ `revert` 回滚提交
+ `test` 测试
+ `docs` 文档
+ `chore` 依赖或者脚手架调整
+ `workflow` 工作流优化
+ `ci` 持续集成
+ `types` 类型定义
+ `wip` 开发中
