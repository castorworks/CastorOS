/**
 * @file virtio_gpu.h
 * @brief VirtIO GPU driver for ARM64
 * 
 * Implements a simple virtio-gpu driver for QEMU's virt machine.
 * Uses MMIO transport (not PCI) for simplicity.
 */

#ifndef _DRIVERS_ARM_VIRTIO_GPU_H_
#define _DRIVERS_ARM_VIRTIO_GPU_H_

#include <types.h>

/* ============================================================================
 * VirtIO MMIO Register Offsets
 * ========================================================================== */

#define VIRTIO_MMIO_MAGIC_VALUE         0x000
#define VIRTIO_MMIO_VERSION             0x004
#define VIRTIO_MMIO_DEVICE_ID           0x008
#define VIRTIO_MMIO_VENDOR_ID           0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES     0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES     0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_QUEUE_SEL           0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX       0x034
#define VIRTIO_MMIO_QUEUE_NUM           0x038
#define VIRTIO_MMIO_QUEUE_READY         0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS    0x060
#define VIRTIO_MMIO_INTERRUPT_ACK       0x064
#define VIRTIO_MMIO_STATUS              0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW      0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH     0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW     0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH    0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW      0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH     0x0a4

/* Legacy (v1) specific registers */
#define VIRTIO_MMIO_GUEST_PAGE_SIZE     0x028
#define VIRTIO_MMIO_QUEUE_PFN           0x040
#define VIRTIO_MMIO_QUEUE_ALIGN         0x03c

/* VirtIO Magic Value */
#define VIRTIO_MMIO_MAGIC               0x74726976  /* "virt" */

/* VirtIO Device IDs */
#define VIRTIO_DEV_NET                  1
#define VIRTIO_DEV_BLK                  2
#define VIRTIO_DEV_CONSOLE              3
#define VIRTIO_DEV_GPU                  16

/* VirtIO Status Bits */
#define VIRTIO_STATUS_ACKNOWLEDGE       1
#define VIRTIO_STATUS_DRIVER            2
#define VIRTIO_STATUS_DRIVER_OK         4
#define VIRTIO_STATUS_FEATURES_OK       8
#define VIRTIO_STATUS_FAILED            128

/* ============================================================================
 * VirtIO GPU Command Types
 * ========================================================================== */

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO         0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D       0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF           0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT              0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH           0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D      0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING  0x0106
#define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING  0x0107

#define VIRTIO_GPU_RESP_OK_NODATA               0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO         0x1101

/* VirtIO GPU Formats */
#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM        1
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM        2
#define VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM        3
#define VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM        4
#define VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM        67
#define VIRTIO_GPU_FORMAT_X8B8G8R8_UNORM        68
#define VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM        121
#define VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM        134

/* ============================================================================
 * VirtIO GPU Structures
 * ========================================================================== */

/* Control header for all GPU commands */
struct virtio_gpu_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed));

/* Display info response */
struct virtio_gpu_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct virtio_gpu_display_one {
    struct virtio_gpu_rect r;
    uint32_t enabled;
    uint32_t flags;
} __attribute__((packed));

#define VIRTIO_GPU_MAX_SCANOUTS 16

struct virtio_gpu_resp_display_info {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_display_one pmodes[VIRTIO_GPU_MAX_SCANOUTS];
} __attribute__((packed));

/* Resource create 2D */
struct virtio_gpu_resource_create_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

/* Resource attach backing */
struct virtio_gpu_mem_entry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_attach_backing {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
    /* Followed by nr_entries of virtio_gpu_mem_entry */
} __attribute__((packed));

/* Set scanout */
struct virtio_gpu_set_scanout {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed));

/* Transfer to host 2D */
struct virtio_gpu_transfer_to_host_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

/* Resource flush */
struct virtio_gpu_resource_flush {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

/* ============================================================================
 * VirtIO Queue Structures
 * ========================================================================== */

#define VIRTQ_DESC_F_NEXT       1
#define VIRTQ_DESC_F_WRITE      2

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[];
} __attribute__((packed));

/* ============================================================================
 * Public API
 * ========================================================================== */

/**
 * @brief Initialize the virtio-gpu driver
 * @return 0 on success, negative on error
 */
int virtio_gpu_init(void);

/**
 * @brief Check if virtio-gpu is initialized
 * @return true if initialized
 */
bool virtio_gpu_is_initialized(void);

/**
 * @brief Get display width
 * @return Display width in pixels
 */
uint32_t virtio_gpu_get_width(void);

/**
 * @brief Get display height
 * @return Display height in pixels
 */
uint32_t virtio_gpu_get_height(void);

/**
 * @brief Get framebuffer pointer
 * @return Pointer to framebuffer memory
 */
uint32_t *virtio_gpu_get_framebuffer(void);

/**
 * @brief Flush a region of the framebuffer to display
 * @param x X coordinate
 * @param y Y coordinate
 * @param width Width of region
 * @param height Height of region
 */
void virtio_gpu_flush(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

/**
 * @brief Flush entire framebuffer
 */
void virtio_gpu_flush_all(void);

#endif /* _DRIVERS_ARM_VIRTIO_GPU_H_ */
