# Design Document: Test Module Refactoring

## Overview

本设计文档描述 CastorOS 测试模块的重构方案，目标是建立一个统一、模块化、可扩展的测试框架。重构将保留现有的 ktest 和 PBT 框架核心功能，同时增强模块化组织、多架构支持、测试覆盖率追踪等能力。

### 设计目标

1. **模块化**: 测试按子系统组织，每个模块独立自包含
2. **统一性**: 所有测试遵循相同的接口和命名规范
3. **可扩展性**: 新增测试模块无需修改核心框架
4. **多架构兼容**: 支持 i686、x86_64、ARM64 三种架构
5. **可追溯性**: 测试与需求之间有明确的映射关系

## Architecture

### 测试框架层次结构

```
┌─────────────────────────────────────────────────────────────────┐
│                      Test Runner (test_runner.c)                │
│  - 注册和执行所有测试模块                                         │
│  - 汇总统计信息                                                  │
│  - 架构信息报告                                                  │
├─────────────────────────────────────────────────────────────────┤
│                    Test Framework Layer                          │
│  ┌─────────────────────┐  ┌─────────────────────┐               │
│  │   ktest (单元测试)   │  │   PBT (属性测试)    │               │
│  │  - TEST_CASE        │  │  - PBT_PROPERTY     │               │
│  │  - TEST_SUITE       │  │  - Generators       │               │
│  │  - ASSERT_*         │  │  - PBT_ASSERT       │               │
│  └─────────────────────┘  └─────────────────────┘               │
├─────────────────────────────────────────────────────────────────┤
│                    Test Modules by Subsystem                     │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐           │
│  │    mm    │ │    fs    │ │   net    │ │ drivers  │           │
│  │ pmm_test │ │ vfs_test │ │ ip_test  │ │ pci_test │           │
│  │ vmm_test │ │fat32_test│ │ tcp_test │ │timer_test│           │
│  │heap_test │ │ramfs_test│ │ arp_test │ │serial_tst│           │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘           │
│  ┌──────────┐ ┌──────────┐                                      │
│  │  kernel  │ │   arch   │                                      │
│  │task_test │ │hal_test  │                                      │
│  │sync_test │ │pgtable_ts│                                      │
│  │syscall_ts│ │context_ts│                                      │
│  └──────────┘ └──────────┘                                      │
└─────────────────────────────────────────────────────────────────┘
```

### 目录结构

```
src/tests/
├── ktest.c                    # 核心单元测试框架
├── test_runner.c              # 测试运行器
├── pbt/
│   └── pbt.c                  # 属性测试框架
├── common/
│   ├── test_utils.c           # 共享测试工具
│   └── mock_io.c              # Mock I/O 支持
├── mm/                        # 内存管理测试
│   ├── pmm_test.c
│   ├── vmm_test.c
│   ├── heap_test.c
│   └── cow_test.c
├── fs/                        # 文件系统测试
│   ├── vfs_test.c
│   ├── fat32_test.c
│   ├── ramfs_test.c
│   └── devfs_test.c
├── net/                       # 网络栈测试
│   ├── checksum_test.c
│   ├── ip_test.c
│   ├── tcp_test.c
│   ├── arp_test.c
│   └── netbuf_test.c
├── drivers/                   # 驱动测试
│   ├── pci_test.c
│   ├── timer_test.c
│   └── serial_test.c
├── kernel/                    # 内核核心测试
│   ├── task_test.c
│   ├── sync_test.c
│   └── syscall_test.c
└── arch/                      # 架构相关测试
    ├── hal_test.c
    ├── pgtable_test.c
    └── context_test.c
```

## Components and Interfaces

### 1. 测试模块接口 (test_module.h)

