# 阶段 0: 开发环境搭建

## 基础环境

你的 MacOS / Linux / Windows 电脑

## 准备开发环境

### MacOS

+ 对于 intel 芯片的 MacOS 电脑，使用 VMware Fusion 安装 Ubuntu 20.04 虚拟机
+ 对于 Apple 芯片的 MacOS 电脑，使用 [UTM](https://mac.getutm.app/) 安装 Ubuntu 20.04 虚拟机

### Linux

如果是 ubuntu 系统，则直接使用 ubuntu 系统，20/22/24应该都可以，否则使用 virtualbox 安装 Ubuntu 20.04 虚拟机，当然其他发型版也可以，不过需要自己折腾

### Windows

安装 VMware Workstation，然后安装 Ubuntu 20.04 虚拟机

## 安装开发工具

> 由于是开发环境，建议都直接使用 root 用户操作

> 如果不熟悉脚本，没关系，先操作，再慢慢分析

+ Cursor IDE 或者其他你喜欢的 IDE

+ 编译环境安装脚本，参考 `scripts/cross-compiler-install.sh`

## 创建项目结构

### 1. 初始化 Git 仓库（如果还没有）

```bash
cd /root/CastorOS
git init
```

### 2. 创建目录结构

```bash
# 在项目根目录执行
mkdir -p src/{boot,kernel,drivers,mm,fs,lib,include}
mkdir -p src/include/{drivers,kernel,mm,fs,lib}
mkdir -p build
mkdir -p scripts

# 给这些目录都创建一个 .gitkeep
touch src/boot/.gitkeep
touch src/kernel/.gitkeep
touch src/drivers/.gitkeep
touch src/mm/.gitkeep
touch src/fs/.gitkeep
touch src/lib/.gitkeep
touch src/include/drivers/.gitkeep
touch src/include/kernel/.gitkeep
touch src/include/mm/.gitkeep
touch src/include/fs/.gitkeep
touch src/include/lib/.gitkeep
touch scripts/.gitkeep
```

**目录说明**:
- `src/boot/`: 引导相关代码（汇编）
- `src/kernel/`: 内核核心代码（GDT、IDT、中断处理等）
- `src/drivers/`: 设备驱动（VGA、键盘、定时器等）
- `src/mm/`: 内存管理（物理内存、虚拟内存、堆）
- `src/fs/`: 文件系统
- `src/lib/`: 标准库函数实现
- `src/include/`: 头文件（采用层级式结构，便于管理和维护）
  - `src/include/drivers/`: 驱动程序头文件（如 vga.h, keyboard.h 等）
  - `src/include/kernel/`: 内核核心头文件（如 gdt.h, idt.h 等）
  - `src/include/mm/`: 内存管理头文件（如 pmm.h, vmm.h 等）
  - `src/include/fs/`: 文件系统头文件（如 vfs.h, initrd.h 等）
  - `src/include/lib/`: 库函数头文件（如 string.h, stdio.h 等）
- `build/`: 编译输出目录
- `scripts/`: 工具脚本（环境安装、检查等）

### 3. 创建 .gitignore

```bash
cat > .gitignore << 'EOF'
# 构建输出
build/
*.bin
*.iso
*.o
*.elf

# 编辑器和 IDE
.idea/
*.swp
*.swo
*~
compile_commands.json
.clangd/

# 调试文件
*.log

# OS 特定文件
.DS_Store
Thumbs.db
EOF
```

### 配置 VSCode

```bash
# 安装 clangd（用于代码补全和分析）
sudo apt install -y clangd

# 安装 bear（用于生成 compile_commands.json）
sudo apt install -y bear
```

在 VSCode 中安装以下插件：
- **C/C++** (Microsoft) - 提供 C/C++ 调试支持
- **clangd** - 提供更好的代码补全和语法检查
- **LinkerScript** (ZixuanWang) - 提供 LinkerScript 语法高亮和补全
