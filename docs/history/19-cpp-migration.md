# C++ 重构

CastorOS 最初用 C（`-std=gnu99`）编写，后来整体迁移到 freestanding C++20。
本章说明迁移后的代码约定，以及阅读前面各章（它们的示例代码仍是迁移前的 C 写法）时需要做的名字对应。

## 构建

内核与用户态都用交叉 `g++` 编译：

```
-std=gnu++20 -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics
```

- 不使用异常和 RTTI，没有 libstdc++ / libsupc++。
- 各 Makefile 用 `-MMD -MP` 生成头文件依赖，改头文件后相关 `.o` 会自动重编。
- 引导、中断入口、上下文切换、系统调用入口仍是汇编（x86 用 NASM，ARM64 用 GNU as）。

## C++ 运行时

语言本身需要的最小运行时由我们自己提供：

| 位置 | 内容 |
|------|------|
| `src/lib/cxxrt.cpp` | 内核：全局构造函数调用、`operator new/delete`（基于 `kmalloc/kfree`）、placement new、`__cxa_pure_virtual` 等 ABI 符号 |
| `user/lib/src/crt0.cpp` | 用户态：`_start` 先运行全局构造函数，再调用 `main()`，最后 `exit()` |

链接脚本负责收集构造函数表：aarch64-elf 工具链使用 `.init_array`，i686-elf / x86_64-elf 使用 `.ctors`，
两者分别由 `__init_array_start/end` 和 `__ctors_start/end` 标出。

内核在 `kernel_main` 的第一行调用 `cxx_global_ctors_init()`，此时堆还没有初始化，
所以**全局对象的构造函数里不能分配内存**。内核永不退出，全局对象的析构函数不会运行。

## 与汇编的边界

C++ 会对函数名做名字修饰，汇编看不到修饰后的名字。凡是汇编调用的 C++ 函数、或 C++ 调用的汇编符号，
都要声明为 `extern "C"`：

```cpp
extern "C" void kernel_main(multiboot_info_t *mbi);   // boot.asm 调用
extern "C" void task_switch_context(cpu_context_t **old_ctx, cpu_context_t *new_ctx);  // 汇编实现
```

编译器会隐式生成对 `memcpy` / `memset` / `memmove` / `memcmp` 的调用，所以这四个函数也是 C 链接。

## 命名约定

子系统按“命名空间 + 类”组织。单例性质的模块用静态成员函数：

| 迁移前 | 迁移后 |
|--------|--------|
| `pmm_alloc_frame()` | `mm::Pmm::alloc_frame()` |
| `vmm_map_page(...)` | `mm::Vmm::map_page(...)` |
| `heap_init(...)` | `mm::Heap::init(...)` |
| `vfs_open(...)` | `fs::Vfs::open(...)` |
| `tcp_input(...)` | `net::Tcp::input(...)` |
| `task_yield()` | `kernel::Scheduler::yield()` |
| `timer_get_uptime_ms()` | `drivers::Timer::get_uptime_ms()` |
| `hal_cpu_init()` | `hal::Cpu::init()` |
| `sys_read(...)` | `syscall::Fs::read(...)` |
| `interrupts_disable()` | `kernel::Interrupts::disable()` |

带数据的类型把操作并入结构体，作为静态成员，指针参数保持对 `NULL` 安全：

```cpp
net::Netbuf *buf = net::Netbuf::alloc(size);
net::Netbuf::push(buf, sizeof(eth_header_t));
net::Netbuf::free(buf);          // 与 kfree 一样，传 NULL 是安全的
```

同步原语是真正的成员函数：

```cpp
sync::Spinlock lock;
lock.init();
lock.lock();
lock.unlock();
```

以下接口保持为全局函数：`kmalloc` / `kfree`、`kprintf` / 日志宏、字符串函数，以及用户态的 POSIX 风格 API。

HAL（`hal::Cpu`、`hal::Mmu`、`hal::Interrupt` ...）仍然是编译期按架构选择实现：
每个架构在 `src/arch/<arch>/` 下定义同一组静态成员函数，没有虚函数开销。

## RAII 锁守卫

优先使用守卫，而不是手写 lock/unlock：

```cpp
void *kmalloc(size_t size) {
    sync::SpinlockIrqGuard guard(heap_lock);   // 关中断 + 加锁
    if (!size) return nullptr;                 // 任何 return 都会自动解锁并恢复中断
    ...
}
```

- `sync::SpinlockIrqGuard`：保存中断状态、关中断并加锁，析构时恢复。
- `sync::LockGuard`：通用守卫，适用于 `Spinlock` 和 `Mutex`（`sync::SpinlockGuard` / `sync::MutexGuard` 是它的别名）。
- 只需要保护函数中的一段代码时，用一对花括号限定守卫的作用域。

少数加锁点仍是手写的：它们在持锁期间会 `break` / `continue`、解锁后再加锁，或者在解锁后才调用可能再次取锁的函数。

## 多态：虚接口 + 无状态实现对象

原先靠函数指针表实现的多态改成了虚函数接口：

| 接口 | 用途 | 实现举例 |
|------|------|----------|
| `fs::NodeOps` | VFS 节点操作（read/write/readdir/...） | `RamfsFileOps`、`DevnullOps`、`ProcfsMeminfoOps` |
| `fs::BlockdevOps` | 块设备读写 | `AtaBlockdevOps`、`PartitionBlockdevOps` |
| `net::NetdevOps` | 网卡 open/close/transmit | `E1000NetdevOps` |

节点和设备本身仍是普通结构体（它们由 `kmalloc` + `memset` 创建，不能带虚表指针），
只保存一个指向实现对象的 `ops` 指针。实现对象是没有数据成员的静态常量，由编译器静态初始化：

```cpp
class DevnullOps final : public fs::NodeOps {
public:
    uint32_t supported() const override { return OP_READ | OP_WRITE; }
    uint32_t read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) const override {
        return devnull_read(node, offset, size, buffer);
    }
    ...
};
static const DevnullOps devnull_ops{};

node->ops = &devnull_ops;
```

VFS 用 `fs::node_supports(node, fs::NodeOps::OP_READ)` 取代了原来“函数指针是否为 NULL”的判断。

## 容易踩的坑

1. **成员函数里的同名调用**：在 `net::Ip::checksum()` 里写 `checksum(...)` 会调用它自己而不是全局的
   `checksum()`。要调用同名的全局函数，写成 `::checksum(...)`。
2. **块作用域的 `extern` 声明**：在某个命名空间的类成员函数里写 `extern void foo();`，
   声明的是该命名空间里的 `foo`，链接时找不到。应当包含对应的头文件。
3. **`bool` 是真正的布尔类型**：迁移前 `bool` 是 `unsigned char`，`bool b = flags & 0x100;` 会被截断成 0；现在结果是 `true`。
4. **稀疏的数组指定初始化**（`[5] = x, [9] = y`）C++ 不支持，改用 `switch` 或按顺序初始化。
5. **`volatile` 变量的 `++` / `--`** 在 C++20 中已弃用，写成 `x = x + 1`。

## 测试

- 内核单元测试新增了 “C++ Runtime Tests”（全局构造、`new/delete`、虚函数、placement new）。
- 用户态测试程序 `tests.elf` 新增了全局构造和虚函数分发的检查。
- 完整跑一遍 x86 的内核测试约需半分钟到一分钟，`make test` 默认的 8 秒超时不够，
  可以用 `make test TEST_TIMEOUT=90 OUTPUT_LINES=5000`。
