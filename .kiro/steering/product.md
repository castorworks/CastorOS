# CastorOS

CastorOS is an educational operating system designed for learning and experimentation.

## Overview

- Hobby OS targeting i686 (x86 32-bit) with planned support for x86_64 and ARM64
- Higher-half kernel design with virtual address base at 0x80000000
- Multiboot-compliant bootloader support (GRUB)
- Written primarily in C (GNU99) with NASM assembly for architecture-specific code

## Key Features

- Physical and virtual memory management with paging
- Preemptive multitasking with process/thread support
- VFS layer with FAT32, ramfs, devfs, procfs support
- User-mode execution with system calls
- Synchronization primitives (spinlocks, mutexes, semaphores)
- Network stack (Ethernet, IP, TCP, UDP, DHCP, DNS)
- Device drivers (VGA, keyboard, timer, ATA, PCI, E1000, USB/UHCI)

## Documentation Language

Project documentation is primarily in Chinese (简体中文). Code comments mix Chinese and English.