```c
// 测试模块元数据
typedef struct {
    const char *name;           // 模块名称
    const char *subsystem;      // 所属子系统 (mm, fs, net, etc.)
    void (*run_func)(void);     // 运行函数
    const char **dependencies;  // 依赖的其他模块
    uint32_t dep_count;         // 依赖数量
    bool is_slow;               // 是否为慢测试
} test_module_t;

// 模块注册宏
#define TEST_MODULE(name, subsystem, run_func) \
    static const test_module_t __test_module_##name = { \
        .name = #name, \
        .subsystem = #subsystem, \
        .run_func = run_func, \
        .dependencies = NULL, \
        .dep_count = 0, \
        .is_slow = false \
    }

// 带依赖的模块注册
#define TEST_MODULE_WITH_DEPS(name, subsystem, run_func, deps, dep_count) \
    static const test_module_t __test_module_##name = { \
        .name = #name, \
        .subsystem = #subsystem, \
        .run_func = run_func, \
        .dependencies = deps, \
        .dep_count = dep_count, \
        .is_slow = false \
    }
```

### 2. 增强的 PBT 生成器 (pbt_generators.h)

```c
// 内存地址生成器
paddr_t pbt_gen_paddr(pbt_state_t *state);
paddr_t pbt_gen_paddr_aligned(pbt_state_t *state);
vaddr_t pbt_gen_vaddr_user(pbt_state_t *state);
vaddr_t pbt_gen_vaddr_kernel(pbt_state_t *state);

// 页表项生成器
pte_t pbt_gen_pte(pbt_state_t *state);
pte_t pbt_gen_pte_with_flags(pbt_state_t *state, uint32_t required_flags);

// 缓冲区生成器
void pbt_gen_buffer(pbt_state_t *state, void *buf, size_t size);
char *pbt_gen_string(pbt_state_t *state, size_t max_len);

// 网络数据生成器
uint32_t pbt_gen_ip_addr(pbt_state_t *state);
uint16_t pbt_gen_port(pbt_state_t *state);
```

### 3. 属性模式宏 (pbt_patterns.h)

```c
// 往返属性模式
#define PBT_ROUNDTRIP(encode, decode, gen, eq) \
    PBT_PROPERTY(roundtrip_##encode##_##decode) { \
        auto input = gen(state); \
        auto encoded = encode(input); \
        auto decoded = decode(encoded); \
        PBT_ASSERT(eq(input, decoded)); \
    }

// 不变量属性模式
#define PBT_INVARIANT(operation, invariant, gen) \
    PBT_PROPERTY(invariant_##operation) { \
        auto input = gen(state); \
        auto before = invariant(input); \
        operation(input); \
        auto after = invariant(input); \
        PBT_ASSERT_EQ(before, after); \
    }

// 幂等性属性模式
#define PBT_IDEMPOTENT(operation, eq, gen) \
    PBT_PROPERTY(idempotent_##operation) { \
        auto input = gen(state); \
        auto once = operation(input); \
        auto twice = operation(once); \
        PBT_ASSERT(eq(once, twice)); \
    }
```

### 4. 测试运行器增强 (test_runner.h)

```c
// 运行选项
typedef struct {
    const char *filter_module;    // 只运行指定模块
    const char *filter_subsystem; // 只运行指定子系统
    bool include_slow;            // 包含慢测试
    bool stop_on_failure;         // 失败时停止
    bool verbose;                 // 详细输出
} test_run_options_t;

// 运行函数
void run_all_tests(void);
void run_tests_with_options(const test_run_options_t *options);
void run_subsystem_tests(const char *subsystem);
void run_module_tests(const char *module_name);

// 统计查询
test_stats_t get_global_test_stats(void);
test_stats_t get_module_stats(const char *module_name);
```

### 5. Mock I/O 框架 (mock_io.h)

```c
// Mock I/O 端口
void mock_io_init(void);
void mock_io_set_port_value(uint16_t port, uint32_t value);
uint32_t mock_io_get_port_value(uint16_t port);
void mock_io_expect_write(uint16_t port, uint32_t value);
void mock_io_verify_expectations(void);

// Mock MMIO
void mock_mmio_init(void);
void mock_mmio_map_region(uintptr_t base, size_t size);
void mock_mmio_set_value(uintptr_t addr, uint32_t value);
uint32_t mock_mmio_get_value(uintptr_t addr);
```

