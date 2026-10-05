# 开发环境搭建

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
make build-all          # 构建三个架构
make run                # 在 QEMU 里运行 i686，串口控制台接到当前终端
make test-all           # 三个架构各跑一遍内核测试和用户态自检
```

`make test` 需要 `timeout` 命令（macOS: `brew install coreutils`）。不需要安装 GRUB。

## 目录结构

**目录说明**:
- `src/arch/`: 架构相关代码（引导、GDT/IDT 或异常向量、中断、分页、上下文切换）
- `src/kernel/`: 内核核心代码（任务调度、系统调用、IPC、ELF 加载等）
- `src/drivers/`: 内核里仅有的两个驱动（调试输出用的串口、调度用的时钟）
- `src/mm/`: 内存管理（物理内存、虚拟内存、堆）
- `src/lib/`: 内核库函数实现
- `src/include/`: 头文件（按子系统分层）
- `user/`: 用户态的库、驱动、服务和程序
- `build/`: 编译输出目录
- `scripts/`: 工具脚本（交叉编译器安装）

### 配置 VSCode

```bash
# 安装 clangd（用于代码补全和分析）
sudo apt install -y clangd

# 安装 compiledb（make compile-db 用它生成 compile_commands.json）
pip3 install compiledb
```

在 VSCode 中安装以下插件：
- **C/C++** (Microsoft) - 提供 C/C++ 调试支持
- **clangd** - 提供更好的代码补全和语法检查
- **LinkerScript** (ZixuanWang) - 提供 LinkerScript 语法高亮和补全
