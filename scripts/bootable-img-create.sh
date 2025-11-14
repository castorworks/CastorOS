#!/bin/bash
# ============================================================================
# bootable-img-create.sh - 创建可启动的磁盘镜像
# ============================================================================
#
# 此脚本创建一个包含完整系统和 GRUB 的磁盘镜像，
# 可以直接使用 dd 命令写入物理硬盘或 U 盘。
#
# 用法: sudo ./bootable-img-create.sh [大小MB]
# 示例: sudo ./bootable-img-create.sh 128

set -e

# 颜色定义
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# 默认参数
DISK_IMG="$PROJECT_ROOT/build/bootable.img"
DISK_SIZE_MB=${1:-128}
MOUNT_POINT="/tmp/castor_bootable"
KERNEL_BIN="$PROJECT_ROOT/build/castor.bin"
GRUB_CFG="$PROJECT_ROOT/grub.cfg"

SHELL_ELF="$PROJECT_ROOT/userland/shell/shell.elf"
HELLO_ELF="$PROJECT_ROOT/userland/helloworld/hello.elf"

# ============================================================================
# 函数定义
# ============================================================================

info() {
    echo -e "${GREEN}$1${NC}"
}

warning() {
    echo -e "${YELLOW}$1${NC}"
}

error() {
    echo "$1" >&2
    exit 1
}

check_root() {
    if [ "$EUID" -ne 0 ]; then
        error "This script requires root privileges, please run with sudo"
    fi
}

check_files() {
    if [ ! -f "$KERNEL_BIN" ]; then
        error "Kernel file not found: $KERNEL_BIN\nPlease run 'make' to compile the kernel first"
    fi
    
    if [ ! -f "$GRUB_CFG" ]; then
        error "GRUB config file not found: $GRUB_CFG"
    fi
}

