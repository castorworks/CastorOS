#!/bin/bash
# ============================================================================
# bootable-img-umount.sh - 卸载磁盘镜像
# ============================================================================

set -e

# 颜色定义
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# 默认参数
MOUNT_POINT="${1:-$PROJECT_ROOT/mnt/bootable}"

info() {
    echo -e "${GREEN}$1${NC}"
}

warning() {
    echo -e "${YELLOW}$1${NC}"
}

error() {
    echo -e "${RED}$1${NC}" >&2
    exit 1
}

# 检查 root 权限
if [ "$EUID" -ne 0 ]; then
    error "This script requires root privileges. Please run with sudo."
fi

info "=========================================="
info "  CastorOS Disk Image Unmounter"
info "=========================================="
echo ""

# 检查挂载点是否存在
if [ ! -d "$MOUNT_POINT" ]; then
    warning "Mount point does not exist: $MOUNT_POINT"
    exit 0
fi

# 检查是否已挂载
if ! mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
    info "Mount point is not mounted: $MOUNT_POINT"
    
    # 尝试删除空目录
    if [ -d "$MOUNT_POINT" ]; then
        rmdir "$MOUNT_POINT" 2>/dev/null && info "Removed empty directory: $MOUNT_POINT" || true
    fi
    
    # 清理可能残留的 loop 设备
    info "Checking for orphaned loop devices..."
    for loop in $(losetup -j "$PROJECT_ROOT/build/bootable.img" 2>/dev/null | cut -d: -f1); do
        if [ -n "$loop" ]; then
            warning "Found orphaned loop device: $loop"
            losetup -d "$loop" 2>/dev/null && info "  Detached: $loop" || true
        fi
    done
    
    exit 0
fi

# 获取挂载的设备
MOUNT_DEV=$(findmnt -n -o SOURCE "$MOUNT_POINT" 2>/dev/null)
if [ -z "$MOUNT_DEV" ]; then
    error "Could not determine mounted device"
fi

# 提取 loop 设备名称（去掉分区号）
LOOP_DEV=$(echo "$MOUNT_DEV" | sed 's/p[0-9]*$//')

info "Mount point: $MOUNT_POINT"
info "Device: $MOUNT_DEV"
info "Loop device: $LOOP_DEV"
echo ""

# 1. 卸载文件系统
info "[1/3] Unmounting filesystem..."
umount "$MOUNT_POINT"
info "  ✓ Unmounted"

# 2. 删除挂载点
info "[2/3] Removing mount point..."
rmdir "$MOUNT_POINT" 2>/dev/null && info "  ✓ Removed: $MOUNT_POINT" || true

# 3. 释放 loop 设备
info "[3/3] Detaching loop device..."
if [ -b "$LOOP_DEV" ]; then
    losetup -d "$LOOP_DEV"
    info "  ✓ Detached: $LOOP_DEV"
fi

echo ""
info "=========================================="
info "  Disk image unmounted successfully!"
info "=========================================="

