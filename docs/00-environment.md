# 阶段 0: 开发环境搭建

## 基础环境

你的 MacOS / Linux / Windows 电脑

## 准备开发环境

### MacOS

+ 对于 intel 芯片的 MacOS 电脑，使用 VMware Fusion 安装 Ubuntu 20.04 虚拟机
+ 对于 Apple 芯片的 MacOS 电脑，使用 [UTM](https://mac.getutm.app/) 安装 Ubuntu 20.04 虚拟机
+ 或者直接在 macOS 上使用 Homebrew 安装交叉编译器（见下文）

### Linux

如果是 ubuntu 系统，则直接使用 ubuntu 系统，20/22/24应该都可以，否则使用 virtualbox 安装 Ubuntu 20.04 虚拟机，当然其他发型版也可以，不过需要自己折腾

### Windows

安装 VMware Workstation，然后安装 Ubuntu 20.04 虚拟机

## 安装开发工具

> 由于是开发环境，建议都直接使用 root 用户操作

> 如果不熟悉脚本，没关系，先操作，再慢慢分析

+ Cursor IDE 或者其他你喜欢的 IDE

+ 编译环境安装脚本，参考 `scripts/cross-compiler-install.sh`

## 多架构交叉编译器安装

CastorOS 支持三种 CPU 架构，每种架构需要对应的交叉编译器：

| 架构 | 工具链前缀 | 汇编器 |
|------|-----------|--------|
| i686 | `i686-elf-` | NASM |
| x86_64 | `x86_64-elf-` | NASM |
| arm64 | `aarch64-elf-` | GNU as |

### 方法一：使用 Homebrew (macOS)

macOS 用户可以直接使用 Homebrew 安装交叉编译器：

```bash
# 安装 i686 交叉编译器
brew install i686-elf-gcc i686-elf-binutils

# 安装 x86_64 交叉编译器
brew install x86_64-elf-gcc x86_64-elf-binutils

# 安装 ARM64 交叉编译器
brew install aarch64-elf-gcc aarch64-elf-binutils

# 安装 NASM (x86 汇编器)
brew install nasm

# 安装 QEMU (模拟器)
brew install qemu

# 验证安装
i686-elf-gcc --version
x86_64-elf-gcc --version
aarch64-elf-gcc --version
nasm -v
```

### 方法二：从源码编译 (Ubuntu/Linux)

#### i686 交叉编译器

运行项目提供的安装脚本：

```bash
bash scripts/cross-compiler-install.sh
```

或手动安装：

```bash
# 安装依赖
sudo apt update
sudo apt install -y build-essential bison flex libgmp3-dev libmpc-dev \
                    libmpfr-dev texinfo libisl-dev nasm wget

# 配置
export PREFIX="/usr/local/cross"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"

# 下载并编译 binutils
wget https://mirrors.ustc.edu.cn/gnu/binutils/binutils-2.34.tar.gz
tar -xzf binutils-2.34.tar.gz
mkdir build-binutils && cd build-binutils
../binutils-2.34/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
make -j$(nproc)
sudo make install
cd ..

# 下载并编译 GCC
wget https://mirrors.ustc.edu.cn/gnu/gcc/gcc-9.3.0/gcc-9.3.0.tar.gz
tar -xzf gcc-9.3.0.tar.gz
mkdir build-gcc && cd build-gcc
../gcc-9.3.0/configure --target=$TARGET --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers
make -j$(nproc) all-gcc all-target-libgcc
sudo make install-gcc install-target-libgcc

# 添加到 PATH
echo 'export PATH="/usr/local/cross/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

#### x86_64 交叉编译器

```bash
export PREFIX="/usr/local/cross"
export TARGET=x86_64-elf
export PATH="$PREFIX/bin:$PATH"

# 编译 binutils
mkdir build-binutils-x64 && cd build-binutils-x64
../binutils-2.34/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
make -j$(nproc)
sudo make install
cd ..

# 编译 GCC
mkdir build-gcc-x64 && cd build-gcc-x64
../gcc-9.3.0/configure --target=$TARGET --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers
make -j$(nproc) all-gcc all-target-libgcc
sudo make install-gcc install-target-libgcc
```

#### ARM64 交叉编译器

```bash
export PREFIX="/usr/local/cross"
export TARGET=aarch64-elf
export PATH="$PREFIX/bin:$PATH"

# 编译 binutils
mkdir build-binutils-arm64 && cd build-binutils-arm64
../binutils-2.34/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
make -j$(nproc)
sudo make install
cd ..

# 编译 GCC
mkdir build-gcc-arm64 && cd build-gcc-arm64
../gcc-9.3.0/configure --target=$TARGET --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers
make -j$(nproc) all-gcc all-target-libgcc
sudo make install-gcc install-target-libgcc
```

### 安装 QEMU 模拟器

```bash
# Ubuntu/Debian
sudo apt install -y qemu-system-i386 qemu-system-x86 qemu-system-arm

# macOS
brew install qemu
```

### 验证安装

```bash
# 验证 i686 工具链
i686-elf-gcc --version
i686-elf-ld --version

# 验证 x86_64 工具链
x86_64-elf-gcc --version
x86_64-elf-ld --version

# 验证 ARM64 工具链
aarch64-elf-gcc --version
aarch64-elf-ld --version

# 验证 NASM
nasm -v

# 验证 QEMU
qemu-system-i386 --version
qemu-system-x86_64 --version
qemu-system-aarch64 --version
```

### 快速测试

```bash
# 测试 i686 构建
make ARCH=i686 clean all
make ARCH=i686 run-silent

# 测试 x86_64 构建
make ARCH=x86_64 clean all

# 测试 ARM64 构建
make ARCH=arm64 clean all
```

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
