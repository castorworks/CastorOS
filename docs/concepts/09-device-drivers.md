# 设备驱动

## 概述

设备驱动是操作系统与硬件之间的接口。CastorOS 支持多种设备类型，包括字符设备、块设备和网络设备。

## 设备分类

```
设备类型
├── 字符设备 (Character Device)
│   ├── 串口 (Serial)
│   ├── 键盘 (Keyboard)
│   ├── 终端 (TTY)
│   └── RTC (实时时钟)
│
├── 块设备 (Block Device)
│   ├── ATA/IDE 硬盘
│   └── RAM 磁盘
│
└── 网络设备 (Network Device)
    └── Intel E1000 网卡
```

## I/O 访问方式

### 端口 I/O (PIO)

x86 使用独立的 I/O 地址空间：

```c
// 读端口
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// 写端口
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}
```

### 内存映射 I/O (MMIO)

设备寄存器映射到内存地址空间：

```c
// MMIO 读写（需要防止编译器优化）
static inline uint32_t mmio_read32(volatile void *addr) {
    return *(volatile uint32_t *)addr;
}

static inline void mmio_write32(volatile void *addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}

// 映射 MMIO 区域
void *map_mmio(uint32_t phys_addr, size_t size) {
    uint32_t virt = allocate_mmio_region(size);
    
    for (size_t i = 0; i < size; i += PAGE_SIZE) {
        vmm_map_page(virt + i, phys_addr + i,
                     PAGE_PRESENT | PAGE_WRITE | PAGE_PCD);
    }
    
    return (void *)virt;
}
```

## PCI 总线

### PCI 配置空间

```c
// PCI 配置空间访问
#define PCI_CONFIG_ADDR  0xCF8
#define PCI_CONFIG_DATA  0xCFC

uint32_t pci_read_config(uint8_t bus, uint8_t device, 
                         uint8_t func, uint8_t offset) {
    uint32_t addr = (1 << 31)           // Enable bit
                  | (bus << 16)
                  | (device << 11)
                  | (func << 8)
                  | (offset & 0xFC);
    
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

void pci_write_config(uint8_t bus, uint8_t device,
                      uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t addr = (1 << 31)
                  | (bus << 16)
                  | (device << 11)
                  | (func << 8)
                  | (offset & 0xFC);
    
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, value);
}
```

### PCI 设备结构

```c
typedef struct pci_device {
    uint8_t bus, device, func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type;
    uint32_t bar[6];          // Base Address Registers
    uint8_t irq_line;
} pci_device_t;

// 扫描 PCI 总线
void pci_scan_devices(void) {
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int func = 0; func < 8; func++) {
                uint32_t id = pci_read_config(bus, dev, func, 0);
                if ((id & 0xFFFF) == 0xFFFF) continue;
                
                pci_device_t *device = kmalloc(sizeof(pci_device_t));
                device->bus = bus;
                device->device = dev;
                device->func = func;
                device->vendor_id = id & 0xFFFF;
                device->device_id = id >> 16;
                
                // 读取其他信息...
                pci_register_device(device);
            }
        }
    }
}
```

## 常见驱动实现

### 串口驱动 (Serial)

```c
#define COM1_PORT 0x3F8

void serial_init(void) {
    // 禁用中断
    outb(COM1_PORT + 1, 0x00);
    
    // 设置波特率 (115200)
    outb(COM1_PORT + 3, 0x80);    // 启用 DLAB
    outb(COM1_PORT + 0, 0x01);    // 分频低字节
    outb(COM1_PORT + 1, 0x00);    // 分频高字节
    
    // 8N1 格式
    outb(COM1_PORT + 3, 0x03);
    
    // 启用 FIFO
    outb(COM1_PORT + 2, 0xC7);
    
    // 启用中断
    outb(COM1_PORT + 4, 0x0B);
    outb(COM1_PORT + 1, 0x01);
}

void serial_putchar(char c) {
    // 等待发送缓冲区空
    while ((inb(COM1_PORT + 5) & 0x20) == 0);
    outb(COM1_PORT, c);
}

char serial_getchar(void) {
    // 等待接收数据
    while ((inb(COM1_PORT + 5) & 0x01) == 0);
    return inb(COM1_PORT);
}
```

### 键盘驱动

