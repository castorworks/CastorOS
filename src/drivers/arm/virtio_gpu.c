/**
 * @file virtio_gpu.c
 * @brief VirtIO GPU driver for ARM64
 * 
 * Implements a simple virtio-gpu driver for QEMU's virt machine.
 * Uses MMIO transport for the virt platform.
 */

#include <drivers/arm/virtio_gpu.h>
#include <lib/string.h>

/* Forward declarations for serial output */
extern void serial_puts(const char *str);
extern void serial_put_hex64(uint64_t value);
extern void serial_put_hex32(uint32_t value);

/* ============================================================================
 * Configuration
 * ========================================================================== */

/* QEMU virt machine virtio MMIO base addresses (from DTB typically) */
/* virtio@a000000 through virtio@a003e00, each 0x200 apart */
#define VIRTIO_MMIO_BASE        0x0a000000ULL
#define VIRTIO_MMIO_SIZE        0x200
#define VIRTIO_MMIO_COUNT       32

/* Queue size */
#define VIRTQ_SIZE              16

/* Default display size - must be large enough for QEMU's default (1280x800) */
#define DEFAULT_WIDTH           1280
#define DEFAULT_HEIGHT          800

/* Resource ID for our framebuffer */
#define FB_RESOURCE_ID          1

/* ============================================================================
 * Static Data
 * ========================================================================== */

static volatile uint8_t *virtio_base = NULL;
static bool gpu_initialized = false;

/* Display info */
static uint32_t display_width = DEFAULT_WIDTH;
static uint32_t display_height = DEFAULT_HEIGHT;

/* Framebuffer */
static uint32_t *framebuffer = NULL;
static uint32_t fb_size = 0;

/* 
 * Virtqueue structures - for legacy mode, must be contiguous in memory:
 * - Descriptors: 16 bytes each * VIRTQ_SIZE = 256 bytes
 * - Available ring: 2 + 2 + 2*VIRTQ_SIZE + 2 = 38 bytes  
 * - Padding to page boundary for used ring (legacy requirement)
 * - Used ring: 2 + 2 + 8*VIRTQ_SIZE + 2 = 134 bytes
 * 
 * Legacy virtio requires used ring to be page-aligned from the start of the queue.
 * Total size for descriptors + avail = 256 + 38 = 294 bytes, round up to 4096.
 */
static uint8_t virtq_memory[8192] __attribute__((aligned(4096)));

/* Pointers into virtq_memory - set up in virtq_init() */
static volatile struct virtq_desc *controlq_desc;
static volatile struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTQ_SIZE];
    uint16_t used_event;
} *controlq_avail;
static volatile struct {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[VIRTQ_SIZE];
    uint16_t avail_event;
} *controlq_used;

/* Command/response buffers - must be accessible by device */
static uint8_t cmd_buffer[4096] __attribute__((aligned(4096)));
static uint8_t resp_buffer[4096] __attribute__((aligned(4096)));

/* Queue state */
static uint16_t controlq_free_head = 0;
static volatile uint16_t controlq_last_used = 0;

/* ============================================================================
 * MMIO Access Helpers
 * ========================================================================== */

static inline uint32_t virtio_read32(uint32_t offset) {
    return *(volatile uint32_t *)(virtio_base + offset);
}

static inline void virtio_write32(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(virtio_base + offset) = value;
}

/* ============================================================================
 * Virtqueue Operations
 * ========================================================================== */

