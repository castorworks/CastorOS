/**
 * @file uhci.c
 * @brief UHCI (Universal Host Controller Interface) 驱动实现
 * 
 * Intel USB 1.x 主机控制器驱动
 */

#include <drivers/usb/uhci.h>
#include <drivers/pci.h>
#include <drivers/timer.h>
#include <kernel/io.h>
#include <kernel/irq.h>
#include <mm/heap.h>
#include <mm/vmm.h>
#include <lib/klog.h>
#include <lib/kprintf.h>
#include <lib/string.h>

/* 最大支持的 UHCI 控制器数量 */
#define UHCI_MAX_CONTROLLERS    4

/* 全局控制器数组 */
static uhci_controller_t uhci_controllers[UHCI_MAX_CONTROLLERS];
static int uhci_controller_count = 0;

/* ============================================================================
 * 寄存器访问函数
 * ============================================================================ */

static inline uint8_t uhci_read8(uhci_controller_t *hc, uint16_t reg) {
    return inb(hc->io_base + reg);
}

static inline uint16_t uhci_read16(uhci_controller_t *hc, uint16_t reg) {
    return inw(hc->io_base + reg);
}

static inline uint32_t uhci_read32(uhci_controller_t *hc, uint16_t reg) {
    return inl(hc->io_base + reg);
}

static inline void uhci_write8(uhci_controller_t *hc, uint16_t reg, uint8_t val) {
    outb(hc->io_base + reg, val);
}

static inline void uhci_write16(uhci_controller_t *hc, uint16_t reg, uint16_t val) {
    outw(hc->io_base + reg, val);
}

static inline void uhci_write32(uhci_controller_t *hc, uint16_t reg, uint32_t val) {
    outl(hc->io_base + reg, val);
}

/* ============================================================================
 * TD/QH 内存池管理
 * ============================================================================ */

/**
 * @brief 初始化 TD 池
 */
static int uhci_init_td_pool(uhci_controller_t *hc) {
    uint32_t size = sizeof(uhci_td_t) * UHCI_TD_POOL_SIZE;
    hc->td_pool = (uhci_td_t *)kmalloc_aligned(size, 16);
    if (!hc->td_pool) {
        return -1;
    }
    memset(hc->td_pool, 0, size);
    
    hc->td_pool_phys = vmm_virt_to_phys((uint32_t)hc->td_pool);
    
    hc->free_tds = NULL;
    for (int i = UHCI_TD_POOL_SIZE - 1; i >= 0; i--) {
        uhci_td_t *td = &hc->td_pool[i];
        td->phys_addr = hc->td_pool_phys + i * sizeof(uhci_td_t);
        td->next = hc->free_tds;
        hc->free_tds = td;
    }
    
    return 0;
}

/**
 * @brief 初始化 QH 池
 */
static int uhci_init_qh_pool(uhci_controller_t *hc) {
    uint32_t size = sizeof(uhci_qh_t) * UHCI_QH_POOL_SIZE;
    hc->qh_pool = (uhci_qh_t *)kmalloc_aligned(size, 16);
    if (!hc->qh_pool) {
        return -1;
    }
    memset(hc->qh_pool, 0, size);
    
    hc->qh_pool_phys = vmm_virt_to_phys((uint32_t)hc->qh_pool);
    
    hc->free_qhs = NULL;
    for (int i = UHCI_QH_POOL_SIZE - 1; i >= 0; i--) {
        uhci_qh_t *qh = &hc->qh_pool[i];
        qh->phys_addr = hc->qh_pool_phys + i * sizeof(uhci_qh_t);
        qh->next = hc->free_qhs;
        hc->free_qhs = qh;
    }
    
    return 0;
}

/**
 * @brief 分配 TD
 */
static uhci_td_t *uhci_alloc_td(uhci_controller_t *hc) {
    if (!hc->free_tds) {
        return NULL;
    }
    uhci_td_t *td = hc->free_tds;
    hc->free_tds = td->next;
    
    memset(td, 0, sizeof(uhci_td_t));
    td->phys_addr = hc->td_pool_phys + ((uint32_t)td - (uint32_t)hc->td_pool);
    td->link = UHCI_LP_TERM;
    
    return td;
}

/**
 * @brief 释放 TD
 */
static void uhci_free_td(uhci_controller_t *hc, uhci_td_t *td) {
    if (!td) return;
    td->next = hc->free_tds;
    hc->free_tds = td;
}

/**
 * @brief 分配 QH
 */
static uhci_qh_t *uhci_alloc_qh(uhci_controller_t *hc) {
    if (!hc->free_qhs) {
        return NULL;
    }
    uhci_qh_t *qh = hc->free_qhs;
    hc->free_qhs = qh->next;
    
    memset(qh, 0, sizeof(uhci_qh_t));
    qh->phys_addr = hc->qh_pool_phys + ((uint32_t)qh - (uint32_t)hc->qh_pool);
    qh->head = UHCI_LP_TERM;
    qh->element = UHCI_LP_TERM;
    
    return qh;
}

/**
 * @brief 释放 QH
 */