```c
#define KBD_DATA    0x60
#define KBD_STATUS  0x64
#define KBD_CMD     0x64

static char scancode_to_ascii[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', // ...
};

static volatile char kbd_buffer[256];
static volatile int kbd_head = 0, kbd_tail = 0;

void keyboard_handler(registers_t *regs) {
    (void)regs;
    
    uint8_t scancode = inb(KBD_DATA);
    
    // 忽略释放事件
    if (scancode & 0x80) return;
    
    char c = scancode_to_ascii[scancode];
    if (c) {
        kbd_buffer[kbd_head] = c;
        kbd_head = (kbd_head + 1) % 256;
        
        // 唤醒等待的进程
        wake_up(&kbd_waiters);
    }
}

char keyboard_read(void) {
    while (kbd_head == kbd_tail) {
        wait_on(&kbd_waiters, NULL);
    }
    
    char c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % 256;
    return c;
}
```

### ATA/IDE 驱动

```c
#define ATA_PRIMARY_BASE     0x1F0
#define ATA_PRIMARY_CTRL     0x3F6

#define ATA_REG_DATA         0
#define ATA_REG_ERROR        1
#define ATA_REG_SECCOUNT     2
#define ATA_REG_LBA0         3
#define ATA_REG_LBA1         4
#define ATA_REG_LBA2         5
#define ATA_REG_DRIVE        6
#define ATA_REG_STATUS       7
#define ATA_REG_CMD          7

#define ATA_CMD_READ_PIO     0x20
#define ATA_CMD_WRITE_PIO    0x30
#define ATA_CMD_IDENTIFY     0xEC

#define ATA_STATUS_BSY       0x80
#define ATA_STATUS_DRQ       0x08
#define ATA_STATUS_ERR       0x01

// 等待设备就绪
static void ata_wait_ready(uint16_t base) {
    while (inb(base + ATA_REG_STATUS) & ATA_STATUS_BSY);
}

// 读扇区
int ata_read_sector(int drive, uint32_t lba, void *buf) {
    uint16_t base = ATA_PRIMARY_BASE;
    
    ata_wait_ready(base);
    
    // 选择驱动器和 LBA 模式
    outb(base + ATA_REG_DRIVE, 0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
    
    // 设置扇区数和 LBA
    outb(base + ATA_REG_SECCOUNT, 1);
    outb(base + ATA_REG_LBA0, lba & 0xFF);
    outb(base + ATA_REG_LBA1, (lba >> 8) & 0xFF);
    outb(base + ATA_REG_LBA2, (lba >> 16) & 0xFF);
    
    // 发送读命令
    outb(base + ATA_REG_CMD, ATA_CMD_READ_PIO);
    
    // 等待数据就绪
    while (!(inb(base + ATA_REG_STATUS) & ATA_STATUS_DRQ));
    
    // 读取数据
    uint16_t *ptr = (uint16_t *)buf;
    for (int i = 0; i < 256; i++) {
        ptr[i] = inw(base + ATA_REG_DATA);
    }
    
    return 0;
}
```

### 定时器驱动 (PIT)

```c
#define PIT_FREQ     1193182
#define PIT_CHANNEL0 0x40
#define PIT_CMD      0x43

static volatile uint64_t ticks = 0;

void pit_init(uint32_t freq) {
    uint32_t divisor = PIT_FREQ / freq;
    
    // 通道 0，模式 3，二进制
    outb(PIT_CMD, 0x36);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
    
    irq_register_handler(0, timer_handler);
    irq_enable_line(0);
}

static void timer_handler(registers_t *regs) {
    (void)regs;
    ticks++;
    
    // 更新当前任务时间片
    if (current_task) {
        current_task->time_slice--;
        if (current_task->time_slice == 0) {
            schedule();
        }
    }
}

uint64_t timer_get_ticks(void) {
    return ticks;
}

void timer_sleep(uint32_t ms) {
    uint64_t target = ticks + (ms * TICKS_PER_SECOND / 1000);
    while (ticks < target) {
        __asm__ volatile ("hlt");
    }
}
```

### Intel E1000 网卡驱动

