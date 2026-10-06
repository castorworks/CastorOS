#!/bin/bash

# CastorOS 交叉编译器安装脚本（Ubuntu / Debian）
#
# 从源码编译三个架构的裸机工具链（binutils + gcc/g++），并用 apt 装上 NASM 和 QEMU：
#   i686-elf-      x86_64-elf-      aarch64-elf-
# 内核是 C++20 写的（-std=gnu++20），需要 GCC 10 以上；这里用和 macOS 上 Homebrew
# 一样的版本，两边编出来的东西一致。macOS 上不用这个脚本，见 docs/00-environment.md。
#
# 用法:
#   scripts/cross-compiler-install.sh [-y] [-f] [目标...]
#     目标   i686-elf、x86_64-elf、aarch64-elf 里的一个或几个；不给就是全部三个
#     -y     不提问（已经装好的目标直接跳过）
#     -f     已经装好的目标也重新编译
#
# 环境变量:
#   PREFIX       安装到哪里（默认 /usr/local/cross；当前用户写不了时用 sudo 安装）
#   GNU_MIRROR   从哪里下载源码（默认 https://mirrors.ustc.edu.cn/gnu，
#                在国外可以换成 https://ftpmirror.gnu.org/gnu）
#   WORKDIR      源码和编译目录（默认 ~/cross-compiler，装完可以删）
#   JOBS         并行编译的任务数（默认是 CPU 的个数）
#
# 每个目标大约要编译 15-40 分钟，三个加起来占 5GB 左右的临时空间。

set -e

BINUTILS_VERSION=2.45.1
GCC_VERSION=15.2.0
ALL_TARGETS="i686-elf x86_64-elf aarch64-elf"

PREFIX="${PREFIX:-/usr/local/cross}"
GNU_MIRROR="${GNU_MIRROR:-https://mirrors.ustc.edu.cn/gnu}"
WORKDIR="${WORKDIR:-$HOME/cross-compiler}"
JOBS="${JOBS:-$(nproc)}"

ASSUME_YES=0
FORCE=0
TARGETS=""
for arg in "$@"; do
    case "$arg" in
        -y) ASSUME_YES=1 ;;
        -f) FORCE=1 ;;
        -h|--help) sed -n '3,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        i686-elf|x86_64-elf|aarch64-elf) TARGETS="$TARGETS $arg" ;;
        *) echo "Unknown argument: $arg (targets: $ALL_TARGETS)" >&2; exit 1 ;;
    esac
done
TARGETS="${TARGETS:-$ALL_TARGETS}"

# 安装目录当前用户写不了就用 sudo
SUDO=""
if ! mkdir -p "$PREFIX" 2>/dev/null || [ ! -w "$PREFIX" ]; then
    SUDO="sudo"
    sudo mkdir -p "$PREFIX"
fi
export PATH="$PREFIX/bin:$PATH"

echo "=== Installing the CastorOS cross compilers ==="
echo "  Targets:   $TARGETS"
echo "  Prefix:    $PREFIX"
echo "  Binutils:  $BINUTILS_VERSION"
echo "  GCC:       $GCC_VERSION"
echo "  Mirror:    $GNU_MIRROR"
echo ""

# 这个目标是不是已经装好了要的版本
installed() {
    command -v "$1-g++" > /dev/null 2>&1 && "$1-g++" -dumpfullversion 2>/dev/null | grep -qx "$GCC_VERSION"
}

# 决定哪些目标要编译
TODO=""
for target in $TARGETS; do
    if installed "$target" && [ "$FORCE" = 0 ]; then
        if [ "$ASSUME_YES" = 1 ]; then
            echo "$target-g++ $GCC_VERSION is already installed, skipping"
            continue
        fi
        read -p "$target-g++ $GCC_VERSION is already installed. Rebuild it? (y/N): " -n 1 -r
        echo
        [[ $REPLY =~ ^[Yy]$ ]] || continue
    fi
    TODO="$TODO $target"
done

echo ""
echo ">>> Installing packages (build dependencies, NASM, QEMU)..."
sudo apt-get update
sudo apt-get install -y build-essential bison flex libgmp3-dev libmpc-dev \
                        libmpfr-dev texinfo libisl-dev wget xz-utils \
                        nasm qemu-system-x86 qemu-system-arm

if [ -n "$TODO" ]; then
    echo ""
    echo ">>> Downloading the sources..."
    mkdir -p "$WORKDIR"
    cd "$WORKDIR"
    fetch() {       # fetch <目录名> <URL>：下载并解开，已经有了就跳过
        local dir=$1 url=$2 file
        file=$(basename "$url")
        [ -d "$dir" ] && return 0
        wget -c "$url"
        tar -xf "$file"
    }
    fetch "binutils-$BINUTILS_VERSION" "$GNU_MIRROR/binutils/binutils-$BINUTILS_VERSION.tar.xz"
    fetch "gcc-$GCC_VERSION" "$GNU_MIRROR/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.xz"
fi

for target in $TODO; do
    echo ""
    echo ">>> [$target] Building binutils..."
    cd "$WORKDIR"
    rm -rf "build-binutils-$target"
    mkdir "build-binutils-$target"
    cd "build-binutils-$target"
    "../binutils-$BINUTILS_VERSION/configure" --target="$target" --prefix="$PREFIX" \
        --with-sysroot --disable-nls --disable-werror
    make -j"$JOBS"
    $SUDO make install

    # 只要编译器本身和 libgcc：内核和用户程序都是 freestanding 的，不用 C 库和 libstdc++
    echo ""
    echo ">>> [$target] Building GCC (the slow part)..."
    cd "$WORKDIR"
    rm -rf "build-gcc-$target"
    mkdir "build-gcc-$target"
    cd "build-gcc-$target"
    "../gcc-$GCC_VERSION/configure" --target="$target" --prefix="$PREFIX" \
        --disable-nls --enable-languages=c,c++ --without-headers
    make -j"$JOBS" all-gcc
    make -j"$JOBS" all-target-libgcc
    $SUDO make install-gcc
    $SUDO make install-target-libgcc
done

# 让以后的终端也找得到
if [ -f ~/.bashrc ] && ! grep -qF "$PREFIX/bin" ~/.bashrc; then
    echo "export PATH=\"$PREFIX/bin:\$PATH\"" >> ~/.bashrc
    echo ""
    echo "Added $PREFIX/bin to PATH in ~/.bashrc"
fi

echo ""
echo ">>> Verifying..."
failed=0
check() {           # check <命令> [参数...]：能运行就打印它的版本
    if command -v "$1" > /dev/null 2>&1; then
        echo "[OK]   $("$@" 2>&1 | head -n 1)"
    else
        echo "[FAIL] $1 not found"
        failed=1
    fi
}
for target in $TARGETS; do
    check "$target-g++" --version
    check "$target-ld" --version
done
check nasm -v
check qemu-system-i386 --version
check qemu-system-x86_64 --version
check qemu-system-aarch64 --version
[ "$failed" = 0 ] || exit 1

echo ""
echo "=== Done ==="
echo "Open a new terminal (or run: source ~/.bashrc), then in the CastorOS directory:"
echo "    make build-all      # build all three architectures"
echo "    make test-all       # boot each one in QEMU and run the tests"
echo ""
echo "The sources and build directories in $WORKDIR are no longer needed:"
echo "    rm -rf $WORKDIR"