static void virtq_init(void) {
    /* Clear the entire virtqueue memory */
    memset(virtq_memory, 0, sizeof(virtq_memory));
    
    /* 
     * Layout for legacy virtio (MUST be contiguous in memory):
     * 
     * The legacy virtio spec requires:
     * - Descriptors at offset 0
     * - Available ring immediately after descriptors
     * - Used ring at the next PAGE boundary after avail ring
     * 
     * For VIRTQ_SIZE=16:
     * - Descriptors: 16 * 16 = 256 bytes (offset 0)
     * - Available ring: 2 + 2 + 16*2 + 2 = 38 bytes (offset 256)
     * - Used ring: starts at offset 4096 (next page boundary)
     */
    controlq_desc = (volatile struct virtq_desc *)virtq_memory;
    
    /* Available ring starts after descriptors */
    size_t avail_offset = VIRTQ_SIZE * sizeof(struct virtq_desc);  /* 256 */
    controlq_avail = (volatile void *)(virtq_memory + avail_offset);
    
    /* 
     * Used ring must be at the next page boundary for legacy virtio.
     * This is calculated as: align_up(desc + avail, PAGE_SIZE)
     * For our case: align_up(256 + 38, 4096) = 4096
     */
    size_t used_offset = 4096;  /* Page-aligned for legacy mode */
    controlq_used = (volatile void *)(virtq_memory + used_offset);
    
    /* Initialize descriptor chain */
    for (int i = 0; i < VIRTQ_SIZE - 1; i++) {
        controlq_desc[i].next = i + 1;
    }
    controlq_desc[VIRTQ_SIZE - 1].next = 0;
    controlq_free_head = 0;
    
    /* Clear avail and used rings */
    controlq_avail->flags = 0;
    controlq_avail->idx = 0;
    controlq_used->flags = 0;
    controlq_used->idx = 0;
    
    /* Memory barrier to ensure all writes are visible */
    __asm__ volatile("dsb sy" ::: "memory");
}

static int virtq_alloc_desc(void) {
    int desc = controlq_free_head;
    controlq_free_head = controlq_desc[desc].next;
    return desc;
}

static void virtq_free_desc(int desc) {
    controlq_desc[desc].next = controlq_free_head;
    controlq_free_head = desc;
}

/* Send a command and wait for response */
static int virtio_gpu_cmd(void *cmd, uint32_t cmd_len, void *resp, uint32_t resp_len) {
    /* Allocate descriptors */
    int desc0 = virtq_alloc_desc();
    int desc1 = virtq_alloc_desc();
    
    /* Setup command descriptor (device reads) 
     * Note: virtio needs physical addresses. Our static buffers are in 
     * the kernel's identity-mapped region, so we can use the address directly.
     */
    controlq_desc[desc0].addr = (uint64_t)(uintptr_t)cmd;
    controlq_desc[desc0].len = cmd_len;
    controlq_desc[desc0].flags = VIRTQ_DESC_F_NEXT;
    controlq_desc[desc0].next = desc1;
    
    /* Setup response descriptor (device writes) */
    controlq_desc[desc1].addr = (uint64_t)(uintptr_t)resp;
    controlq_desc[desc1].len = resp_len;
    controlq_desc[desc1].flags = VIRTQ_DESC_F_WRITE;
    controlq_desc[desc1].next = 0;
    
    /* Ensure descriptor writes are visible before updating avail ring */
    __asm__ volatile("dsb sy" ::: "memory");
    
    /* Add to available ring */
    uint16_t avail_idx = controlq_avail->idx;
    controlq_avail->ring[avail_idx % VIRTQ_SIZE] = desc0;
    
    /* Ensure ring entry is written before updating index */
    __asm__ volatile("dsb sy" ::: "memory");
    controlq_avail->idx = avail_idx + 1;
    
    /* Ensure index is written before notifying device */
    __asm__ volatile("dsb sy" ::: "memory");
    
    /* Notify device - queue 0 */
    virtio_write32(VIRTIO_MMIO_QUEUE_NOTIFY, 0);
    
    /* Wait for response with timeout */
    uint32_t timeout = 10000000;  /* Increased timeout */
    volatile uint16_t used_idx;
    
    do {
        __asm__ volatile("dsb sy" ::: "memory");
        used_idx = controlq_used->idx;
        if (used_idx != controlq_last_used) {
            break;
        }
        timeout--;
    } while (timeout > 0);
    
    if (timeout == 0) {
        serial_puts("virtio-gpu: Command timeout\n");
        virtq_free_desc(desc0);
        virtq_free_desc(desc1);
        return -1;
    }
    
    controlq_last_used++;
    
    /* Free descriptors */
    virtq_free_desc(desc0);
    virtq_free_desc(desc1);
    
    /* Check response */
    struct virtio_gpu_ctrl_hdr *hdr = (struct virtio_gpu_ctrl_hdr *)resp;
    if (hdr->type >= 0x1200) {
        /* Error response */
        serial_puts("virtio-gpu: Error response type ");
        serial_put_hex32(hdr->type);
        serial_puts("\n");
        return -1;
    }
    
    return 0;
}