static void uhci_free_qh(uhci_controller_t *hc, uhci_qh_t *qh) {
    if (!qh) return;
    qh->next = hc->free_qhs;
    hc->free_qhs = qh;
}

/* ============================================================================
 * 帧列表初始化
 * ============================================================================ */

/**
 * @brief 初始化帧列表和 QH 结构
 */
static int uhci_init_frame_list(uhci_controller_t *hc) {
    /* 分配帧列表（4KB 对齐） */
    uint32_t size = sizeof(uint32_t) * UHCI_FRAME_LIST_SIZE;
    hc->frame_list = (uint32_t *)kmalloc_aligned(size, 4096);
    if (!hc->frame_list) {
        return -1;
    }
    
    hc->frame_list_phys = vmm_virt_to_phys((uint32_t)hc->frame_list);
    
    /* 分配 QH 池 */
    if (uhci_init_qh_pool(hc) < 0) {
        kfree_aligned(hc->frame_list);
        return -1;
    }
    
    /* 创建 QH 链表结构:
     * Frame List -> QH_INT -> QH_CTRL -> QH_BULK -> Terminate
     */
    hc->qh_int = uhci_alloc_qh(hc);
    hc->qh_ctrl = uhci_alloc_qh(hc);
    hc->qh_bulk = uhci_alloc_qh(hc);
    
    if (!hc->qh_int || !hc->qh_ctrl || !hc->qh_bulk) {
        return -1;
    }
    
    /* 链接 QH */
    hc->qh_int->head = hc->qh_ctrl->phys_addr | UHCI_LP_QH;
    hc->qh_ctrl->head = hc->qh_bulk->phys_addr | UHCI_LP_QH;
    hc->qh_bulk->head = UHCI_LP_TERM;
    
    /* 所有帧都指向 QH_INT */
    for (int i = 0; i < UHCI_FRAME_LIST_SIZE; i++) {
        hc->frame_list[i] = hc->qh_int->phys_addr | UHCI_LP_QH;
    }
    
    return 0;
}

/* ============================================================================
 * 控制器复位和初始化
 * ============================================================================ */

/**
 * @brief 复位 UHCI 控制器
 */
static void uhci_reset(uhci_controller_t *hc) {
    /* 停止控制器 */
    uhci_write16(hc, UHCI_REG_USBCMD, 0);
    
    /* 全局复位 */
    uhci_write16(hc, UHCI_REG_USBCMD, UHCI_CMD_GRESET);
    timer_wait(50);  // 等待 50ms
    
    /* 清除全局复位 */
    uhci_write16(hc, UHCI_REG_USBCMD, 0);
    timer_wait(10);
    
    /* 主机控制器复位 */
    uhci_write16(hc, UHCI_REG_USBCMD, UHCI_CMD_HCRESET);
    
    /* 等待复位完成 */
    int timeout = 100;
    while ((uhci_read16(hc, UHCI_REG_USBCMD) & UHCI_CMD_HCRESET) && timeout > 0) {
        timer_wait(1);
        timeout--;
    }
    
    if (timeout == 0) {
        LOG_WARN_MSG("uhci: Reset timeout\n");
    }
    
    /* 清除状态 */
    uhci_write16(hc, UHCI_REG_USBSTS, 0xFFFF);
    uhci_write16(hc, UHCI_REG_USBINTR, 0);
}

/**
 * @brief 启动 UHCI 控制器
 */
static void uhci_start(uhci_controller_t *hc) {
    /* 设置帧列表基地址 */
    uhci_write32(hc, UHCI_REG_FRBASEADD, hc->frame_list_phys);
    
    /* 设置帧编号为 0 */
    uhci_write16(hc, UHCI_REG_FRNUM, 0);
    
    /* 设置 SOF 修改值 */
    uhci_write8(hc, UHCI_REG_SOFMOD, 64);
    
    /* 启用中断 */
    uhci_write16(hc, UHCI_REG_USBINTR, 
                 UHCI_INTR_TIMEOUT | UHCI_INTR_RESUME | 
                 UHCI_INTR_IOC | UHCI_INTR_SP);
    
    /* 启动控制器 */
    uhci_write16(hc, UHCI_REG_USBCMD, UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP);
    
    LOG_INFO_MSG("uhci: Controller started\n");
}

/* ============================================================================
 * 端口操作
 * ============================================================================ */

uint16_t uhci_get_port_status(uhci_controller_t *hc, int port) {
    if (port < 0 || port >= UHCI_NUM_PORTS) return 0;
    uint16_t reg = (port == 0) ? UHCI_REG_PORTSC1 : UHCI_REG_PORTSC2;
    return uhci_read16(hc, reg);
}

static void uhci_set_port_status(uhci_controller_t *hc, int port, uint16_t value) {
    if (port < 0 || port >= UHCI_NUM_PORTS) return;
    uint16_t reg = (port == 0) ? UHCI_REG_PORTSC1 : UHCI_REG_PORTSC2;
    uhci_write16(hc, reg, value);
}

bool uhci_port_connected(uhci_controller_t *hc, int port) {
    return (uhci_get_port_status(hc, port) & UHCI_PORT_CCS) != 0;
}

