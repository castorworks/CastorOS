#!/bin/bash
# ============================================================================
# bootable-img-mount.sh - 挂载磁盘镜像到系统
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
DISK_IMG="${1:-$PROJECT_ROOT/build/bootable.img}"
MOUNT_POINT="${2:-$PROJECT_ROOT/mnt/bootable}"

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

# 检查镜像文件是否存在
if [ ! -f "$DISK_IMG" ]; then
    error "Disk image not found: $DISK_IMG"
fi

info "=========================================="
info "  CastorOS Disk Image Mounter"
info "=========================================="
echo ""
echo "Image: $DISK_IMG"
echo "Mount point: $MOUNT_POINT"
echo ""

# 检查是否已经挂载
if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
    warning "Mount point is already in use: $MOUNT_POINT"
    echo "Please unmount first using: sudo ./scripts/umount-img.sh"
    exit 1
fi

# 1. 设置 loop 设备
info "[1/4] Setting up loop device..."
LOOP_DEV=$(losetup -f --show -P "$DISK_IMG")
if [ -z "$LOOP_DEV" ]; then
    error "Failed to create loop device"
fi
info "  Loop device: $LOOP_DEV"

# 等待分区设备创建
sleep 1

# 检查分区是否可用
if [ ! -b "${LOOP_DEV}p2" ]; then
    partprobe "$LOOP_DEV" 2>/dev/null || true
    sleep 1
fi

# 2. 显示分区信息
info "[2/4] Partition information:"
fdisk -l "$LOOP_DEV" 2>/dev/null | grep "^${LOOP_DEV}" || true
echo ""

# 3. 创建挂载点
info "[3/4] Creating mount point..."
mkdir -p "$MOUNT_POINT"
info "  Created: $MOUNT_POINT"

# 4. 挂载第二个分区（FAT32 根分区）
info "[4/4] Mounting partition..."
if [ -b "${LOOP_DEV}p2" ]; then
    mount "${LOOP_DEV}p2" "$MOUNT_POINT"
    info "  ✓ Mounted ${LOOP_DEV}p2 at $MOUNT_POINT"
else
    losetup -d "$LOOP_DEV" 2>/dev/null || true
    error "Partition ${LOOP_DEV}p2 not found"
fi

echo ""
info "=========================================="
info "  Disk image mounted successfully!"
info "=========================================="
echo ""
echo "You can now access the filesystem at:"
echo "  $MOUNT_POINT"
echo ""
echo "Directory contents:"
ls -lh "$MOUNT_POINT"
echo ""
warning "Remember to unmount when done:"
echo "  sudo ./scripts/umount-img.sh"
echo ""
info "Loop device: $LOOP_DEV"
echo "=========================================="