/* ============================================================================
 * GPU Commands
 * ========================================================================== */

static int gpu_get_display_info(void) {
    struct virtio_gpu_ctrl_hdr *cmd = (struct virtio_gpu_ctrl_hdr *)cmd_buffer;
    struct virtio_gpu_resp_display_info *resp = (struct virtio_gpu_resp_display_info *)resp_buffer;
    
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
    
    memset(resp, 0, sizeof(*resp));
    
    if (virtio_gpu_cmd(cmd, sizeof(*cmd), resp, sizeof(*resp)) < 0) {
        return -1;
    }
    
    if (resp->hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) {
        return -1;
    }
    
    /* Use first enabled display */
    for (int i = 0; i < VIRTIO_GPU_MAX_SCANOUTS; i++) {
        if (resp->pmodes[i].enabled) {
            display_width = resp->pmodes[i].r.width;
            display_height = resp->pmodes[i].r.height;
            serial_puts("virtio-gpu: Display ");
            serial_put_hex32(i);
            serial_puts(" enabled: ");
            serial_put_hex32(display_width);
            serial_puts("x");
            serial_put_hex32(display_height);
            serial_puts("\n");
            return 0;
        }
    }
    
    /* No display enabled, use defaults */
    serial_puts("virtio-gpu: No display enabled, using defaults\n");
    return 0;
}

static int gpu_create_resource(uint32_t resource_id, uint32_t width, uint32_t height) {
    struct virtio_gpu_resource_create_2d *cmd = (struct virtio_gpu_resource_create_2d *)cmd_buffer;
    struct virtio_gpu_ctrl_hdr *resp = (struct virtio_gpu_ctrl_hdr *)resp_buffer;
    
    memset(cmd, 0, sizeof(*cmd));
    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    cmd->resource_id = resource_id;
    cmd->format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;  /* BGRX */
    cmd->width = width;
    cmd->height = height;
    
    memset(resp, 0, sizeof(*resp));
    
    return virtio_gpu_cmd(cmd, sizeof(*cmd), resp, sizeof(*resp));
}

static int gpu_attach_backing(uint32_t resource_id, uint64_t addr, uint32_t length) {
    /* Command with one memory entry */
    struct {
        struct virtio_gpu_resource_attach_backing hdr;
        struct virtio_gpu_mem_entry entry;
    } *cmd = (void *)cmd_buffer;
    struct virtio_gpu_ctrl_hdr *resp = (struct virtio_gpu_ctrl_hdr *)resp_buffer;
    
    memset(cmd, 0, sizeof(*cmd));
    cmd->hdr.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    cmd->hdr.resource_id = resource_id;
    cmd->hdr.nr_entries = 1;
    cmd->entry.addr = addr;
    cmd->entry.length = length;
    cmd->entry.padding = 0;
    
    memset(resp, 0, sizeof(*resp));
    
    return virtio_gpu_cmd(cmd, sizeof(*cmd), resp, sizeof(*resp));
}