bool uhci_port_low_speed(uhci_controller_t *hc, int port) {
    return (uhci_get_port_status(hc, port) & UHCI_PORT_LSDA) != 0;
}

int uhci_reset_port(uhci_controller_t *hc, int port) {
    if (port < 0 || port >= UHCI_NUM_PORTS) return -1;
    
    uint16_t status;
    
    /* 设置复位位 */
    status = uhci_get_port_status(hc, port);
    status &= ~UHCI_PORT_W1C_MASK;  // 不清除 W1C 位
    uhci_set_port_status(hc, port, status | UHCI_PORT_PR);
    
    /* 等待至少 50ms */
    timer_wait(60);
    
    /* 清除复位位 */
    status = uhci_get_port_status(hc, port);
    status &= ~(UHCI_PORT_W1C_MASK | UHCI_PORT_PR);
    uhci_set_port_status(hc, port, status);
    
    /* 等待复位完成 */
    timer_wait(10);
    
    /* 清除状态变化位 */
    status = uhci_get_port_status(hc, port);
    uhci_set_port_status(hc, port, status | UHCI_PORT_CSC | UHCI_PORT_PEC);
    
    LOG_DEBUG_MSG("uhci: Port %d reset, status=0x%04x\n", port, uhci_get_port_status(hc, port));
    return 0;
}

int uhci_enable_port(uhci_controller_t *hc, int port) {
    if (port < 0 || port >= UHCI_NUM_PORTS) return -1;
    
    uint16_t status = uhci_get_port_status(hc, port);
    
    /* 启用端口 */
    status &= ~UHCI_PORT_W1C_MASK;
    uhci_set_port_status(hc, port, status | UHCI_PORT_PE);
    
    timer_wait(10);
    
    /* 验证端口已启用 */
    status = uhci_get_port_status(hc, port);
    if (!(status & UHCI_PORT_PE)) {
        LOG_WARN_MSG("uhci: Port %d enable failed\n", port);
        return -1;
    }
    
    LOG_DEBUG_MSG("uhci: Port %d enabled, status=0x%04x\n", port, status);
    return 0;
}

/* ============================================================================
 * TD 构建函数
 * ============================================================================ */

/**
 * @brief 构建 TD token 字段
 */
static uint32_t uhci_build_token(uint8_t pid, uint8_t dev_addr, uint8_t endpoint,
                                  uint8_t toggle, uint16_t max_len) {
    uint32_t token = 0;
    token |= pid;
    token |= ((uint32_t)dev_addr & 0x7F) << 8;
    token |= ((uint32_t)endpoint & 0x0F) << 15;
    token |= ((uint32_t)toggle & 0x01) << 19;
    token |= ((uint32_t)(max_len > 0 ? max_len - 1 : 0x7FF) & 0x7FF) << 21;
    return token;
}

/**
 * @brief 创建 SETUP TD
 */
static uhci_td_t *uhci_create_setup_td(uhci_controller_t *hc, usb_urb_t *urb, uint32_t setup_phys) {
    uhci_td_t *td = uhci_alloc_td(hc);
    if (!td) return NULL;
    
    td->link = UHCI_LP_TERM | UHCI_LP_DEPTH;
    
    /* 控制/状态字段 */
    td->ctrl_status = UHCI_TD_ACTIVE | (3 << UHCI_TD_CERR_SHIFT);
    if (urb->device->speed == USB_SPEED_LOW) {
        td->ctrl_status |= UHCI_TD_LS;
    }
    
    /* Token 字段 */
    td->token = uhci_build_token(UHCI_TD_PID_SETUP, urb->device->address,
                                  0, 0, 8);
    
    /* 缓冲区 */
    td->buffer = setup_phys;
    td->urb = urb;
    
    return td;
}

/**
 * @brief 创建 DATA TD
 */
static uhci_td_t *uhci_create_data_td(uhci_controller_t *hc, usb_urb_t *urb,
                                       uint8_t pid, uint32_t data_phys,
                                       uint16_t len, uint8_t toggle) {
    uhci_td_t *td = uhci_alloc_td(hc);
    if (!td) return NULL;
    
    td->link = UHCI_LP_TERM | UHCI_LP_DEPTH;
    
    /* 控制/状态字段 */
    td->ctrl_status = UHCI_TD_ACTIVE | (3 << UHCI_TD_CERR_SHIFT);
    if (urb->device->speed == USB_SPEED_LOW) {
        td->ctrl_status |= UHCI_TD_LS;
    }
    if (pid == UHCI_TD_PID_IN) {
        td->ctrl_status |= UHCI_TD_SPD;  // 短包检测
    }
    
    /* Token 字段 */
    uint8_t endpoint = urb->endpoint ? (urb->endpoint->address & 0x0F) : 0;
    td->token = uhci_build_token(pid, urb->device->address, endpoint, toggle, len);
    
    /* 缓冲区 */
    td->buffer = data_phys;
    td->urb = urb;
    
    return td;
}

/**
 * @brief 创建 STATUS TD
 */
