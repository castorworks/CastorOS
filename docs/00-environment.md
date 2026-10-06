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

+ 编译环境：macOS 用 Homebrew，Ubuntu/Debian 用 `scripts/cross-compiler-install.sh`（见下文）

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

### 方法二：从源码编译 (Ubuntu/Debian)

发行版的仓库里没有这三个裸机目标的 GCC，要自己编译。项目里的脚本把三个都装好，
同时用 apt 装上 NASM 和 QEMU：

```bash
bash scripts/cross-compiler-install.sh          # 三个架构都装
bash scripts/cross-compiler-install.sh i686-elf # 只装一个（i686-elf / x86_64-elf / aarch64-elf）
bash scripts/cross-compiler-install.sh -y       # 不提问，已经装好的跳过
```

它编译的是 binutils 2.45.1 和 GCC 15.2.0（只要 C/C++ 编译器和 libgcc，不要 C 库），和
macOS 上 Homebrew 装的版本一样。内核是用 `-std=gnu++20` 编译的，所以 GCC 至少要 10；
更早的版本（比如 Ubuntu 20.04 自带年代的 9.x）不认识这个选项。

- 默认装到 `/usr/local/cross`，并把 `/usr/local/cross/bin` 加进 `~/.bashrc` 的 `PATH`；
  用 `PREFIX=...` 换地方。
- 源码默认从中科大的镜像下载；在国外用 `GNU_MIRROR=https://ftpmirror.gnu.org/gnu`。
- 每个目标要编译 15-40 分钟，三个加起来临时占 5GB 左右（`~/cross-compiler`，装完可以删）。

脚本做的事就是对每个目标重复这两步，想手动来可以照着做：

```bash
export PREFIX=/usr/local/cross TARGET=i686-elf      # 或 x86_64-elf、aarch64-elf
export PATH="$PREFIX/bin:$PATH"

mkdir build-binutils && cd build-binutils
../binutils-2.45.1/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
make -j$(nproc) && sudo make install
cd ..

mkdir build-gcc && cd build-gcc
../gcc-15.2.0/configure --target=$TARGET --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers
make -j$(nproc) all-gcc all-target-libgcc
sudo make install-gcc install-target-libgcc
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
make test-all           # 三个架构各跑一遍内核测试、用户态自检和命令行检查
make lib-test           # 用户库的宿主机测试：几秒钟，不需要交叉编译器和 QEMU
```

### 持续集成

每次推送到 `main`、每个 PR，GitHub Actions 都会对三个架构各跑一遍 `make test ARCH=<arch>`
（`.github/workflows/test.yml`），另外还有一个只跑 `make lib-test` 的任务。它用 macOS 的 runner 和上面“方法一”里的 Homebrew 包，
所以和在 macOS 上本地开发是同一套工具。三个架构各是一个独立的任务，一个失败不影响另外两个跑完。
每次运行的日志（`test.log` 是 QEMU 的全部输出，`shell-test.log` 是命令行检查逐项的结果）作为
artifact 上传，失败时先下载它们来看。runner 比开发机慢，所以那里把等待的上限放宽了
（`TEST_TIMEOUT=600`、`STEP_TIMEOUT=120`）；它们只是上限，不会让通过的运行变慢。

不需要安装 GRUB。

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
