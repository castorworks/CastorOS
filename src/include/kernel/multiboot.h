#ifndef _KERNEL_MULTIBOOT_H_
#define _KERNEL_MULTIBOOT_H_

#include <types.h>

/* The magic number passed by the bootloader in %eax */
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/* Flags for multiboot_info->flags */
#define MULTIBOOT_INFO_MEM            0x001
#define MULTIBOOT_INFO_BOOTDEV        0x002
#define MULTIBOOT_INFO_CMDLINE        0x004
#define MULTIBOOT_INFO_MODS           0x008
#define MULTIBOOT_INFO_AOUT_SYMS      0x010
#define MULTIBOOT_INFO_ELF_SHDR       0x020
#define MULTIBOOT_INFO_MEM_MAP        0x040
#define MULTIBOOT_INFO_DRIVE_INFO     0x080
#define MULTIBOOT_INFO_CONFIG_TABLE   0x100
#define MULTIBOOT_INFO_BOOT_LOADER_NAME 0x200
#define MULTIBOOT_INFO_APM_TABLE      0x400
#define MULTIBOOT_INFO_VBE_INFO       0x800
#define MULTIBOOT_INFO_FRAMEBUFFER_INFO 0x1000

/* Memory map entry */
typedef struct multiboot_mmap_entry {
    uint32_t size;      /* Size of the entry (excluding this field) */
    uint64_t addr;      /* Base address */
    uint64_t len;       /* Length in bytes */
    #define MULTIBOOT_MEMORY_AVAILABLE              1
    #define MULTIBOOT_MEMORY_RESERVED               2
    #define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE       3
    #define MULTIBOOT_MEMORY_NVS                    4
    #define MULTIBOOT_MEMORY_BADRAM                 5
    uint32_t type;      /* Type of memory region */
} __attribute__((packed)) multiboot_memory_map_t;

/* Module structure */
typedef struct multiboot_mod_list {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;   /* Module command line */
    uint32_t pad;
} multiboot_module_t;

/* A.out symbol table (for legacy support) */
typedef struct multiboot_aout_symbol_table {
    uint32_t tabsize;
    uint32_t strsize;
    uint32_t addr;
    uint32_t reserved;
} multiboot_aout_symbol_table_t;

/* ELF section header table */
typedef struct multiboot_elf_section_header_table {
    uint32_t num;
    uint32_t size;
    uint32_t addr;
    uint32_t shndx;
} multiboot_elf_section_header_table_t;

/* Main multiboot info structure */
typedef struct multiboot_info {
    uint32_t flags;

    /* Available memory */
    uint32_t mem_lower;
    uint32_t mem_upper;

    /* Boot device */
    uint32_t boot_device;

    /* Command line */
    uint32_t cmdline;

    /* Modules */
    uint32_t mods_count;
    uint32_t mods_addr;

    union {
        multiboot_aout_symbol_table_t aout_sym;
        multiboot_elf_section_header_table_t elf_sec;
    } u;

    /* Memory map */
    uint32_t mmap_length;
    uint32_t mmap_addr;

    /* Drive info */
    uint32_t drives_length;
    uint32_t drives_addr;

    /* ROM configuration table */
    uint32_t config_table;

    /* Bootloader name */
    uint32_t boot_loader_name;

    /* APM table */
    uint32_t apm_table;

    /* VBE info */
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;

    /* Framebuffer info */
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    #define MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED 0
    #define MULTIBOOT_FRAMEBUFFER_TYPE_RGB     1
    #define MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT 2
    uint8_t framebuffer_type;
    union {
        struct {
            uint32_t framebuffer_palette_addr;
            uint16_t framebuffer_palette_num_colors;
        };
        struct {
            uint8_t framebuffer_red_field_position;
            uint8_t framebuffer_red_mask_size;
            uint8_t framebuffer_green_field_position;
            uint8_t framebuffer_green_mask_size;
            uint8_t framebuffer_blue_field_position;
            uint8_t framebuffer_blue_mask_size;
        };
    };
} multiboot_info_t;

#endif /* _KERNEL_MULTIBOOT_H_ */