static uhci_td_t *uhci_create_status_td(uhci_controller_t *hc, usb_urb_t *urb, uint8_t pid) {
    uhci_td_t *td = uhci_alloc_td(hc);
    if (!td) return NULL;
    
    td->link = UHCI_LP_TERM | UHCI_LP_DEPTH;
    
    /* 控制/状态字段 - 状态阶段总是在完成时中断 */
    td->ctrl_status = UHCI_TD_ACTIVE | UHCI_TD_IOC | (3 << UHCI_TD_CERR_SHIFT);
    if (urb->device->speed == USB_SPEED_LOW) {
        td->ctrl_status |= UHCI_TD_LS;
    }
    
    /* Token 字段 - 状态阶段使用 toggle=1，长度=0 */
    td->token = uhci_build_token(pid, urb->device->address, 0, 1, 0);
    
    td->buffer = 0;
    td->urb = urb;
    
    return td;
}

/* ============================================================================
 * URB 提交
 * ============================================================================ */

/**
 * @brief 检查 TD 是否完成
 */
static bool uhci_td_is_complete(uhci_td_t *td) {
    return !(td->ctrl_status & UHCI_TD_ACTIVE);
}

/**
 * @brief 获取 TD 状态
 */
static int uhci_td_get_status(uhci_td_t *td) {
    if (td->ctrl_status & UHCI_TD_ACTIVE) {
        return URB_STATUS_PENDING;
    }
    if (td->ctrl_status & UHCI_TD_STALLED) {
        return URB_STATUS_STALL;
    }
    if (td->ctrl_status & (UHCI_TD_DATA_BUFFER_ERR | UHCI_TD_BABBLE | 
                           UHCI_TD_TIMEOUT | UHCI_TD_BITSTUFF)) {
        return URB_STATUS_ERROR;
    }
    if (td->ctrl_status & UHCI_TD_NAK) {
        return URB_STATUS_NAK;
    }
    return URB_STATUS_COMPLETE;
}

/**
 * @brief 获取 TD 实际传输长度
 */
static uint32_t uhci_td_get_actlen(uhci_td_t *td) {
    uint32_t actlen = td->ctrl_status & UHCI_TD_ACTLEN_MASK;
    if (actlen == 0x7FF) {
        return 0;  // NULL 包
    }
    return (actlen + 1) & 0x7FF;
}

/**
 * @brief 提交控制传输
 */
static int uhci_submit_control(uhci_controller_t *hc, usb_urb_t *urb) {
    /* 分配 SETUP 包缓冲区 */
    uint8_t *setup_buf = (uint8_t *)kmalloc_aligned(8, 16);
    if (!setup_buf) {
        return -1;
    }
    memcpy(setup_buf, &urb->setup, 8);
    uint32_t setup_phys = vmm_virt_to_phys((uint32_t)setup_buf);
    
    /* 数据缓冲区物理地址 */
    uint32_t data_phys = 0;
    if (urb->buffer && urb->buffer_length > 0) {
        data_phys = vmm_virt_to_phys((uint32_t)urb->buffer);
    }
    
    /* 确定数据方向 */
    bool is_in = (urb->setup.bmRequestType & USB_REQTYPE_DIR_MASK) == USB_REQTYPE_DEV_TO_HOST;
    
    /* 创建 SETUP TD */
    uhci_td_t *setup_td = uhci_create_setup_td(hc, urb, setup_phys);
    if (!setup_td) {
        kfree_aligned(setup_buf);
        return -1;
    }
    
    uhci_td_t *first_td = setup_td;
    uhci_td_t *last_td = setup_td;
    
    /* 创建 DATA TDs */
    uint8_t toggle = 1;
    uint32_t offset = 0;
    uint16_t max_pkt = urb->endpoint->max_packet_size;
    
    while (offset < urb->buffer_length) {
        uint16_t len = (urb->buffer_length - offset > max_pkt) ? max_pkt : (urb->buffer_length - offset);
        
        uhci_td_t *data_td = uhci_create_data_td(hc, urb,
                                                  is_in ? UHCI_TD_PID_IN : UHCI_TD_PID_OUT,
                                                  data_phys + offset, len, toggle);
        if (!data_td) {
            /* 清理 */
            for (uhci_td_t *td = first_td; td; ) {
                uhci_td_t *next = td->next;
                uhci_free_td(hc, td);
                td = next;
            }
            kfree_aligned(setup_buf);
            return -1;
        }
        
        last_td->link = data_td->phys_addr | UHCI_LP_DEPTH;
        last_td->next = data_td;
        last_td = data_td;
        
        toggle ^= 1;
        offset += len;
    }
    
    /* 创建 STATUS TD */
    uhci_td_t *status_td = uhci_create_status_td(hc, urb, 
                                                  is_in ? UHCI_TD_PID_OUT : UHCI_TD_PID_IN);
    if (!status_td) {
        for (uhci_td_t *td = first_td; td; ) {
            uhci_td_t *next = td->next;
            uhci_free_td(hc, td);
            td = next;
        }
        kfree_aligned(setup_buf);
        return -1;
    }
    
    last_td->link = status_td->phys_addr | UHCI_LP_DEPTH;
    last_td->next = status_td;
    last_td = status_td;
    
    /* 分配 QH */
    uhci_qh_t *qh = uhci_alloc_qh(hc);
    if (!qh) {
        for (uhci_td_t *td = first_td; td; ) {
            uhci_td_t *next = td->next;
            uhci_free_td(hc, td);
            td = next;
        }
        kfree_aligned(setup_buf);
        return -1;
    }
    
    qh->first_td = first_td;
    qh->last_td = last_td;
    qh->element = first_td->phys_addr;
    
    /* 插入到控制 QH 链表 */
    qh->head = hc->qh_ctrl->head;
    hc->qh_ctrl->element = qh->phys_addr | UHCI_LP_QH;
    hc->active_ctrl_qh = qh;
    
    /* 等待完成（轮询） */
    int timeout = 5000;  // 5 秒超时
    while (timeout > 0) {
        /* 检查所有 TD */
        bool all_done = true;
        int status = URB_STATUS_COMPLETE;
        uint32_t total_len = 0;
        
        for (uhci_td_t *td = first_td; td; td = td->next) {
            if (!uhci_td_is_complete(td)) {
                all_done = false;
                break;
            }
            
            int td_status = uhci_td_get_status(td);
            if (td_status != URB_STATUS_COMPLETE) {
                status = td_status;
                all_done = true;
                break;
            }
            
            /* 累加数据传输长度（跳过 SETUP 和 STATUS） */
            if (td != first_td && td != last_td) {
                total_len += uhci_td_get_actlen(td);
            }
        }
        
        if (all_done) {
            urb->actual_length = total_len;
            urb->status = status;
            break;
        }
        
        timer_wait(1);
        timeout--;
    }
    
    if (timeout == 0) {
        urb->status = URB_STATUS_TIMEOUT;
        LOG_WARN_MSG("uhci: Control transfer timeout\n");
    }
    
    /* 从 QH 链表移除 */
    hc->qh_ctrl->element = UHCI_LP_TERM;
    hc->active_ctrl_qh = NULL;
    
    /* 释放资源 */
    for (uhci_td_t *td = first_td; td; ) {
        uhci_td_t *next = td->next;
        uhci_free_td(hc, td);
        td = next;
    }
    uhci_free_qh(hc, qh);
    kfree_aligned(setup_buf);
    
    return (urb->status == URB_STATUS_COMPLETE) ? 0 : urb->status;
}