## Data Models

### 测试统计结构

```c
typedef struct {
    uint32_t total_modules;      // 总模块数
    uint32_t passed_modules;     // 通过的模块数
    uint32_t failed_modules;     // 失败的模块数
    uint32_t skipped_modules;    // 跳过的模块数
    
    uint32_t total_suites;       // 总套件数
    uint32_t passed_suites;      // 通过的套件数
    uint32_t failed_suites;      // 失败的套件数
    
    uint32_t total_cases;        // 总用例数
    uint32_t passed_cases;       // 通过的用例数
    uint32_t failed_cases;       // 失败的用例数
    
    uint32_t total_assertions;   // 总断言数
    uint32_t passed_assertions;  // 通过的断言数
    uint32_t failed_assertions;  // 失败的断言数
    
    uint32_t pbt_properties;     // PBT 属性数
    uint32_t pbt_iterations;     // PBT 迭代总数
    
    uint64_t execution_time_ms;  // 执行时间（毫秒）
} test_stats_t;
```

### 测试模块注册表

```c
typedef struct {
    test_module_t *modules;      // 模块数组
    uint32_t count;              // 模块数量
    uint32_t capacity;           // 数组容量
} test_registry_t;
```

## Correctness Properties

*A property is a characteristic or behavior that should hold true across all valid executions of a system-essentially, a formal statement about what the system should do. Properties serve as the bridge between human-readable specifications and machine-verifiable correctness guarantees.*

Based on the prework analysis, the following correctness properties have been identified:

### Property 1: Test Result Format Consistency

*For any* test execution across different architectures (i686, x86_64, ARM64), the test runner output format SHALL be consistent, containing architecture name, test counts, and pass/fail status.

**Validates: Requirements 1.4, 7.1**

### Property 2: PBT Custom Generator Validity

*For any* custom generator (pbt_gen_paddr, pbt_gen_vaddr_user, pbt_gen_pte), the generated values SHALL be within valid ranges for the target architecture (page-aligned addresses, valid PTE flags).

**Validates: Requirements 2.2**

### Property 3: PBT-ktest Statistics Integration

*For any* PBT test execution, the PBT statistics (properties tested, iterations run) SHALL be reflected in the unified ktest statistics report.

**Validates: Requirements 2.3**

### Property 4: PMM Allocation Alignment and Uniqueness

*For any* sequence of PMM frame allocations without intervening frees, all returned addresses SHALL be page-aligned and unique.

**Validates: Requirements 3.1**

### Property 5: VMM Map-Query Round-Trip

*For any* valid virtual address `virt`, physical address `phys`, and flags `flags`, after `vmm_map_page(virt, phys, flags)` succeeds, querying the mapping SHALL return `phys`.

**Validates: Requirements 3.2**

### Property 6: Heap Allocation Alignment

*For any* heap allocation request of size `n > 0`, the returned pointer SHALL be 4-byte aligned.

**Validates: Requirements 3.3**

### Property 7: COW Reference Count Consistency

*For any* page with COW enabled, after `n` clone operations and `m` free operations where `n >= m`, the reference count SHALL be `1 + n - m`.

**Validates: Requirements 3.4**

### Property 8: Memory Leak Detection

*For any* sequence of memory operations (allocate, map, free, unmap), the PMM free frame count before and after SHALL be equal (within tolerance for internal bookkeeping).

**Validates: Requirements 3.5**

### Property 9: VFS Read-Write Round-Trip

*For any* valid file path and data buffer, writing data to a file and reading it back SHALL return identical data.

**Validates: Requirements 4.2**

### Property 10: Ramfs Create-Delete Consistency

*For any* file created in ramfs, the file SHALL be accessible until deleted, and inaccessible after deletion.

**Validates: Requirements 4.4**

### Property 11: ARP Table Lookup Round-Trip

*For any* IP address and MAC address pair added to the ARP table, looking up the IP address SHALL return the corresponding MAC address.