```c
// E1000 寄存器偏移
#define E1000_CTRL   0x0000
#define E1000_STATUS 0x0008
#define E1000_EERD   0x0014
#define E1000_ICR    0x00C0
#define E1000_IMS    0x00D0
#define E1000_RCTL   0x0100
#define E1000_TCTL   0x0400
#define E1000_RDBAL  0x2800
#define E1000_RDBAH  0x2804
#define E1000_RDLEN  0x2808
#define E1000_RDH    0x2810
#define E1000_RDT    0x2818
#define E1000_TDBAL  0x3800
#define E1000_TDBAH  0x3804
#define E1000_TDLEN  0x3808
#define E1000_TDH    0x3810
#define E1000_TDT    0x3818

typedef struct e1000_device {
    pci_device_t *pci;
    volatile uint32_t *mmio_base;
    
    // 接收描述符环
    e1000_rx_desc_t *rx_descs;
    uint32_t rx_cur;
    
    // 发送描述符环
    e1000_tx_desc_t *tx_descs;
    uint32_t tx_cur;
    
    uint8_t mac[6];
} e1000_device_t;

// 寄存器访问
static inline uint32_t e1000_read(e1000_device_t *dev, uint32_t reg) {
    return dev->mmio_base[reg / 4];
}

static inline void e1000_write(e1000_device_t *dev, uint32_t reg, uint32_t val) {
    dev->mmio_base[reg / 4] = val;
}

// 发送数据包
int e1000_send(e1000_device_t *dev, void *data, size_t len) {
    uint32_t tail = e1000_read(dev, E1000_TDT);
    
    e1000_tx_desc_t *desc = &dev->tx_descs[tail];
    memcpy(desc->buffer, data, len);
    desc->length = len;
    desc->cmd = E1000_TXDESC_CMD_EOP | E1000_TXDESC_CMD_IFCS | E1000_TXDESC_CMD_RS;
    desc->status = 0;
    
    tail = (tail + 1) % TX_DESC_COUNT;
    e1000_write(dev, E1000_TDT, tail);
    
    // 等待发送完成
    while (!(desc->status & E1000_TXDESC_STATUS_DD));
    
    return 0;
}

// 接收中断处理
void e1000_receive_handler(e1000_device_t *dev) {
    while (dev->rx_descs[dev->rx_cur].status & E1000_RXDESC_STATUS_DD) {
        e1000_rx_desc_t *desc = &dev->rx_descs[dev->rx_cur];
        
        // 处理接收到的数据包
        netdev_rx(dev->netdev, desc->buffer, desc->length);
        
        desc->status = 0;
        e1000_write(dev, E1000_RDT, dev->rx_cur);
        
        dev->rx_cur = (dev->rx_cur + 1) % RX_DESC_COUNT;
    }
}
```

## ACPI 驱动

### 关机实现

```c
void acpi_poweroff(void) {
    // 读取 FADT 中的 PM1a_CNT_BLK
    uint16_t pm1a_cnt = acpi_info.fadt->Pm1aCntBlk;
    uint16_t slp_typa = acpi_info.slp_typa;
    
    // 写入 SLP_TYPa | SLP_EN
    outw(pm1a_cnt, slp_typa | (1 << 13));
    
    // 如果有 PM1b，也写入
    if (acpi_info.fadt->Pm1bCntBlk) {
        outw(acpi_info.fadt->Pm1bCntBlk, 
             acpi_info.slp_typb | (1 << 13));
    }
}
```

## 中断驱动 vs 轮询

### 轮询模式

```c
// 简单但效率低
while (1) {
    if (device_has_data()) {
        process_data();
    }
    // CPU 忙等待，浪费资源
}
```

### 中断驱动模式

```c
// 高效，CPU 可做其他事
void device_init(void) {
    irq_register_handler(device_irq, device_handler);
    irq_enable_line(device_irq);
}

void device_handler(registers_t *regs) {
    if (device_has_data()) {
        process_data();
    }
    // 发送 EOI 后返回
}

// 主循环可以处理其他任务
while (1) {
    process_other_tasks();
    __asm__ volatile ("hlt");  // 等待中断
}
```

## 驱动模型最佳实践

1. **延迟初始化**: 只在需要时初始化设备
2. **错误处理**: 检测并处理所有硬件错误
3. **超时机制**: 避免无限等待
4. **电源管理**: 支持设备挂起/恢复
5. **日志记录**: 记录关键操作便于调试
6. **资源清理**: 驱动卸载时释放所有资源