/**
 * @brief 提交批量传输
 */
static int uhci_submit_bulk(uhci_controller_t *hc, usb_urb_t *urb) {
    if (!urb->endpoint || urb->endpoint->type != USB_TRANSFER_BULK) {
        return -1;
    }
    
    bool is_in = (urb->endpoint->address & USB_DIR_MASK) == USB_DIR_IN;
    uint8_t pid = is_in ? UHCI_TD_PID_IN : UHCI_TD_PID_OUT;
    uint16_t max_pkt = urb->endpoint->max_packet_size;
    
    /* 数据缓冲区物理地址 */
    uint32_t data_phys = vmm_virt_to_phys((uint32_t)urb->buffer);
    
    /* 创建 TDs */
    uhci_td_t *first_td = NULL;
    uhci_td_t *last_td = NULL;
    uint8_t toggle = urb->endpoint->toggle;
    uint32_t offset = 0;
    
    while (offset < urb->buffer_length) {
        uint16_t len = (urb->buffer_length - offset > max_pkt) ? max_pkt : (urb->buffer_length - offset);
        
        uhci_td_t *td = uhci_create_data_td(hc, urb, pid, data_phys + offset, len, toggle);
        if (!td) {
            for (uhci_td_t *t = first_td; t; ) {
                uhci_td_t *next = t->next;
                uhci_free_td(hc, t);
                t = next;
            }
            return -1;
        }
        
        if (!first_td) {
            first_td = td;
        }
        if (last_td) {
            last_td->link = td->phys_addr | UHCI_LP_DEPTH;
            last_td->next = td;
        }
        last_td = td;
        
        toggle ^= 1;
        offset += len;
    }
    
    /* 最后一个 TD 设置 IOC */
    if (last_td) {
        last_td->ctrl_status |= UHCI_TD_IOC;
    }
    
    /* 分配 QH */
    uhci_qh_t *qh = uhci_alloc_qh(hc);
    if (!qh) {
        for (uhci_td_t *td = first_td; td; ) {
            uhci_td_t *next = td->next;
            uhci_free_td(hc, td);
            td = next;
        }
        return -1;
    }
    
    qh->first_td = first_td;
    qh->last_td = last_td;
    qh->element = first_td->phys_addr;
    
    /* 插入到批量 QH 链表 */
    qh->head = hc->qh_bulk->head;
    hc->qh_bulk->element = qh->phys_addr | UHCI_LP_QH;
    hc->active_bulk_qh = qh;
    
    /* 等待完成 */
    int timeout = 10000;  // 10 秒超时
    while (timeout > 0) {
        bool all_done = true;
        int status = URB_STATUS_COMPLETE;
        uint32_t total_len = 0;
        
        for (uhci_td_t *td = first_td; td; td = td->next) {
            if (!uhci_td_is_complete(td)) {
                all_done = false;
                break;
            }
            
            int td_status = uhci_td_get_status(td);
            if (td_status != URB_STATUS_COMPLETE) {
                status = td_status;
                all_done = true;
                break;
            }
            
            total_len += uhci_td_get_actlen(td);
        }
        
        if (all_done) {
            urb->actual_length = total_len;
            urb->status = status;
            break;
        }
        
        timer_wait(1);
        timeout--;
    }
    
    if (timeout == 0) {
        urb->status = URB_STATUS_TIMEOUT;
        LOG_WARN_MSG("uhci: Bulk transfer timeout\n");
    }
    
    /* 更新端点 toggle */
    urb->endpoint->toggle = toggle;
    
    /* 移除并释放 */
    hc->qh_bulk->element = UHCI_LP_TERM;
    hc->active_bulk_qh = NULL;
    
    for (uhci_td_t *td = first_td; td; ) {
        uhci_td_t *next = td->next;
        uhci_free_td(hc, td);
        td = next;
    }
    uhci_free_qh(hc, qh);
    
    return (urb->status == URB_STATUS_COMPLETE) ? 0 : urb->status;
}