**Validates: Requirements 5.4**

### Property 12: Netbuf Data Integrity

*For any* data written to a netbuf, reading from the same offset SHALL return identical data.

**Validates: Requirements 5.5**

### Property 13: Mock PCI Config Space Round-Trip

*For any* PCI configuration space write operation, reading from the same offset SHALL return the written value.

**Validates: Requirements 6.2**

### Property 14: Mock Serial Character Round-Trip

*For any* character transmitted through the mock serial driver, receiving SHALL return the same character.

**Validates: Requirements 6.4**

### Property 15: Page Table Level Correctness

*For any* architecture, the page table level count SHALL match the expected value (2 for i686, 4 for x86_64/ARM64).

**Validates: Requirements 7.4**

### Property 16: Test Hierarchy Structure

*For any* test execution, the framework SHALL maintain a three-level hierarchy (module → suite → case) with correct parent-child relationships.

**Validates: Requirements 12.1**

## Error Handling

### 测试失败处理

1. **断言失败**: 记录失败位置、期望值、实际值，继续执行当前测试用例的后续断言
2. **PBT 属性失败**: 记录失败种子、迭代次数、收缩后的反例，停止当前属性测试
3. **测试崩溃**: 捕获异常，记录崩溃位置，标记测试为失败，继续下一个测试
4. **依赖失败**: 如果依赖模块失败，跳过依赖它的模块

### 错误报告格式

```
================================================================================
TEST FAILURE DIAGNOSTICS
================================================================================
Test:     test_vmm_map_page_basic
Location: src/tests/mm/vmm_test.c:42
Architecture: i686 (32-bit)

Assertion failed: expected == actual
    Expected: 0x1000
    Actual:   0x0
    
Debugging Hints:
  - Check 32-bit address calculations
  - Verify 2-level page table operations
================================================================================
```

## Testing Strategy

### 双重测试方法

本设计采用单元测试和属性测试相结合的方法：

1. **单元测试 (ktest)**
   - 验证特定示例和边界情况
   - 测试错误处理路径
   - 验证集成点

2. **属性测试 (PBT)**
   - 验证应对所有有效输入成立的通用属性
   - 使用随机生成的输入发现边界情况
   - 每个属性至少运行 100 次迭代

### 测试框架选择

- **单元测试**: 使用现有的 ktest 框架
- **属性测试**: 使用现有的 PBT 框架，增强生成器支持

### 测试标注要求

每个属性测试必须包含以下注释：

```c
/**
 * **Feature: test-refactor, Property N: Property Name**
 * **Validates: Requirements X.Y**
 */
```

### 测试覆盖目标

| 子系统 | 当前覆盖 | 目标覆盖 | 优先级 |
|--------|----------|----------|--------|
| mm     | 高       | 高       | P0     |
| kernel | 中       | 高       | P0     |
| arch   | 中       | 高       | P0     |
| fs     | 低       | 中       | P1     |
| net    | 低       | 中       | P1     |
| drivers| 低       | 中       | P2     |

### 新增测试模块清单

#### P0 - 核心模块（必须）

1. **mm/cow_test.c** - COW 机制测试
2. **kernel/syscall_error_test.c** - 系统调用错误处理测试（已存在，需完善）

#### P1 - 重要模块（推荐）

3. **fs/vfs_test.c** - VFS 层测试
4. **fs/ramfs_test.c** - ramfs 测试
5. **net/checksum_test.c** - 校验和测试
6. **net/netbuf_test.c** - 网络缓冲区测试

#### P2 - 扩展模块（可选）

7. **fs/fat32_test.c** - FAT32 测试
8. **fs/devfs_test.c** - devfs 测试
9. **net/ip_test.c** - IP 协议测试
10. **net/tcp_test.c** - TCP 协议测试
11. **net/arp_test.c** - ARP 测试
12. **drivers/pci_test.c** - PCI 驱动测试
13. **drivers/timer_test.c** - 定时器测试
14. **drivers/serial_test.c** - 串口测试
