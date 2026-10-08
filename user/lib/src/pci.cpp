/**
 * PCI 配置空间的访问（只有 x86，见 pci.h）
 */

#include <pci.h>
#include <syscall.h>

#if !defined(ARCH_ARM64)

#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

uint32_t pci_read(pci_dev_t dev, uint32_t off) {
    uint32_t v = 0;
    io_write(PCI_CONFIG_ADDRESS, 4, 0x80000000u | (dev << 8) | (off & 0xFC));
    io_read(PCI_CONFIG_DATA, 4, &v);
    return v;
}

void pci_write(pci_dev_t dev, uint32_t off, uint32_t value) {
    io_write(PCI_CONFIG_ADDRESS, 4, 0x80000000u | (dev << 8) | (off & 0xFC));
    io_write(PCI_CONFIG_DATA, 4, value);
}

bool pci_find_class(uint32_t class_code, pci_dev_t *dev) {
    for (uint32_t device = 0; device < 32; device++) {
        if ((pci_read(PCI_DEV(device, 0), PCI_ID) & 0xFFFF) == 0xFFFF) {
            continue;
        }
        // 只有声明了“多功能”的设备才有 1 到 7 号功能
        uint32_t functions = pci_read(PCI_DEV(device, 0), PCI_HEADER) & (1u << 23) ? 8 : 1;
        for (uint32_t function = 0; function < functions; function++) {
            pci_dev_t d = PCI_DEV(device, function);
            if ((pci_read(d, PCI_ID) & 0xFFFF) != 0xFFFF && (pci_read(d, PCI_CLASS) >> 8) == class_code) {
                *dev = d;
                return true;
            }
        }
    }
    return false;
}

uint32_t pci_bar_size(pci_dev_t dev, uint32_t bar) {
    uint32_t off = PCI_BAR0 + bar * 4;
    uint32_t value = pci_read(dev, off);
    bool io = value & 1;
    uint32_t command = pci_read(dev, PCI_COMMAND);
    pci_write(dev, PCI_COMMAND, command & ~(uint32_t)(PCI_COMMAND_IO | PCI_COMMAND_MEMORY));
    pci_write(dev, off, 0xFFFFFFFFu);
    uint32_t mask = pci_read(dev, off) & (io ? ~3u : ~0xFu);
    pci_write(dev, off, value);
    pci_write(dev, PCI_COMMAND, command);
    uint32_t size = ~mask + 1;
    return io ? size & 0xFFFF : size;
}

#endif
