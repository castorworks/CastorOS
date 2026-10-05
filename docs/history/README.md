# 历史开发记录

这里是 CastorOS 早期的分步开发记录，写于内核还是单内核（内核里带着 VGA、键盘、
文件系统、shell 等）、并且用 C 编写的时候。保留它们是因为其中的背景知识
（Multiboot、GDT/IDT、分页、上下文切换、特权级切换、同步原语）仍然适用。

但文中的代码清单、文件路径、启动流程和系统调用都已经和现在的代码对不上：
现在的内核是微内核，驱动和文件系统在用户态，用 QEMU `-kernel` 直接启动。
当前的结构以 [../microkernel.md](../microkernel.md) 为准。

| 文档 | 内容 |
|------|------|
| [01-boot](01-boot.md) | Multiboot 头、boot.asm、进入高半核 |
| [02-infrastructure](02-infrastructure.md) | GDT、IDT、ISR、IRQ |
| [03-mm](03-mm.md) | PMM、VMM、内核堆 |
| [05-task](05-task.md) | PCB、上下文切换、调度 |
| [09-usermode](09-usermode.md) | 特权级、进入用户态、ELF 加载 |
| [10-sync](10-sync.md) | 自旋锁、互斥锁、信号量 |
| [19-cpp-migration](19-cpp-migration.md) | 从 C 迁移到 C++ 的记录 |
