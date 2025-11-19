#!/bin/bash
# ============================================================================
# bootable-img-create-macos.sh - macOS (ARM/Intel) 版磁盘镜像生成脚本
# 使用 i686-elf-grub，而不是 grub-install
# 支持生成可在 BIOS 启动的 i386 PC 启动镜像
# ============================================================================

set -e

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()    { echo -e "${GREEN}$1${NC}"; }
warning() { echo -e "${YELLOW}$1${NC}"; }
error()   { echo "$1" >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

DISK_IMG="$PROJECT_ROOT/build/bootable.img"
DISK_SIZE_MB=${1:-128}

MOUNT_POINT="/tmp/castor_bootable"
KERNEL_BIN="$PROJECT_ROOT/build/castor.bin"
GRUB_CFG="$PROJECT_ROOT/grub.cfg"

SHELL_ELF="$PROJECT_ROOT/userland/shell/shell.elf"
HELLO_ELF="$PROJECT_ROOT/userland/helloworld/hello.elf"

# i686-elf-grub 工具链路径(你需要根据自己环境调整)
GRUB_PREFIX="/opt/homebrew"
GRUB_MKIMAGE="$GRUB_PREFIX/bin/i686-elf-grub-mkimage"

# 自动检测 GRUB 文件路径
if [ -d "$GRUB_PREFIX/lib/i686-elf/grub/i386-pc" ]; then
    GRUB_BIOS_BOOT_IMG="$GRUB_PREFIX/lib/i686-elf/grub/i386-pc/boot.img"
    GRUB_BIOS_CORE_MODS="$GRUB_PREFIX/lib/i686-elf/grub/i386-pc"
elif [ -d "$GRUB_PREFIX/lib/grub/i386-pc" ]; then
    GRUB_BIOS_BOOT_IMG="$GRUB_PREFIX/lib/grub/i386-pc/boot.img"
    GRUB_BIOS_CORE_MODS="$GRUB_PREFIX/lib/grub/i386-pc"
else
    # 尝试在 Cellar 中查找
    GRUB_CELLAR=$(find "$GRUB_PREFIX/Cellar/i686-elf-grub" -name "boot.img" -path "*i386-pc*" 2>/dev/null | head -n1)
    if [ -n "$GRUB_CELLAR" ]; then
        GRUB_BIOS_BOOT_IMG="$GRUB_CELLAR"
        GRUB_BIOS_CORE_MODS="$(dirname "$GRUB_CELLAR")"
    fi
fi

# ============================================================================

check_tools() {
    local tools=(
        hdiutil newfs_msdos dd mount umount python3
        "$GRUB_MKIMAGE"
    )

    local missing=()

    for t in "${tools[@]}"; do
        if [ ! -x "$t" ] && ! command -v "$t" &>/dev/null; then
            missing+=("$t")
        fi
    done

    if [ ${#missing[@]} -ne 0 ]; then
        error "Missing tools:\n${missing[*]}\nYou must install: i686-elf-grub + Xcode Command Line Tools + Python 3"
    fi
    
    # 检查 GRUB 文件
    [ -f "$GRUB_BIOS_BOOT_IMG" ] || error "GRUB boot.img not found: $GRUB_BIOS_BOOT_IMG"
}

check_files() {
    [ -f "$KERNEL_BIN" ]  || error "Kernel missing: $KERNEL_BIN"
    [ -f "$GRUB_CFG" ]    || error "grub.cfg missing: $GRUB_CFG"
}

create_image() {
    info "[1/7] Creating raw disk image (${DISK_SIZE_MB}MB)"
    mkdir -p "$(dirname "$DISK_IMG")"
    dd if=/dev/zero of="$DISK_IMG" bs=1m count="$DISK_SIZE_MB" >/dev/null
}

partition_image() {
    info "[2/7] Creating MBR partition table"

    # 计算镜像的总扇区数
    local img_size=$(stat -f%z "$DISK_IMG")
    local total_sectors=$((img_size / 512))
    local part_start=2048
    local part_size=$((total_sectors - part_start))
    
    # 使用 Python 创建 MBR 分区表
    # 这样可以精确控制分区起始位置（扇区 2048 = 1MB 对齐）
    python3 - <<EOF
import struct
import sys

# 打开镜像文件
with open("$DISK_IMG", "r+b") as f:
    # 读取现有的 MBR（可能为空）
    mbr = bytearray(f.read(512))
    
    # 清空分区表区域（偏移 446-510）
    mbr[446:510] = bytes(64)
    
    # 创建分区 1：FAT32 LBA (0x0C)，从扇区 2048 开始
    # 分区表项格式（16字节）：
    # +0: 引导标志 (0x00 = 非活动, 0x80 = 活动)
    # +1-3: CHS 起始地址
    # +4: 分区类型
    # +5-7: CHS 结束地址
    # +8-11: LBA 起始扇区（小端序）
    # +12-15: 扇区数量（小端序）
    
    partition = struct.pack(
        '<BBBBBBBBII',
        0x80,           # 引导标志：活动分区
        0x00, 0x00, 0x00,  # CHS 起始（不使用）
        0x0C,           # 分区类型：FAT32 LBA
        0x00, 0x00, 0x00,  # CHS 结束（不使用）
        $part_start,    # LBA 起始扇区
        $part_size      # 扇区数量
    )
    
    # 写入分区 1 到偏移 446
    mbr[446:446+16] = partition
    
    # 写入 MBR 签名 (0x55AA)
    mbr[510] = 0x55
    mbr[511] = 0xAA
    
    # 写回 MBR
    f.seek(0)
    f.write(mbr)
    
print("MBR partition table created")
EOF

    info "  [OK] Partition table created (partition starts at sector 2048)"
}

attach_image() {
    info "[3/7] Formatting and mounting partition"

    # attach 镜像并获取设备
    DEV_OUTPUT=$(hdiutil attach -nomount "$DISK_IMG")
    DISK_DEV=$(echo "$DEV_OUTPUT" | head -n1 | awk '{print $1}')
    PART_DEV="${DISK_DEV}s1"

    [ -n "$DISK_DEV" ] || error "Failed to attach disk image"

    # 格式化分区为 FAT32
    newfs_msdos -F 32 -v "CastorOS" "$PART_DEV" >/dev/null
    
    info "  [OK] Partition formatted as FAT32"

    # 挂载分区
    mkdir -p "$MOUNT_POINT"
    mount -t msdos "$PART_DEV" "$MOUNT_POINT"
    
    info "  [OK] Mounted at $MOUNT_POINT"
}

install_files() {
    info "[4/7] Installing CastorOS files"

    mkdir -p "$MOUNT_POINT/boot/grub"
    mkdir -p "$MOUNT_POINT/bin"

    cp "$KERNEL_BIN" "$MOUNT_POINT/boot/castor.bin"
    cp "$GRUB_CFG" "$MOUNT_POINT/boot/grub/grub.cfg"

    [ -f "$SHELL_ELF" ] && cp "$SHELL_ELF" "$MOUNT_POINT/bin/shell.elf"
    [ -f "$HELLO_ELF" ] && cp "$HELLO_ELF" "$MOUNT_POINT/bin/hello.elf"

    sync
}

install_grub_bios() {
    info "[5/7] Installing GRUB (manual BIOS mode, no grub-install)"
    
    # 确保文件系统已卸载
    if mount | grep -q "$MOUNT_POINT"; then
        umount "$MOUNT_POINT" 2>/dev/null || true
    fi
    if [ -n "$DISK_DEV" ]; then
        hdiutil detach "$DISK_DEV" >/dev/null 2>&1 || true
    fi

    # 创建一个嵌入式配置文件，帮助 GRUB 找到主配置文件
    cat > grub_early.cfg << 'EOF'
insmod part_msdos
insmod fat
set root=(hd0,msdos1)
set prefix=($root)/boot/grub
configfile ($root)/boot/grub/grub.cfg
EOF

    # 创建 core.img，包含必要的模块和嵌入式配置
    # 确保包含所有必要的文件系统和分区模块
    "$GRUB_MKIMAGE" \
        -O i386-pc \
        -o core.img \
        -c grub_early.cfg \
        -p '/boot/grub' \
        biosdisk part_msdos fat \
        multiboot normal configfile \
        ls cat echo test

    info "  Writing boot.img to MBR (440 bytes)"
    # 写入 boot.img 的前 440 字节到 MBR（避免覆盖分区表）
    dd if="$GRUB_BIOS_BOOT_IMG" of="$DISK_IMG" conv=notrunc bs=1 count=440 2>/dev/null

    info "  Writing core.img after MBR (at sector 1)"
    # 写入 core.img 到扇区 1（512 字节偏移），在 MBR 和第一个分区之间
    # diskutil 创建的分区从扇区 63 开始，所以有足够空间
    dd if=core.img of="$DISK_IMG" conv=notrunc bs=512 seek=1 2>/dev/null
    
    info "  GRUB bootloader installed"
    
    # 清理临时文件
    rm -f core.img grub_early.cfg
}

detach_image() {
    info "[6/7] Cleanup"
    
    # 最后清理：确保没有遗留的挂载
    if mount | grep -q "$MOUNT_POINT"; then
        umount "$MOUNT_POINT" 2>/dev/null || true
    fi
    
    # detach 所有相关的磁盘设备
    hdiutil detach "$DISK_DEV" >/dev/null 2>&1 || true
    
    # 清理挂载点
    [ -d "$MOUNT_POINT" ] && rmdir "$MOUNT_POINT" 2>/dev/null || true
}

summary() {
    info "[7/7] Done!"

    echo ""
    echo "Bootable image generated:"
    echo "  $DISK_IMG"
    echo ""
    echo "Test with:"
    echo "  qemu-system-i386 -hda $DISK_IMG"
    echo ""
}

# ============================================================================

main() {
    echo "======================================"
    echo " CastorOS bootable image creator (macOS)"
    echo " Using i686-elf-grub (manual BIOS mode)"
    echo "======================================"

    check_tools
    check_files
    create_image
    partition_image
    attach_image
    install_files
    install_grub_bios
    detach_image
    summary
}

trap detach_image EXIT

main "$@"