int uhci_submit_urb(uhci_controller_t *hc, usb_urb_t *urb) {
    if (!hc || !urb || !urb->device || !urb->endpoint) {
        return -1;
    }
    
    switch (urb->endpoint->type) {
        case USB_TRANSFER_CONTROL:
            return uhci_submit_control(hc, urb);
        case USB_TRANSFER_BULK:
            return uhci_submit_bulk(hc, urb);
        default:
            LOG_ERROR_MSG("uhci: Unsupported transfer type %d\n", urb->endpoint->type);
            return -1;
    }
}

/* ============================================================================
 * 中断处理
 * ============================================================================ */

static void uhci_irq_handler(registers_t *regs) {
    (void)regs;
    
    for (int i = 0; i < uhci_controller_count; i++) {
        uhci_controller_t *hc = &uhci_controllers[i];
        
        uint16_t status = uhci_read16(hc, UHCI_REG_USBSTS);
        if (status == 0) {
            continue;
        }
        
        /* 清除中断状态（写 1 清除） */
        uhci_write16(hc, UHCI_REG_USBSTS, status);
        
        if (status & UHCI_STS_USBINT) {
            LOG_DEBUG_MSG("uhci: Transfer complete interrupt\n");
        }
        
        if (status & UHCI_STS_ERROR) {
            LOG_WARN_MSG("uhci: USB error interrupt\n");
        }
        
        if (status & UHCI_STS_RD) {
            LOG_DEBUG_MSG("uhci: Resume detect\n");
        }
        
        if (status & UHCI_STS_HSE) {
            LOG_ERROR_MSG("uhci: Host system error!\n");
        }
        
        if (status & UHCI_STS_HCPE) {
            LOG_ERROR_MSG("uhci: Host controller process error!\n");
        }
        
        /* 检查端口状态变化（热插拔） */
        uhci_check_port_changes(hc);
    }
}

/* ============================================================================
 * USB 主机控制器操作
 * ============================================================================ */

static int uhci_hc_submit_urb(void *hc_data, usb_urb_t *urb) {
    return uhci_submit_urb((uhci_controller_t *)hc_data, urb);
}

static int uhci_hc_reset_port(void *hc_data, int port) {
    return uhci_reset_port((uhci_controller_t *)hc_data, port);
}

static int uhci_hc_enable_port(void *hc_data, int port) {
    return uhci_enable_port((uhci_controller_t *)hc_data, port);
}

static uint16_t uhci_hc_get_port_status(void *hc_data, int port) {
    return uhci_get_port_status((uhci_controller_t *)hc_data, port);
}

static bool uhci_hc_port_connected(void *hc_data, int port) {
    return uhci_port_connected((uhci_controller_t *)hc_data, port);
}

static bool uhci_hc_port_low_speed(void *hc_data, int port) {
    return uhci_port_low_speed((uhci_controller_t *)hc_data, port);
}

static int uhci_hc_get_port_count(void *hc_data) {
    (void)hc_data;
    return UHCI_NUM_PORTS;
}

static usb_hc_ops_t uhci_hc_ops = {
    .submit_urb = uhci_hc_submit_urb,
    .cancel_urb = NULL,
    .reset_port = uhci_hc_reset_port,
    .enable_port = uhci_hc_enable_port,
    .get_port_status = uhci_hc_get_port_status,
    .port_connected = uhci_hc_port_connected,
    .port_low_speed = uhci_hc_port_low_speed,
    .get_port_count = uhci_hc_get_port_count,
};

/* ============================================================================
 * 设备检测和初始化
 * ============================================================================ */

/**
 * @brief 初始化单个 UHCI 控制器
 */
