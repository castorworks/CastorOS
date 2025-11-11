#!/bin/bash

# CastorOS 交叉编译器安装脚本
# 用于 Ubuntu 20.04
# 安装 i686-elf-gcc 和 i686-elf-binutils

set -e  # 遇到错误立即退出

echo "=== Installing i686-elf Cross Compiler ==="
echo ""

# 配置
export PREFIX="/usr/local/cross"
export TARGET=i686-elf
export BINUTILS_VERSION=2.34
export GCC_VERSION=9.3.0
export PATH="$PREFIX/bin:$PATH"

# 显示配置信息
echo "Configuration:"
echo "  Install directory: $PREFIX"
echo "  Target architecture: $TARGET"
echo "  Binutils: $BINUTILS_VERSION"
echo "  GCC:      $GCC_VERSION"
echo ""

# 检查是否已安装
if command -v i686-elf-gcc &> /dev/null; then
    echo "Detected i686-elf-gcc is already installed:"
    i686-elf-gcc --version | head -n 1
    read -p "Do you want to continue reinstalling? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Installation cancelled"
        exit 0
    fi
fi

# 安装依赖
echo ">>> [1/5] Installing dependencies..."
sudo apt update
sudo apt install -y build-essential bison flex libgmp3-dev libmpc-dev \
                    libmpfr-dev texinfo libisl-dev nasm wget

# 创建工作目录
echo ""
echo ">>> [2/5] Downloading source code..."
mkdir -p ~/cross-compiler
cd ~/cross-compiler

# 下载 Binutils
if [ ! -f "binutils-${BINUTILS_VERSION}.tar.gz" ]; then
    echo "Downloading Binutils ${BINUTILS_VERSION}..."
    wget -c https://mirrors.ustc.edu.cn/gnu/binutils/binutils-${BINUTILS_VERSION}.tar.gz
else
    echo "Binutils source already exists, skipping download"
fi

if [ ! -d "binutils-${BINUTILS_VERSION}" ]; then
    echo "Extracting Binutils..."
    tar -xzf binutils-${BINUTILS_VERSION}.tar.gz
fi

# 下载 GCC
if [ ! -f "gcc-${GCC_VERSION}.tar.gz" ]; then
    echo "Downloading GCC ${GCC_VERSION} (large file, about 150MB)..."
    wget -c https://mirrors.ustc.edu.cn/gnu/gcc/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.gz
else
    echo "GCC source already exists, skipping download"
fi

if [ ! -d "gcc-${GCC_VERSION}" ]; then
    echo "Extracting GCC..."
    tar -xzf gcc-${GCC_VERSION}.tar.gz
fi

# 编译 Binutils
echo ""
echo ">>> [3/5] Compiling Binutils (estimated 5-10 minutes)..."
rm -rf build-binutils
mkdir -p build-binutils
cd build-binutils

../binutils-${BINUTILS_VERSION}/configure \
    --target=$TARGET \
    --prefix="$PREFIX" \
    --with-sysroot \
    --disable-nls \
    --disable-werror

make -j$(nproc)
sudo make install
cd ..

echo "Binutils installation complete!"

# 编译 GCC
echo ""
echo ">>> [4/5] Compiling GCC (estimated 20-50 minutes, please be patient)..."
rm -rf build-gcc
mkdir -p build-gcc
cd build-gcc

../gcc-${GCC_VERSION}/configure \
    --target=$TARGET \
    --prefix="$PREFIX" \
    --disable-nls \
    --enable-languages=c,c++ \
    --without-headers

echo "Starting GCC compilation..."
make -j$(nproc) all-gcc
make -j$(nproc) all-target-libgcc
sudo make install-gcc
sudo make install-target-libgcc
cd ~

echo "GCC installation complete!"

# 配置环境变量
echo ""
echo ">>> [5/5] Configuring environment variables..."
if ! grep -q "/usr/local/cross/bin" ~/.bashrc; then
    echo 'export PATH="/usr/local/cross/bin:$PATH"' >> ~/.bashrc
    echo "Added to ~/.bashrc"
else
    echo "Environment variable already exists, skipping"
fi

# 验证安装
echo ""
echo ">>> Verifying installation..."
export PATH="$PREFIX/bin:$PATH"

if command -v i686-elf-gcc &> /dev/null; then
    echo "[OK] i686-elf-gcc installed successfully!"
    $PREFIX/bin/i686-elf-gcc --version | head -n 1
else
    echo "[FAIL] Installation failed, please check error messages"
    exit 1
fi

if command -v i686-elf-ld &> /dev/null; then
    echo "[OK] i686-elf-ld installed successfully!"
    $PREFIX/bin/i686-elf-ld --version | head -n 1
else
    echo "[FAIL] Installation failed, please check error messages"
    exit 1
fi

if command -v nasm &> /dev/null; then
    echo "[OK] nasm installed successfully!"
    nasm -v
else
    echo "[FAIL] nasm installation failed"
    exit 1
fi

# 完成
echo ""
echo "=============================================="
echo "=== Installation Complete! ==="
echo "=============================================="
echo ""
echo "Please run the following command to activate environment variables:"
echo "    source ~/.bashrc"
echo ""
echo "Or restart your terminal"
echo ""
echo "Optional: Delete source directory to save about 2-3 GB of space:"
echo "    rm -rf ~/cross-compiler"
echo ""
echo "Getting started:"
echo "    i686-elf-gcc --version"
echo "    i686-elf-ld --version"
echo ""
