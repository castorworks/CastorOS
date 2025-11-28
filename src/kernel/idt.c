// ============================================================================
// idt.c - 中断描述符表实现
// ============================================================================

#include <kernel/idt.h>
#include <kernel/gdt.h>
#include <lib/string.h>
#include <lib/klog.h>

/* IDT 表（256 个表项） */
static struct idt_entry idt_entries[256];
static struct idt_ptr idt_pointer;

/**
 * 设置 IDT 表项
 */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags) {
    idt_entries[num].base_low  = base & 0xFFFF;
    idt_entries[num].base_high = (base >> 16) & 0xFFFF;
    idt_entries[num].selector  = selector;
    idt_entries[num].zero      = 0;
    idt_entries[num].flags     = flags;
}

/**
 * 初始化 IDT
 */
void idt_init(void) {
    LOG_INFO_MSG("Initializing IDT...\n");

    /* 设置 IDT 指针 */
    idt_pointer.limit = sizeof(struct idt_entry) * 256 - 1;
    idt_pointer.base  = (uint32_t)&idt_entries;

    /* 清空 IDT */
    memset(&idt_entries, 0, sizeof(struct idt_entry) * 256);

    /* 注意：中断处理程序将在 isr.c 和 irq.c 中注册 */

    /* 加载 IDT */
    idt_flush((uint32_t)&idt_pointer);

    LOG_INFO_MSG("IDT initialized successfully\n");
    LOG_DEBUG_MSG("  IDT base: 0x%x\n", idt_pointer.base);
    LOG_DEBUG_MSG("  IDT limit: %u bytes\n", idt_pointer.limit + 1);
}