static int gpu_set_scanout(uint32_t scanout_id, uint32_t resource_id,
                           uint32_t width, uint32_t height) {
    struct virtio_gpu_set_scanout *cmd = (struct virtio_gpu_set_scanout *)cmd_buffer;
    struct virtio_gpu_ctrl_hdr *resp = (struct virtio_gpu_ctrl_hdr *)resp_buffer;
    
    memset(cmd, 0, sizeof(*cmd));
    cmd->hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    cmd->r.x = 0;
    cmd->r.y = 0;
    cmd->r.width = width;
    cmd->r.height = height;
    cmd->scanout_id = scanout_id;
    cmd->resource_id = resource_id;
    
    memset(resp, 0, sizeof(*resp));
    
    return virtio_gpu_cmd(cmd, sizeof(*cmd), resp, sizeof(*resp));
}

static int gpu_transfer_to_host(uint32_t resource_id, uint32_t x, uint32_t y,
                                 uint32_t width, uint32_t height) {
    struct virtio_gpu_transfer_to_host_2d *cmd = (struct virtio_gpu_transfer_to_host_2d *)cmd_buffer;
    struct virtio_gpu_ctrl_hdr *resp = (struct virtio_gpu_ctrl_hdr *)resp_buffer;
    
    memset(cmd, 0, sizeof(*cmd));
    cmd->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    cmd->r.x = x;
    cmd->r.y = y;
    cmd->r.width = width;
    cmd->r.height = height;
    cmd->offset = (y * display_width + x) * 4;
    cmd->resource_id = resource_id;
    
    memset(resp, 0, sizeof(*resp));
    
    return virtio_gpu_cmd(cmd, sizeof(*cmd), resp, sizeof(*resp));
}

static int gpu_resource_flush(uint32_t resource_id, uint32_t x, uint32_t y,
                               uint32_t width, uint32_t height) {
    struct virtio_gpu_resource_flush *cmd = (struct virtio_gpu_resource_flush *)cmd_buffer;
    struct virtio_gpu_ctrl_hdr *resp = (struct virtio_gpu_ctrl_hdr *)resp_buffer;
    
    memset(cmd, 0, sizeof(*cmd));
    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    cmd->r.x = x;
    cmd->r.y = y;
    cmd->r.width = width;
    cmd->r.height = height;
    cmd->resource_id = resource_id;
    
    memset(resp, 0, sizeof(*resp));
    
    return virtio_gpu_cmd(cmd, sizeof(*cmd), resp, sizeof(*resp));
}

/* ============================================================================
 * Device Discovery
 * ========================================================================== */

static volatile uint8_t *find_virtio_gpu(void) {
    for (uint32_t i = 0; i < VIRTIO_MMIO_COUNT; i++) {
        volatile uint8_t *base = (volatile uint8_t *)(VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_SIZE);
        
        uint32_t magic = *(volatile uint32_t *)(base + VIRTIO_MMIO_MAGIC_VALUE);
        uint32_t device_id = *(volatile uint32_t *)(base + VIRTIO_MMIO_DEVICE_ID);
        
        if (magic == VIRTIO_MMIO_MAGIC && device_id == VIRTIO_DEV_GPU) {
            serial_puts("virtio-gpu: Found at 0x");
            serial_put_hex64((uint64_t)base);
            serial_puts("\n");
            return base;
        }
    }
    return NULL;
}

/* ============================================================================
 * Public API
 * ========================================================================== */

