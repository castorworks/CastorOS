# CastorOS 测试目录结构

测试代码按子系统模块化组织，与 `test_module.h` 中定义的子系统分类对应。

## 目录结构

```
src/tests/
├── framework/          # 测试框架核心
│   ├── ktest.c         # 单元测试框架实现
│   ├── test_module.c   # 模块注册表实现
│   └── test_runner.c   # 统一测试运行器
├── lib/                # 库函数测试 (TEST_SUBSYSTEM_LIB)
│   ├── string_test.c
│   ├── kprintf_test.c
│   └── klog_test.c
├── mm/                 # 内存管理测试 (TEST_SUBSYSTEM_MM)
│   ├── pmm_test.c
│   ├── vmm_test.c
│   ├── heap_test.c
│   ├── mm_types_test.c
│   ├── pgtable_test.c
│   ├── cow_flag_test.c
│   └── dma_test.c
├── fs/                 # 文件系统测试 (TEST_SUBSYSTEM_FS)
│   ├── vfs_test.c
│   ├── ramfs_test.c
│   ├── fat32_test.c
│   └── devfs_test.c
├── net/                # 网络测试 (TEST_SUBSYSTEM_NET)
│   ├── checksum_test.c
│   ├── netbuf_test.c
│   ├── arp_test.c
│   ├── ip_test.c
│   └── tcp_test.c
├── kernel/             # 内核核心测试 (TEST_SUBSYSTEM_KERNEL)
│   ├── task_test.c
│   ├── sync_test.c
│   ├── syscall_test.c
│   ├── syscall_error_test.c
│   ├── fork_exec_test.c
│   └── usermode_test.c
├── drivers/            # 驱动测试 (TEST_SUBSYSTEM_DRIVERS)
│   ├── pci_test.c
│   ├── timer_test.c
│   └── serial_test.c
├── arch/               # 架构相关测试 (TEST_SUBSYSTEM_ARCH)
│   ├── hal_test.c
│   ├── arch_types_test.c
│   ├── interrupt_handler_test.c
│   ├── userlib_syscall_test.c
│   ├── i686/           # i686 特定测试
│   ├── x86_64/         # x86_64 特定测试
│   │   ├── isr64_test.c
│   │   └── paging64_test.c
│   └── arm64/          # ARM64 特定测试
│       ├── arm64_mmu_test.c
│       ├── arm64_exception_test.c
│       └── arm64_fault_test.c
├── pbt/                # 属性测试框架
│   └── pbt.c
└── examples/           # 示例测试
    └── ktest_example.c
```

## 头文件组织

头文件位于 `src/include/tests/` 下，目录结构与源文件对应：

- `tests/ktest.h` - 测试框架核心
- `tests/test_module.h` - 模块注册宏和类型
- `tests/test_runner.h` - 测试运行器接口
- `tests/<subsystem>/<module>_test.h` - 各模块测试头文件

## 添加新测试

1. 在对应子系统目录下创建 `xxx_test.c`
2. 在 `src/include/tests/<subsystem>/` 下创建 `xxx_test.h`
3. 在 `test_runner.c` 中添加 include 和 TEST_ENTRY
4. 使用 `TEST_MODULE_DESC` 宏注册模块