static int uhci_init_controller(pci_device_t *pci_dev) {
    if (uhci_controller_count >= UHCI_MAX_CONTROLLERS) {
        LOG_WARN_MSG("uhci: Maximum controllers reached\n");
        return -1;
    }
    
    uhci_controller_t *hc = &uhci_controllers[uhci_controller_count];
    memset(hc, 0, sizeof(uhci_controller_t));
    
    /* 保存 PCI 信息 */
    hc->bus = pci_dev->bus;
    hc->slot = pci_dev->slot;
    hc->func = pci_dev->func;
    hc->irq = pci_dev->interrupt_line;
    
    /* 获取 I/O 基地址（BAR4） */
    uint32_t bar4 = pci_get_bar_address(pci_dev, 4);
    if (bar4 == 0 || !pci_bar_is_io(pci_dev, 4)) {
        LOG_ERROR_MSG("uhci: Invalid BAR4\n");
        return -1;
    }
    hc->io_base = (uint16_t)bar4;
    
    /* 启用 PCI 总线主控和 I/O 空间 */
    pci_enable_bus_master(pci_dev);
    pci_enable_io_space(pci_dev);
    
    /* 复位控制器 */
    uhci_reset(hc);
    
    /* 初始化 TD 池 */
    if (uhci_init_td_pool(hc) < 0) {
        LOG_ERROR_MSG("uhci: Failed to init TD pool\n");
        return -1;
    }
    
    /* 初始化帧列表 */
    if (uhci_init_frame_list(hc) < 0) {
        LOG_ERROR_MSG("uhci: Failed to init frame list\n");
        return -1;
    }
    
    /* 注册中断处理程序 */
    if (hc->irq != 0 && hc->irq != 0xFF) {
        irq_register_handler(hc->irq, uhci_irq_handler);
        irq_enable_line(hc->irq);
    }
    
    /* 启动控制器 */
    uhci_start(hc);
    
    /* 初始化 USB 主机控制器结构 */
    hc->usb_hc.name = "UHCI";
    hc->usb_hc.private_data = hc;
    hc->usb_hc.ops = &uhci_hc_ops;
    hc->usb_hc.next_address = 1;
    hc->usb_hc.devices = NULL;
    
    /* 注册到 USB 核心 */
    usb_register_hc(&hc->usb_hc);
    
    uhci_controller_count++;
    
    LOG_INFO_MSG("uhci: Controller %d initialized (I/O base: 0x%04x, IRQ: %d)\n",
                 uhci_controller_count - 1, hc->io_base, hc->irq);
    
    /* 初始化热插拔跟踪 */
    for (int port = 0; port < UHCI_NUM_PORTS; port++) {
        hc->port_status[port] = uhci_get_port_status(hc, port);
        hc->port_device[port] = NULL;
        
        /* 清除任何挂起的状态变化位 */
        if (hc->port_status[port] & UHCI_PORT_W1C_MASK) {
            uhci_set_port_status(hc, port, hc->port_status[port] | UHCI_PORT_W1C_MASK);
            hc->port_status[port] = uhci_get_port_status(hc, port);
        }
        
        if (uhci_port_connected(hc, port)) {
            LOG_INFO_MSG("uhci: Device detected on port %d (%s speed)\n",
                        port, uhci_port_low_speed(hc, port) ? "low" : "full");
        }
    }
    
    return 0;
}

/**
 * @brief 初始化 UHCI 驱动
 */
int uhci_init(void) {
    uhci_controller_count = 0;
    
    /* 扫描 PCI 总线查找 UHCI 控制器 */
    int pci_count = pci_get_device_count();
    for (int i = 0; i < pci_count; i++) {
        pci_device_t *dev = pci_get_device(i);
        if (dev && 
            dev->class_code == UHCI_PCI_CLASS &&
            dev->subclass == UHCI_PCI_SUBCLASS &&
            dev->prog_if == UHCI_PCI_PROG_IF) {
            
            LOG_INFO_MSG("uhci: Found UHCI controller at %02x:%02x.%x\n",
                        dev->bus, dev->slot, dev->func);
            uhci_init_controller(dev);
        }
    }
    
    if (uhci_controller_count == 0) {
        LOG_DEBUG_MSG("uhci: No controllers found\n");
        return 0;
    }
    
    LOG_INFO_MSG("uhci: Initialized %d controller(s)\n", uhci_controller_count);
    return uhci_controller_count;
}

uhci_controller_t *uhci_get_controller(int index) {
    if (index < 0 || index >= uhci_controller_count) {
        return NULL;
    }
    return &uhci_controllers[index];
}

void uhci_print_info(uhci_controller_t *hc) {
    if (!hc) return;
    
    kprintf("UHCI Controller Info:\n");
    kprintf("  PCI: %02x:%02x.%x\n", hc->bus, hc->slot, hc->func);
    kprintf("  I/O Base: 0x%04x\n", hc->io_base);
    kprintf("  IRQ: %d\n", hc->irq);
    kprintf("  USBCMD: 0x%04x\n", uhci_read16(hc, UHCI_REG_USBCMD));
    kprintf("  USBSTS: 0x%04x\n", uhci_read16(hc, UHCI_REG_USBSTS));
    kprintf("  FRNUM: %d\n", uhci_read16(hc, UHCI_REG_FRNUM));
    kprintf("  FRBASEADD: 0x%08x\n", uhci_read32(hc, UHCI_REG_FRBASEADD));
    kprintf("  Port 0: 0x%04x\n", uhci_get_port_status(hc, 0));
    kprintf("  Port 1: 0x%04x\n", uhci_get_port_status(hc, 1));
}