int virtio_gpu_init(void) {
    serial_puts("virtio-gpu: Initializing...\n");
    
    /* Find virtio-gpu device */
    virtio_base = find_virtio_gpu();
    if (!virtio_base) {
        serial_puts("virtio-gpu: Device not found\n");
        return -1;
    }
    
    /* Check version - support both legacy (1) and modern (2) */
    uint32_t version = virtio_read32(VIRTIO_MMIO_VERSION);
    serial_puts("virtio-gpu: Version ");
    serial_put_hex32(version);
    serial_puts("\n");
    
    if (version != 1 && version != 2) {
        serial_puts("virtio-gpu: Unsupported version\n");
        return -1;
    }
    
    bool legacy_mode = (version == 1);
    
    /* Reset device */
    virtio_write32(VIRTIO_MMIO_STATUS, 0);
    
    /* Acknowledge device */
    virtio_write32(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    virtio_write32(VIRTIO_MMIO_STATUS, 
                   VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    
    /* Negotiate features (we don't need any special features) */
    virtio_write32(VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
    uint32_t features = virtio_read32(VIRTIO_MMIO_DEVICE_FEATURES);
    serial_puts("virtio-gpu: Device features: ");
    serial_put_hex32(features);
    serial_puts("\n");
    
    virtio_write32(VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
    virtio_write32(VIRTIO_MMIO_DRIVER_FEATURES, 0);  /* Accept no features */
    
    if (!legacy_mode) {
        /* Modern mode requires FEATURES_OK */
        virtio_write32(VIRTIO_MMIO_STATUS,
                       VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | 
                       VIRTIO_STATUS_FEATURES_OK);
        
        /* Check features OK */
        uint32_t status = virtio_read32(VIRTIO_MMIO_STATUS);
        if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
            serial_puts("virtio-gpu: Features not accepted\n");
            return -1;
        }
    }
    
    /* Initialize virtqueue */
    virtq_init();
    
    /* Setup controlq (queue 0) */
    virtio_write32(VIRTIO_MMIO_QUEUE_SEL, 0);
    
    uint32_t max_size = virtio_read32(VIRTIO_MMIO_QUEUE_NUM_MAX);
    
    if (max_size == 0) {
        serial_puts("virtio-gpu: Queue not available\n");
        return -1;
    }
    
    if (max_size < VIRTQ_SIZE) {
        serial_puts("virtio-gpu: Queue too small\n");
        return -1;
    }
    
    virtio_write32(VIRTIO_MMIO_QUEUE_NUM, VIRTQ_SIZE);
    
    /* Set queue addresses - different for legacy vs modern */
    uint64_t desc_addr = (uint64_t)(uintptr_t)controlq_desc;
    
    if (legacy_mode) {
        /* 
         * Legacy mode: use page-based addressing
         * The device expects the queue to start at a page-aligned address.
         * We pass the page frame number (address / page_size).
         * 
         * IMPORTANT: For legacy virtio, the queue layout is:
         * - Descriptors at PFN * page_size
         * - Available ring immediately after descriptors
         * - Used ring at next page boundary
         * 
         * We must also set QUEUE_ALIGN for legacy mode.
         */
        virtio_write32(VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096);
        
        /* Set alignment - used ring must be page-aligned */
        virtio_write32(VIRTIO_MMIO_QUEUE_ALIGN, 4096);
        
        /* Set the queue page frame number */
        uint32_t pfn = (uint32_t)(desc_addr / 4096);
        virtio_write32(VIRTIO_MMIO_QUEUE_PFN, pfn);
        
    } else {
        /* Modern mode: use separate addresses for each ring */
        uint64_t avail_addr = (uint64_t)(uintptr_t)controlq_avail;
        uint64_t used_addr = (uint64_t)(uintptr_t)controlq_used;
        
        virtio_write32(VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)desc_addr);
        virtio_write32(VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32_t)(desc_addr >> 32));
        virtio_write32(VIRTIO_MMIO_QUEUE_AVAIL_LOW, (uint32_t)avail_addr);
        virtio_write32(VIRTIO_MMIO_QUEUE_AVAIL_HIGH, (uint32_t)(avail_addr >> 32));
        virtio_write32(VIRTIO_MMIO_QUEUE_USED_LOW, (uint32_t)used_addr);
        virtio_write32(VIRTIO_MMIO_QUEUE_USED_HIGH, (uint32_t)(used_addr >> 32));
        
        virtio_write32(VIRTIO_MMIO_QUEUE_READY, 1);
        
    }
    
    /* Driver ready */
    if (legacy_mode) {
        virtio_write32(VIRTIO_MMIO_STATUS,
                       VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                       VIRTIO_STATUS_DRIVER_OK);
    } else {
        virtio_write32(VIRTIO_MMIO_STATUS,
                       VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                       VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);
    }
    
    /* Verify device is ready */
    uint32_t status = virtio_read32(VIRTIO_MMIO_STATUS);
    if (!(status & VIRTIO_STATUS_DRIVER_OK)) {
        serial_puts("virtio-gpu: Device not ready\n");
        return -1;
    }
    
    /* Get display info */
    serial_puts("virtio-gpu: Getting display info...\n");
    if (gpu_get_display_info() < 0) {
        serial_puts("virtio-gpu: Failed to get display info\n");
        /* Continue with defaults */
    }
    
    /* Allocate framebuffer */
    fb_size = display_width * display_height * 4;
    
    /* Use a static buffer for simplicity (in real code, use kmalloc) */
    static uint32_t static_fb[DEFAULT_WIDTH * DEFAULT_HEIGHT] __attribute__((aligned(4096)));
    framebuffer = static_fb;
    
    /* Clear framebuffer to black */
    memset(framebuffer, 0, fb_size);
    
    /* Create GPU resource */
    if (gpu_create_resource(FB_RESOURCE_ID, display_width, display_height) < 0) {
        serial_puts("virtio-gpu: Failed to create resource\n");
        return -1;
    }
    serial_puts("virtio-gpu: Resource created\n");
    
    /* Attach backing memory */
    if (gpu_attach_backing(FB_RESOURCE_ID, (uint64_t)(uintptr_t)framebuffer, fb_size) < 0) {
        serial_puts("virtio-gpu: Failed to attach backing\n");
        return -1;
    }
    serial_puts("virtio-gpu: Backing attached\n");
    
    /* Transfer initial framebuffer content to host BEFORE setting scanout */
    __asm__ volatile("dsb sy" ::: "memory");
    if (gpu_transfer_to_host(FB_RESOURCE_ID, 0, 0, display_width, display_height) < 0) {
        serial_puts("virtio-gpu: Failed initial transfer\n");
        return -1;
    }
    serial_puts("virtio-gpu: Initial transfer done\n");
    
    /* Set scanout */
    if (gpu_set_scanout(0, FB_RESOURCE_ID, display_width, display_height) < 0) {
        serial_puts("virtio-gpu: Failed to set scanout\n");
        return -1;
    }
    serial_puts("virtio-gpu: Scanout configured\n");
    
    /* Flush to display */
    if (gpu_resource_flush(FB_RESOURCE_ID, 0, 0, display_width, display_height) < 0) {
        serial_puts("virtio-gpu: Failed initial flush\n");
        return -1;
    }
    serial_puts("virtio-gpu: Initial flush done\n");
    
    gpu_initialized = true;
    serial_puts("virtio-gpu: Initialization complete\n");
    
    return 0;
}

bool virtio_gpu_is_initialized(void) {
    return gpu_initialized;
}

uint32_t virtio_gpu_get_width(void) {
    return display_width;
}

uint32_t virtio_gpu_get_height(void) {
    return display_height;
}

uint32_t *virtio_gpu_get_framebuffer(void) {
    return framebuffer;
}

void virtio_gpu_flush(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!gpu_initialized) return;
    
    /* Clamp to display bounds */
    if (x >= display_width || y >= display_height) return;
    if (x + width > display_width) width = display_width - x;
    if (y + height > display_height) height = display_height - y;
    
    /* Ensure framebuffer writes are visible to device */
    __asm__ volatile("dsb sy" ::: "memory");
    
    /* Transfer to host */
    gpu_transfer_to_host(FB_RESOURCE_ID, x, y, width, height);
    
    /* Flush to display */
    gpu_resource_flush(FB_RESOURCE_ID, x, y, width, height);
}

void virtio_gpu_flush_all(void) {
    virtio_gpu_flush(0, 0, display_width, display_height);
}
