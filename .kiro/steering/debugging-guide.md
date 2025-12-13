---
inclusion: always
---

# CastorOS 调试指南

## 超时配置

- 默认测试超时: 8 秒 (可通过 `TEST_TIMEOUT` 调整)
- 统一使用 `timeout` 命令 (macOS 需安装 coreutils)
- 输出限制: 200 行 (可通过 `OUTPUT_LINES` 调整)

## 快速测试命令

```bash
# 单架构测试 (推荐)
make test                      # 测试 i686 (默认)
make test ARCH=x86_64          # 测试 x86_64
make test ARCH=arm64           # 测试 ARM64

# 测试所有架构
make test-all

# 自定义超时
make test TEST_TIMEOUT=15      # 15秒超时
```

## 调试命令

```bash
# 捕获输出到文件
make debug-capture             # 输出保存到 build/$(ARCH)/debug.log

# GDB 调试 (等待连接)
make debug                     # 带 GUI
make debug-silent              # 无 GUI

# 手动调试命令 (参考，需先运行 make disk ARCH=xxx)
# i686: 使用 GRUB 磁盘镜像
timeout 8 qemu-system-i386 -hda build/i686/bootable.img -serial stdio -display none 2>&1 | head -200

# x86_64: 使用 GRUB 磁盘镜像
timeout 15 qemu-system-x86_64 -hda build/x86_64/bootable.img -serial stdio -display none 2>&1 | head -200

# arm64: 使用 -M virt 机器类型 (直接 -kernel)
timeout 8 qemu-system-aarch64 -M virt -cpu cortex-a72 -kernel build/arm64/castor.bin -nographic 2>&1 | head -200
```

## 构建验证

```bash
# 仅编译检查
make check                     # 当前架构
make build-all                 # 所有架构

# 查看配置
make info                      # 显示当前配置
make sources                   # 列出源文件
```

## 常见问题

1. **timeout 未找到**: `brew install coreutils` (macOS)
2. **交叉编译器未找到**: 运行 `scripts/cross-compiler-install.sh`
3. **QEMU 未找到**: `brew install qemu`

## 架构特定说明

| 架构 | QEMU | 启动方式 | 状态 |
|------|------|----------|------|
| i686 | qemu-system-i386 | -hda 磁盘镜像 | 主要 |
| x86_64 | qemu-system-x86_64 | -hda 磁盘镜像 | 开发中 |
| arm64 | qemu-system-aarch64 | -M virt -kernel | 开发中 |

**注意**: i686/x86_64 测试时会自动构建 GRUB 磁盘镜像。