int uhci_get_controller_count(void) {
    return uhci_controller_count;
}

/* ============================================================================
 * 热插拔支持
 * ============================================================================ */

void uhci_check_port_changes(uhci_controller_t *hc) {
    if (!hc) return;
    
    for (int port = 0; port < UHCI_NUM_PORTS; port++) {
        uint16_t status = uhci_get_port_status(hc, port);
        
        /* 检查连接状态变化 (CSC) */
        if (status & UHCI_PORT_CSC) {
            /* 清除 CSC 位（写 1 清除） */
            uhci_set_port_status(hc, port, status | UHCI_PORT_CSC);
            
            /* 获取当前连接状态 */
            bool connected = (status & UHCI_PORT_CCS) != 0;
            bool was_connected = hc->port_device[port] != NULL;
            
            LOG_INFO_MSG("uhci: Port %d status change: %s -> %s\n",
                        port,
                        was_connected ? "connected" : "disconnected",
                        connected ? "connected" : "disconnected");
            
            if (connected && !was_connected) {
                /* 新设备连接 */
                /* 延迟一下等待设备稳定 */
                timer_wait(100);
                
                /* 检查设备是否仍然连接 */
                status = uhci_get_port_status(hc, port);
                if (status & UHCI_PORT_CCS) {
                    usb_device_t *dev = usb_handle_port_connect(&hc->usb_hc, port);
                    hc->port_device[port] = dev;
                }
            } else if (!connected && was_connected) {
                /* 设备断开 */
                usb_handle_port_disconnect(&hc->usb_hc, port);
                hc->port_device[port] = NULL;
            }
            
            /* 更新记录的端口状态 */
            hc->port_status[port] = uhci_get_port_status(hc, port);
        }
        
        /* 检查端口使能变化 (PEC) */
        if (status & UHCI_PORT_PEC) {
            /* 清除 PEC 位 */
            uhci_set_port_status(hc, port, status | UHCI_PORT_PEC);
            
            LOG_DEBUG_MSG("uhci: Port %d enable change, status=0x%04x\n", port, status);
        }
    }
}

void uhci_poll_port_changes(void) {
    for (int i = 0; i < uhci_controller_count; i++) {
        uhci_controller_t *hc = &uhci_controllers[i];
        uhci_check_port_changes(hc);
    }
}

/**
 * @brief 同步端口设备映射（在初始扫描后调用）
 * 
 * 遍历已枚举的设备，更新 port_device 数组
 */
void uhci_sync_port_devices(void) {
    for (int i = 0; i < uhci_controller_count; i++) {
        uhci_controller_t *hc = &uhci_controllers[i];
        
        /* 清空映射 */
        for (int port = 0; port < UHCI_NUM_PORTS; port++) {
            hc->port_device[port] = NULL;
        }
        
        /* 遍历设备列表，建立端口映射 */
        for (usb_device_t *dev = hc->usb_hc.devices; dev; dev = dev->next) {
            if (dev->port < UHCI_NUM_PORTS) {
                hc->port_device[dev->port] = dev;
                LOG_DEBUG_MSG("uhci: Port %d -> Device addr %d\n", dev->port, dev->address);
            }
        }
    }
}

/* ============================================================================
 * 热插拔监控定时器
 * ============================================================================ */

/** 热插拔轮询间隔（毫秒） */
#define UHCI_HOTPLUG_POLL_INTERVAL_MS   500

/** 热插拔定时器 ID */
static uint32_t uhci_hotplug_timer_id = 0;

/**
 * @brief 热插拔轮询定时器回调
 */
static void uhci_hotplug_timer_callback(void *data) {
    (void)data;
    uhci_poll_port_changes();
}

/**
 * @brief 启动热插拔监控
 * 
 * 注册周期性定时器，定期检查端口状态变化
 * 应在系统初始化完成后调用
 */
void uhci_start_hotplug_monitor(void) {
    if (uhci_controller_count == 0) {
        return;  // 没有 UHCI 控制器，不需要监控
    }
    
    if (uhci_hotplug_timer_id != 0) {
        return;  // 已经启动
    }
    
    uhci_hotplug_timer_id = timer_register_callback(
        uhci_hotplug_timer_callback,
        NULL,
        UHCI_HOTPLUG_POLL_INTERVAL_MS,
        true  // 重复执行
    );
    
    if (uhci_hotplug_timer_id != 0) {
        LOG_INFO_MSG("uhci: Hot-plug monitor started (polling every %d ms)\n",
                    UHCI_HOTPLUG_POLL_INTERVAL_MS);
    } else {
        LOG_WARN_MSG("uhci: Failed to start hot-plug monitor\n");
    }
}

/**
 * @brief 停止热插拔监控
 */
void uhci_stop_hotplug_monitor(void) {
    if (uhci_hotplug_timer_id != 0) {
        timer_unregister_callback(uhci_hotplug_timer_id);
        uhci_hotplug_timer_id = 0;
        LOG_INFO_MSG("uhci: Hot-plug monitor stopped\n");
    }
}

