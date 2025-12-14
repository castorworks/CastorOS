#ifndef _KERNEL_ELF_H_
#define _KERNEL_ELF_H_

#include <types.h>
#include <mm/vmm.h>

/**
 * ELF 可执行文件格式支持
 * 
 * 支持 32 位 i386 和 64 位 x86_64 ELF 格式
 */

/* ELF 魔数 */
#define ELF_MAGIC 0x464C457F  // "\x7FELF"

/* ELF 类型 */
#define ELF_CLASS_32    1  // 32-bit
#define ELF_CLASS_64    2  // 64-bit

/* 数据编码 */
#define ELF_DATA_LSB    1  // 小端
#define ELF_DATA_MSB    2  // 大端

/* 目标文件类型 */
#define ET_NONE         0  // 未知类型
#define ET_REL          1  // 可重定位文件
#define ET_EXEC         2  // 可执行文件
#define ET_DYN          3  // 共享目标文件
#define ET_CORE         4  // Core 文件

/* 机器类型 */
#define EM_386          3   // Intel 80386
#define EM_X86_64       62  // AMD x86-64
#define EM_AARCH64      183 // ARM64 (AArch64)

/* 版本 */
#define EV_CURRENT      1  // 当前版本

/* 段类型 */
#define PT_NULL         0  // 未使用
#define PT_LOAD         1  // 可加载段
#define PT_DYNAMIC      2  // 动态链接信息
#define PT_INTERP       3  // 解释器路径
#define PT_NOTE         4  // 辅助信息
#define PT_SHLIB        5  // 保留
#define PT_PHDR         6  // 程序头表
#define PT_TLS          7  // 线程局部存储

/* 段标志 */
#define PF_X            0x1  // 可执行
#define PF_W            0x2  // 可写
#define PF_R            0x4  // 可读

/**
 * ELF 文件头（32位）
 */
typedef struct {
    uint8_t  e_ident[16];    // 魔数和其他信息
    uint16_t e_type;         // 目标文件类型
    uint16_t e_machine;      // 机器类型
    uint32_t e_version;      // 版本
    uint32_t e_entry;        // 程序入口点虚拟地址
    uint32_t e_phoff;        // 程序头表偏移
    uint32_t e_shoff;        // 节头表偏移
    uint32_t e_flags;        // 处理器特定标志
    uint16_t e_ehsize;       // ELF 头大小
    uint16_t e_phentsize;    // 程序头表项大小
    uint16_t e_phnum;        // 程序头表项数量
    uint16_t e_shentsize;    // 节头表项大小
    uint16_t e_shnum;        // 节头表项数量
    uint16_t e_shstrndx;     // 节头字符串表索引
} __attribute__((packed)) elf32_ehdr_t;

/**
 * ELF 程序头（32位）
 */
typedef struct {
    uint32_t p_type;         // 段类型
    uint32_t p_offset;       // 段在文件中的偏移
    uint32_t p_vaddr;        // 段的虚拟地址
    uint32_t p_paddr;        // 段的物理地址（通常忽略）
    uint32_t p_filesz;       // 段在文件中的大小
    uint32_t p_memsz;        // 段在内存中的大小
    uint32_t p_flags;        // 段标志
    uint32_t p_align;        // 对齐
} __attribute__((packed)) elf32_phdr_t;

/**
 * ELF 文件头（64位）
 */
typedef struct {
    uint8_t  e_ident[16];    // 魔数和其他信息
    uint16_t e_type;         // 目标文件类型
    uint16_t e_machine;      // 机器类型
    uint32_t e_version;      // 版本
    uint64_t e_entry;        // 程序入口点虚拟地址
    uint64_t e_phoff;        // 程序头表偏移
    uint64_t e_shoff;        // 节头表偏移
    uint32_t e_flags;        // 处理器特定标志
    uint16_t e_ehsize;       // ELF 头大小
    uint16_t e_phentsize;    // 程序头表项大小
    uint16_t e_phnum;        // 程序头表项数量
    uint16_t e_shentsize;    // 节头表项大小
    uint16_t e_shnum;        // 节头表项数量
    uint16_t e_shstrndx;     // 节头字符串表索引
} __attribute__((packed)) elf64_ehdr_t;

/**
 * ELF 程序头（64位）
 * 注意：64位程序头的字段顺序与32位不同
 */
typedef struct {
    uint32_t p_type;         // 段类型
    uint32_t p_flags;        // 段标志（64位中位置不同）
    uint64_t p_offset;       // 段在文件中的偏移
    uint64_t p_vaddr;        // 段的虚拟地址
    uint64_t p_paddr;        // 段的物理地址（通常忽略）
    uint64_t p_filesz;       // 段在文件中的大小
    uint64_t p_memsz;        // 段在内存中的大小
    uint64_t p_align;        // 对齐
} __attribute__((packed)) elf64_phdr_t;

/**
 * 验证 ELF 文件头（自动检测32/64位）
 * @param elf_data ELF 数据指针
 * @return 成功返回 true
 */
bool elf_validate_header(const void *elf_data);

/**
 * 检查 ELF 是否为 64 位
 * @param elf_data ELF 数据指针
 * @return 64位返回 true，32位返回 false
 */
bool elf_is_64bit(const void *elf_data);

/**
 * 加载 ELF 文件到指定页目录（使用 uintptr_t 支持 32/64 位）
 * @param elf_data ELF 数据指针
 * @param size ELF 文件大小
 * @param page_dir 目标页目录
 * @param entry_point 输出参数：程序入口点地址
 * @param program_end 输出参数：程序加载的最高地址（可选，可为 NULL）
 * @return 成功返回 true
 */
bool elf_load(const void *elf_data, uint32_t size, 
              page_directory_t *page_dir, uintptr_t *entry_point,
              uintptr_t *program_end);

/**
 * 获取 ELF 入口点地址
 * @param elf_data ELF 数据指针
 * @return 入口点地址，失败返回 0
 */
uintptr_t elf_get_entry(const void *elf_data);

#endif // _KERNEL_ELF_H_