check_tools() {
    local missing_tools=()
    
    for tool in parted mkfs.vfat grub-install losetup; do
        if ! command -v $tool &> /dev/null; then
            missing_tools+=($tool)
        fi
    done
    
    if [ ${#missing_tools[@]} -ne 0 ]; then
        error "Missing required tools: ${missing_tools[*]}\nPlease install: sudo apt install parted grub2-common dosfstools"
    fi
}

create_image() {
    info "[1/8] Creating disk image (${DISK_SIZE_MB}MB)..."
    
    mkdir -p "$(dirname "$DISK_IMG")"
    dd if=/dev/zero of="$DISK_IMG" bs=1M count="$DISK_SIZE_MB" status=progress
    
    info "  [OK] Image file created: $DISK_IMG"
}

create_partitions() {
    info "[2/8] Creating partition table..."
    
    # 创建 GPT 分区表
    parted -s "$DISK_IMG" mklabel gpt
    
    # 创建 BIOS 启动分区 (1MB)
    parted -s "$DISK_IMG" mkpart primary 1MiB 2MiB
    parted -s "$DISK_IMG" set 1 bios_grub on
    
    # 创建根分区
    parted -s "$DISK_IMG" mkpart primary fat32 2MiB 100%
    
    info "  [OK] Partition table created"
    parted -s "$DISK_IMG" print
}

setup_loop_device() {
    info "[3/8] Setting up loop device..."
    
    LOOP_DEV=$(losetup -f --show -P "$DISK_IMG")
    
    # 等待分区设备节点创建
    sleep 1
    
    # 检查分区是否可用
    if [ ! -b "${LOOP_DEV}p2" ]; then
        # 手动触发分区扫描
        partprobe "$LOOP_DEV" 2>/dev/null || true
        sleep 1
    fi
    
    info "  [OK] Loop device: $LOOP_DEV"
}

format_partition() {
    info "[4/8] Formatting root partition..."
    
    mkfs.vfat -F 32 -n "CastorOS" "${LOOP_DEV}p2"
    
    info "  [OK] Root partition formatted as FAT32"
}

mount_partition() {
    info "[5/8] Mounting partition..."
    
    mkdir -p "$MOUNT_POINT"
    mount "${LOOP_DEV}p2" "$MOUNT_POINT"
    
    info "  [OK] Mounted at $MOUNT_POINT"
}

install_files() {
    info "[6/8] Installing system files..."
    
    # 创建目录结构
    mkdir -p "$MOUNT_POINT/boot/grub"
    mkdir -p "$MOUNT_POINT/bin"
    
    # 复制内核
    cp "$KERNEL_BIN" "$MOUNT_POINT/boot/castor.bin"
    
    # 复制 GRUB 配置
    cp "$GRUB_CFG" "$MOUNT_POINT/boot/grub/grub.cfg"

    # 复制 shell 程序
    if [ -f "$SHELL_ELF" ]; then
        cp "$SHELL_ELF" "$MOUNT_POINT/bin/shell.elf"
    else
        warning "Shell program not found: $SHELL_ELF"
    fi

    # 复制 hello 程序
    if [ -f "$HELLO_ELF" ]; then
        cp "$HELLO_ELF" "$MOUNT_POINT/bin/hello.elf"
    else
        warning "Hello world program not found: $HELLO_ELF"
    fi
    
    # 同步到磁盘
    sync
    
    info "  [OK] System files installed"
}

install_grub() {
    info "[7/8] Installing GRUB..."
    
    grub-install --target=i386-pc --boot-directory="$MOUNT_POINT/boot" "$LOOP_DEV"
    
    info "  [OK] GRUB installed"
}

cleanup() {
    info "[8/8] Cleaning up..."
    
    # 卸载分区
    umount "$MOUNT_POINT" 2>/dev/null || true
    rmdir "$MOUNT_POINT" 2>/dev/null || true
    
    # 释放 loop 设备
    losetup -d "$LOOP_DEV" 2>/dev/null || true
    
    info "  [OK] Cleanup complete"
}

show_success() {
    local img_size=$(ls -lh "$DISK_IMG" | awk '{print $5}')
    
    echo ""
    echo "======================================"
    info "[OK] Bootable disk image created successfully!"
    echo "======================================"
    echo ""
    echo "Image info:"
    echo "  File: $DISK_IMG"
    echo "  Size: $img_size"
    echo "  Format: GPT + FAT32"
    echo "  Bootloader: GRUB"
    echo ""
    echo "Included files:"
    echo "  - /boot/castor.bin (kernel)"
    echo "  - /boot/grub/ (bootloader)"
    if [ -f "$SHELL_ELF" ]; then
        echo "  - /bin/shell.elf (shell program)"
    fi
    if [ -f "$HELLO_ELF" ]; then
        echo "  - /bin/hello.elf (hello world program)"
    fi
    echo ""
    info "Usage:"
    echo ""
    echo "1. Write to physical hard drive:"
    echo "   sudo dd if=$DISK_IMG of=/dev/sdX bs=4M status=progress conv=fsync"
    echo ""
    echo "2. Write to USB drive:"
    echo "   sudo dd if=$DISK_IMG of=/dev/sdX bs=4M status=progress conv=fsync"
    echo "   (Replace /dev/sdX with actual device name)"
    echo ""
    echo "3. Test in QEMU:"
    echo "   qemu-system-i386 -hda $DISK_IMG"
    echo ""
    warning "Warning: dd command will completely overwrite the target device. Make sure to select the correct device!"
    echo "======================================"
}

# ============================================================================
# 主程序
# ============================================================================

main() {
    echo ""
    echo "======================================"
    echo "  CastorOS Bootable Image Creator"
    echo "======================================"
    echo ""
    
    # 检查 root 权限
    check_root
    
    # 检查工具
    check_tools
    
    # 检查文件
    check_files
    
    # 创建镜像
    create_image
    
    # 创建分区
    create_partitions
    
    # 设置 loop 设备
    setup_loop_device
    
    # 格式化分区
    format_partition
    
    # 挂载分区
    mount_partition
    
    # 安装文件
    install_files
    
    # 安装 GRUB
    install_grub
    
    # 清理
    cleanup
    
    # 显示成功信息
    show_success
}

# 错误处理
trap cleanup EXIT

# 运行主程序
main "$@"

