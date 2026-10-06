# 开发环境搭建

构建和运行 CastorOS 需要三样东西：每个目标架构的交叉编译器、x86 的汇编器 NASM、模拟器 QEMU。不需要安装 GRUB，也不需要制作磁盘映像：内核用 QEMU 的 `-kernel` 直接启动。

| 架构 | 工具链前缀 | 汇编器 | QEMU |
|------|-----------|--------|------|
| i686 | `i686-elf-` | NASM | `qemu-system-i386` |
| x86_64 | `x86_64-elf-` | NASM | `qemu-system-x86_64` |
| arm64 | `aarch64-elf-` | GNU as | `qemu-system-aarch64` |

内核是用 `-std=gnu++20` 编译的，所以 GCC 至少要 10；更早的版本（比如 Ubuntu 20.04 自带年代的 9.x）不认识这个选项。

## 选择系统

- **macOS**：直接用 Homebrew 安装（方法一）。持续集成用的就是这一套。
- **Ubuntu / Debian**：用项目里的脚本从源码编译（方法二）。Ubuntu 20/22/24 应该都可以；其他发行版也可以，不过需要自己折腾。
- **Windows**：安装 VMware Workstation 或 VirtualBox，在 Ubuntu 虚拟机里按方法二来。

## 方法一：Homebrew（macOS）

```bash
brew install i686-elf-gcc i686-elf-binutils
brew install x86_64-elf-gcc x86_64-elf-binutils
brew install aarch64-elf-gcc aarch64-elf-binutils
brew install nasm qemu
```

## 方法二：从源码编译（Ubuntu / Debian）

发行版的仓库里没有这三个裸机目标的 GCC，要自己编译。项目里的脚本把三个都装好，同时用 apt 装上 NASM 和 QEMU：

```bash
bash scripts/cross-compiler-install.sh          # 三个架构都装
bash scripts/cross-compiler-install.sh i686-elf # 只装一个（i686-elf / x86_64-elf / aarch64-elf）
bash scripts/cross-compiler-install.sh -y       # 不提问，已经装好的跳过
```

它编译的是 binutils 2.45.1 和 GCC 15.2.0（只要 C/C++ 编译器和 libgcc，不要 C 库），和 macOS 上 Homebrew 装的版本一样。

- 默认装到 `/usr/local/cross`，并把 `/usr/local/cross/bin` 加进 `~/.bashrc` 的 `PATH`；用 `PREFIX=...` 换地方。
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

## 验证安装

```bash
i686-elf-gcc --version     && i686-elf-ld --version
x86_64-elf-gcc --version   && x86_64-elf-ld --version
aarch64-elf-gcc --version  && aarch64-elf-ld --version
nasm -v

qemu-system-i386 --version
qemu-system-x86_64 --version
qemu-system-aarch64 --version
```

然后在项目里试一遍：

```bash
make build-all          # 构建三个架构
make run                # 在 QEMU 里运行 i686，串口控制台接到当前终端
make test-all           # 三个架构各跑一遍内核测试、用户态自检和命令行检查
make lib-test           # 用户库的宿主机测试：几秒钟，不需要交叉编译器和 QEMU
```

各个命令做了什么、失败时去哪里看，见 [测试与调试](testing.md)。

## 编辑器

任何编辑器都可以。要代码补全和跳转，用 clangd 加上 `compile_commands.json`：

```bash
sudo apt install -y clangd  # Ubuntu；macOS 的 Xcode 命令行工具里自带
pip3 install compiledb      # make compile-db 用它生成 compile_commands.json
make compile-db
```

VSCode（以及基于它的编辑器）里有用的插件：

- **clangd**：代码补全和语法检查
- **C/C++**（Microsoft）：调试支持
- **LinkerScript**（ZixuanWang）：链接脚本的语法高亮

## 常见问题

- **交叉编译器未找到**：确认 `i686-elf-g++` 等在 `PATH` 里。方法二装完之后要重新打开终端，或者 `source ~/.bashrc`。
- **QEMU 未找到**：macOS 上 `brew install qemu`；Ubuntu 上方法二的脚本已经装了。
- **手动运行 QEMU 时找不到 `timeout`**：macOS 上 `brew install coreutils`。`make test` 本身不需要它。
