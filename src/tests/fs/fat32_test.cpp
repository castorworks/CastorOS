// ============================================================================
// fat32_test.c - FAT32 文件系统单元测试
// ============================================================================
//
// 模块名称: fat32
// 子系统: fs (文件系统)
// 描述: 测试 FAT32 文件系统的目录项解析和文件名处理功能
//
// 功能覆盖:
//   - 目录项解析 (fat32_dirent 结构解析)
//   - 短文件名 (8.3 格式) 处理
//   - 长文件名 (LFN) 处理
//   - 文件名格式转换
//   - 内存块设备上的真实卷：共享 in-core 节点、打开状态下删除、
//     扩展失败回滚、截断、BPB 校验
//
// **Feature: test-refactor**
// **Validates: Requirements 4.3**
// ============================================================================

#include <tests/ktest.h>
#include <tests/test_module.h>
#include <fs/fat32.h>
#include <fs/vfs.h>
#include <fs/blockdev.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <types.h>

// ============================================================================
// FAT32 目录项结构定义 (用于测试)
// ============================================================================

// FAT32 目录项属性
#define FAT32_ATTR_READ_ONLY  0x01
#define FAT32_ATTR_HIDDEN     0x02
#define FAT32_ATTR_SYSTEM     0x04
#define FAT32_ATTR_VOLUME_ID  0x08
#define FAT32_ATTR_DIRECTORY  0x10
#define FAT32_ATTR_ARCHIVE    0x20
#define FAT32_ATTR_LONG_NAME  0x0F

// FAT32 目录项结构 (32 字节)
typedef struct test_fat32_dirent {
    char name[11];                // 8.3 格式文件名
    uint8_t attributes;           // 属性
    uint8_t reserved;             // 保留
    uint8_t create_time_tenth;    // 创建时间（10 毫秒单位）
    uint16_t create_time;         // 创建时间
    uint16_t create_date;         // 创建日期
    uint16_t access_date;         // 访问日期
    uint16_t cluster_high;        // 起始簇号（高 16 位）
    uint16_t modify_time;         // 修改时间
    uint16_t modify_date;         // 修改日期
    uint16_t cluster_low;         // 起始簇号（低 16 位）
    uint32_t file_size;           // 文件大小（字节）
} __attribute__((packed)) test_fat32_dirent_t;

// FAT32 长文件名目录项结构
typedef struct test_fat32_lfn_entry {
    uint8_t sequence;             // 序号 (0x40 | n 表示最后一个)
    uint16_t name1[5];            // 名称字符 1-5 (UCS-2)
    uint8_t attributes;           // 属性 (0x0F)
    uint8_t type;                 // 类型 (0)
    uint8_t checksum;             // 短文件名校验和
    uint16_t name2[6];            // 名称字符 6-11 (UCS-2)
    uint16_t cluster;             // 簇号 (0)
    uint16_t name3[2];            // 名称字符 12-13 (UCS-2)
} __attribute__((packed)) test_fat32_lfn_entry_t;

// ============================================================================
// 测试辅助函数
// ============================================================================

/**
 * @brief 将 8.3 格式文件名转换为普通文件名
 *
 * 这是 fat32.c 中 fat32_format_filename 函数的测试版本
 */
static void test_format_filename(const char *fat_name, char *name) {
    // 提取主文件名（8 字符）
    int i = 0;
    while (i < 8 && fat_name[i] != ' ' && fat_name[i] != 0) {
        name[i] = fat_name[i];
        i++;
    }
    
    // 检查扩展名
    if (fat_name[8] != ' ' && fat_name[8] != 0) {
        name[i++] = '.';
        int j = 8;
        while (j < 11 && fat_name[j] != ' ' && fat_name[j] != 0) {
            name[i++] = fat_name[j++];
        }
    }
    
    name[i] = '\0';
    
    // 转换为小写
    for (int k = 0; k < i; k++) {
        if (name[k] >= 'A' && name[k] <= 'Z') {
            name[k] = name[k] - 'A' + 'a';
        }
    }
}

/**
 * @brief 将普通文件名转换为 8.3 格式
 *
 * 这是 fat32.c 中 fat32_make_short_name 函数的测试版本
 * @return 0 成功，-1 失败
 */
static int test_make_short_name(const char *name, char out[11]) {
    if (!name || !out) {
        return -1;
    }

    size_t len = strlen(name);
    if (len == 0 || len > 255) {
        return -1;
    }
    
    // 检查特殊名称
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return -1;
    }

    memset(out, ' ', 11);

    int main_index = 0;
    int ext_index = 0;
    bool seen_dot = false;

    for (size_t i = 0; i < len; i++) {
        char c = name[i];

        if (c == '.') {
            if (seen_dot || i == 0) {
                return -1;
            }
            seen_dot = true;
            continue;
        }

        // 检查非法字符
        if (c < 0x20 || c == '"' || c == '*' || c == '+' || c == ',' ||
            c == '/' || c == ':' || c == ';' || c == '<' || c == '=' ||
            c == '>' || c == '?' || c == '[' || c == '\\' || c == ']' ||
            c == '|' ) {
            return -1;
        }

        if (c == ' ') {
            return -1;
        }

        // 转换为大写
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }

        if (!seen_dot) {
            if (main_index >= 8) {
                return -1;
            }
            out[main_index++] = c;
        } else {
            if (ext_index >= 3) {
                return -1;
            }
            out[8 + ext_index++] = c;
        }
    }

    if (main_index == 0) {
        return -1;
    }

    return 0;
}

/**
 * @brief 检查目录项是否为有效文件
 *
 * 这是 fat32.c 中 fat32_is_valid_dirent 函数的测试版本
 */
static bool test_is_valid_dirent(test_fat32_dirent_t *dirent) {
    uint8_t first_byte = (uint8_t)dirent->name[0];
    if (first_byte == 0x00) {
        return false;  // 空目录项
    }
    if (first_byte == 0xE5) {
        return false;  // 已删除
    }
    if ((dirent->attributes & FAT32_ATTR_LONG_NAME) == FAT32_ATTR_LONG_NAME) {
        return false;  // 长文件名项
    }
    if (dirent->attributes & FAT32_ATTR_VOLUME_ID) {
        return false;  // 卷标
    }
    return true;
}

/**
 * @brief 计算短文件名校验和 (用于 LFN)
 */
static uint8_t test_lfn_checksum(const char *short_name) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + (uint8_t)short_name[i];
    }
    return sum;
}

/**
 * @brief 创建测试用目录项
 */
static void create_test_dirent(test_fat32_dirent_t *dirent, 
                               const char *name, 
                               uint8_t attr,
                               uint32_t cluster,
                               uint32_t size) {
    memset(dirent, 0, sizeof(test_fat32_dirent_t));
    memcpy(dirent->name, name, 11);
    dirent->attributes = attr;
    dirent->cluster_low = (uint16_t)(cluster & 0xFFFF);
    dirent->cluster_high = (uint16_t)((cluster >> 16) & 0xFFFF);
    dirent->file_size = size;
}

// ============================================================================
// 测试套件 1: fat32_dirent_tests - 目录项解析测试
// ============================================================================
//
// 测试 FAT32 目录项结构的解析
// **Validates: Requirements 4.3** - FAT32 目录项解析
// ============================================================================

/**
 * @brief 测试目录项结构大小
 *
 * 验证 FAT32 目录项结构为 32 字节
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_dirent_size) {
    ASSERT_EQ_U(sizeof(test_fat32_dirent_t), 32);
}

/**
 * @brief 测试 LFN 目录项结构大小
 *
 * 验证 FAT32 长文件名目录项结构为 32 字节
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_lfn_entry_size) {
    ASSERT_EQ_U(sizeof(test_fat32_lfn_entry_t), 32);
}

/**
 * @brief 测试有效目录项检测
 *
 * 验证能正确识别有效的目录项
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_valid_dirent) {
    test_fat32_dirent_t dirent;
    
    // 创建有效文件目录项
    create_test_dirent(&dirent, "TEST    TXT", FAT32_ATTR_ARCHIVE, 100, 1024);
    ASSERT_TRUE(test_is_valid_dirent(&dirent));
    
    // 创建有效目录项
    create_test_dirent(&dirent, "SUBDIR     ", FAT32_ATTR_DIRECTORY, 200, 0);
    ASSERT_TRUE(test_is_valid_dirent(&dirent));
}

/**
 * @brief 测试空目录项检测
 *
 * 验证能正确识别空目录项 (首字节为 0x00)
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_empty_dirent) {
    test_fat32_dirent_t dirent;
    memset(&dirent, 0, sizeof(dirent));
    
    // 首字节为 0x00 表示空目录项
    dirent.name[0] = 0x00;
    ASSERT_FALSE(test_is_valid_dirent(&dirent));
}

/**
 * @brief 测试已删除目录项检测
 *
 * 验证能正确识别已删除的目录项 (首字节为 0xE5)
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_deleted_dirent) {
    test_fat32_dirent_t dirent;
    create_test_dirent(&dirent, "DELETED TXT", FAT32_ATTR_ARCHIVE, 100, 1024);
    
    // 首字节为 0xE5 表示已删除
    dirent.name[0] = 0xE5;
    ASSERT_FALSE(test_is_valid_dirent(&dirent));
}

/**
 * @brief 测试卷标目录项检测
 *
 * 验证能正确识别卷标目录项
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_volume_id_dirent) {
    test_fat32_dirent_t dirent;
    create_test_dirent(&dirent, "VOLUME     ", FAT32_ATTR_VOLUME_ID, 0, 0);
    
    // 卷标不是有效文件
    ASSERT_FALSE(test_is_valid_dirent(&dirent));
}

/**
 * @brief 测试 LFN 目录项检测
 *
 * 验证能正确识别长文件名目录项
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_lfn_dirent) {
    test_fat32_dirent_t dirent;
    create_test_dirent(&dirent, "LONGNAME   ", FAT32_ATTR_LONG_NAME, 0, 0);
    
    // LFN 条目不是有效文件
    ASSERT_FALSE(test_is_valid_dirent(&dirent));
}

/**
 * @brief 测试簇号提取
 *
 * 验证能正确从目录项提取 32 位簇号
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_cluster_extraction) {
    test_fat32_dirent_t dirent;
    
    // 测试小簇号
    create_test_dirent(&dirent, "TEST    TXT", FAT32_ATTR_ARCHIVE, 100, 1024);
    uint32_t cluster = ((uint32_t)dirent.cluster_high << 16) | dirent.cluster_low;
    ASSERT_EQ_U(cluster, 100);
    
    // 测试大簇号 (跨越 16 位边界)
    create_test_dirent(&dirent, "TEST    TXT", FAT32_ATTR_ARCHIVE, 0x12345678, 1024);
    cluster = ((uint32_t)dirent.cluster_high << 16) | dirent.cluster_low;
    ASSERT_EQ_U(cluster, 0x12345678);
}

// ============================================================================
// 测试套件 2: fat32_shortname_tests - 短文件名测试
// ============================================================================
//
// 测试 FAT32 8.3 格式短文件名的处理
// **Validates: Requirements 4.3** - 短文件名处理
// ============================================================================

/**
 * @brief 测试简单文件名格式化
 *
 * 验证 8.3 格式转换为普通文件名
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_format_simple_name) {
    char result[13];
    
    // 简单文件名
    test_format_filename("TEST    TXT", result);
    ASSERT_STR_EQ(result, "test.txt");
}

/**
 * @brief 测试无扩展名文件名格式化
 *
 * 验证无扩展名的 8.3 格式转换
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_format_no_extension) {
    char result[13];
    
    // 无扩展名
    test_format_filename("README     ", result);
    ASSERT_STR_EQ(result, "readme");
}

/**
 * @brief 测试满长度文件名格式化
 *
 * 验证 8 字符主名 + 3 字符扩展名的转换
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_format_full_length) {
    char result[13];
    
    // 满长度文件名
    test_format_filename("FILENAMEEXT", result);
    ASSERT_STR_EQ(result, "filename.ext");
}

/**
 * @brief 测试目录名格式化
 *
 * 验证目录名的 8.3 格式转换
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_format_directory) {
    char result[13];
    
    // 目录名（无扩展名）
    test_format_filename("SUBDIR     ", result);
    ASSERT_STR_EQ(result, "subdir");
}

/**
 * @brief 测试特殊目录项格式化
 *
 * 验证 "." 和 ".." 目录项的格式化
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_format_special_dirs) {
    char result[13];
    
    // "." 目录
    test_format_filename(".          ", result);
    ASSERT_STR_EQ(result, ".");
    
    // ".." 目录
    test_format_filename("..         ", result);
    ASSERT_STR_EQ(result, "..");
}

/**
 * @brief 测试短文件名生成 - 简单名称
 *
 * 验证普通文件名转换为 8.3 格式
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_make_short_simple) {
    char out[11];
    
    int result = test_make_short_name("test.txt", out);
    ASSERT_EQ(result, 0);
    ASSERT_TRUE(memcmp(out, "TEST    TXT", 11) == 0);
}

/**
 * @brief 测试短文件名生成 - 无扩展名
 *
 * 验证无扩展名文件转换为 8.3 格式
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_make_short_no_ext) {
    char out[11];
    
    int result = test_make_short_name("readme", out);
    ASSERT_EQ(result, 0);
    ASSERT_TRUE(memcmp(out, "README     ", 11) == 0);
}

/**
 * @brief 测试短文件名生成 - 大写转换
 *
 * 验证小写字母转换为大写
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_make_short_uppercase) {
    char out[11];
    
    int result = test_make_short_name("Hello.Doc", out);
    ASSERT_EQ(result, 0);
    ASSERT_TRUE(memcmp(out, "HELLO   DOC", 11) == 0);
}

/**
 * @brief 测试短文件名生成 - 非法字符
 *
 * 验证包含非法字符的文件名被拒绝
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_make_short_invalid_chars) {
    char out[11];
    
    // 包含非法字符
    ASSERT_EQ(test_make_short_name("test*.txt", out), -1);
    ASSERT_EQ(test_make_short_name("test?.txt", out), -1);
    ASSERT_EQ(test_make_short_name("test<.txt", out), -1);
    ASSERT_EQ(test_make_short_name("test>.txt", out), -1);
    ASSERT_EQ(test_make_short_name("test:.txt", out), -1);
    ASSERT_EQ(test_make_short_name("test\".txt", out), -1);
    ASSERT_EQ(test_make_short_name("test|.txt", out), -1);
}

/**
 * @brief 测试短文件名生成 - 名称过长
 *
 * 验证超过 8.3 限制的文件名被拒绝
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_make_short_too_long) {
    char out[11];
    
    // 主名超过 8 字符
    ASSERT_EQ(test_make_short_name("verylongname.txt", out), -1);
    
    // 扩展名超过 3 字符
    ASSERT_EQ(test_make_short_name("test.html5", out), -1);
}

/**
 * @brief 测试短文件名生成 - 特殊情况
 *
 * 验证特殊情况的处理
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_make_short_special) {
    char out[11];
    
    // 空名称
    ASSERT_EQ(test_make_short_name("", out), -1);
    
    // 只有点
    ASSERT_EQ(test_make_short_name(".", out), -1);
    ASSERT_EQ(test_make_short_name("..", out), -1);
    
    // 以点开头
    ASSERT_EQ(test_make_short_name(".hidden", out), -1);
    
    // 包含空格
    ASSERT_EQ(test_make_short_name("test file.txt", out), -1);
}

// ============================================================================
// 测试套件 3: fat32_lfn_tests - 长文件名测试
// ============================================================================
//
// 测试 FAT32 长文件名 (LFN) 的处理
// **Validates: Requirements 4.3** - 长文件名处理
// ============================================================================

/**
 * @brief 测试 LFN 校验和计算
 *
 * 验证长文件名校验和算法
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_lfn_checksum) {
    // 测试已知的校验和值
    uint8_t checksum = test_lfn_checksum("TEST    TXT");
    // 校验和应该是一个确定的值
    ASSERT_TRUE(checksum != 0);
    
    // 相同输入应产生相同校验和
    uint8_t checksum2 = test_lfn_checksum("TEST    TXT");
    ASSERT_EQ_U(checksum, checksum2);
    
    // 不同输入应产生不同校验和
    uint8_t checksum3 = test_lfn_checksum("FILE    TXT");
    ASSERT_NE_U(checksum, checksum3);
}

/**
 * @brief 测试 LFN 序号解析
 *
 * 验证 LFN 序号字段的解析
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_lfn_sequence) {
    test_fat32_lfn_entry_t lfn;
    memset(&lfn, 0, sizeof(lfn));
    
    // 第一个 LFN 条目 (最后一个片段)
    lfn.sequence = 0x41;  // 0x40 | 1
    ASSERT_TRUE((lfn.sequence & 0x40) != 0);  // 是最后一个
    ASSERT_EQ_U(lfn.sequence & 0x1F, 1);      // 序号为 1
    
    // 中间 LFN 条目
    lfn.sequence = 0x02;
    ASSERT_FALSE((lfn.sequence & 0x40) != 0); // 不是最后一个
    ASSERT_EQ_U(lfn.sequence & 0x1F, 2);      // 序号为 2
}

/**
 * @brief 测试 LFN 属性识别
 *
 * 验证 LFN 条目的属性值
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_lfn_attributes) {
    // LFN 属性是 READ_ONLY | HIDDEN | SYSTEM | VOLUME_ID
    uint8_t lfn_attr = FAT32_ATTR_READ_ONLY | FAT32_ATTR_HIDDEN | 
                       FAT32_ATTR_SYSTEM | FAT32_ATTR_VOLUME_ID;
    ASSERT_EQ_U(lfn_attr, FAT32_ATTR_LONG_NAME);
    ASSERT_EQ_U(FAT32_ATTR_LONG_NAME, 0x0F);
}

// ============================================================================
// 测试套件 4: fat32_edge_tests - 边界条件测试
// ============================================================================
//
// 测试 FAT32 的边界条件和错误处理
// **Validates: Requirements 4.3** - 边界条件处理
// ============================================================================

/**
 * @brief 测试文件名边界长度
 *
 * 验证恰好 8.3 长度的文件名处理
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_boundary_length) {
    char out[11];
    
    // 恰好 8 字符主名
    int result = test_make_short_name("12345678", out);
    ASSERT_EQ(result, 0);
    ASSERT_TRUE(memcmp(out, "12345678   ", 11) == 0);
    
    // 恰好 8.3 格式
    result = test_make_short_name("12345678.123", out);
    ASSERT_EQ(result, 0);
    ASSERT_TRUE(memcmp(out, "12345678123", 11) == 0);
}

/**
 * @brief 测试数字文件名
 *
 * 验证纯数字文件名的处理
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_numeric_name) {
    char out[11];
    char result_name[13];
    
    // 纯数字文件名
    int result = test_make_short_name("123.456", out);
    ASSERT_EQ(result, 0);
    
    // 验证往返转换
    test_format_filename(out, result_name);
    ASSERT_STR_EQ(result_name, "123.456");
}

/**
 * @brief 测试允许的特殊字符
 *
 * 验证 FAT32 允许的特殊字符
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_allowed_special_chars) {
    char out[11];
    
    // 下划线
    ASSERT_EQ(test_make_short_name("test_1.txt", out), 0);
    
    // 连字符
    ASSERT_EQ(test_make_short_name("test-1.txt", out), 0);
    
    // 波浪号
    ASSERT_EQ(test_make_short_name("test~1.txt", out), 0);
}

/**
 * @brief 测试目录项属性组合
 *
 * 验证各种属性组合的识别
 * _Requirements: 4.3_
 */
TEST_CASE(test_fat32_attribute_combinations) {
    test_fat32_dirent_t dirent;
    
    // 只读文件
    create_test_dirent(&dirent, "READONLY   ", 
                       FAT32_ATTR_READ_ONLY | FAT32_ATTR_ARCHIVE, 100, 1024);
    ASSERT_TRUE(test_is_valid_dirent(&dirent));
    ASSERT_TRUE((dirent.attributes & FAT32_ATTR_READ_ONLY) != 0);
    
    // 隐藏文件
    create_test_dirent(&dirent, "HIDDEN     ", 
                       FAT32_ATTR_HIDDEN | FAT32_ATTR_ARCHIVE, 100, 1024);
    ASSERT_TRUE(test_is_valid_dirent(&dirent));
    ASSERT_TRUE((dirent.attributes & FAT32_ATTR_HIDDEN) != 0);
    
    // 系统文件
    create_test_dirent(&dirent, "SYSTEM     ", 
                       FAT32_ATTR_SYSTEM | FAT32_ATTR_ARCHIVE, 100, 1024);
    ASSERT_TRUE(test_is_valid_dirent(&dirent));
    ASSERT_TRUE((dirent.attributes & FAT32_ATTR_SYSTEM) != 0);
}

// ============================================================================
// 真实文件系统测试：在内存块设备上格式化一个很小的 FAT32 卷
// ============================================================================
//
// 卷布局（512 字节扇区，每簇 1 扇区）：
//   扇区 0..3   保留区（扇区 0 是 BPB）
//   扇区 4..5   FAT #1（256 个表项）
//   扇区 6..7   FAT #2
//   扇区 8..71  数据区，64 个簇（簇 2..65），簇 2 是根目录
// 所以空卷上有 63 个空闲簇。

#define RD_SECTOR_SIZE      512u
#define RD_RESERVED         4u
#define RD_FAT_SECTORS      2u
#define RD_CLUSTERS         64u
#define RD_TOTAL_SECTORS    (RD_RESERVED + 2 * RD_FAT_SECTORS + RD_CLUSTERS)
#define RD_FREE_WHEN_EMPTY  (RD_CLUSTERS - 1)

typedef struct {
    uint8_t *data;
    uint32_t sectors;
} fat32_ramdisk_t;

class Fat32RamdiskOps final : public fs::BlockdevOps {
public:
    int read(void *dev, uint32_t sector, uint32_t count, uint8_t *buffer) const override {
        fat32_ramdisk_t *disk = (fat32_ramdisk_t *)dev;
        if (sector > disk->sectors || count > disk->sectors - sector) {
            return -1;
        }
        memcpy(buffer, disk->data + sector * RD_SECTOR_SIZE, count * RD_SECTOR_SIZE);
        return 0;
    }
    int write(void *dev, uint32_t sector, uint32_t count, const uint8_t *buffer) const override {
        fat32_ramdisk_t *disk = (fat32_ramdisk_t *)dev;
        if (sector > disk->sectors || count > disk->sectors - sector) {
            return -1;
        }
        memcpy(disk->data + sector * RD_SECTOR_SIZE, buffer, count * RD_SECTOR_SIZE);
        return 0;
    }
};
static const Fat32RamdiskOps fat32_ramdisk_ops{};

static fat32_ramdisk_t fat32_test_disk;
static fs::Blockdev fat32_test_dev;

static void rd_put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void rd_put32(uint8_t *p, uint32_t v) { rd_put16(p, (uint16_t)v); rd_put16(p + 2, (uint16_t)(v >> 16)); }
static uint32_t rd_get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/** 分配内存盘并写入一个空的 FAT32 卷（不挂载） */
static bool fat32_test_format(void) {
    fat32_test_disk.sectors = RD_TOTAL_SECTORS;
    fat32_test_disk.data = (uint8_t *)kmalloc(RD_TOTAL_SECTORS * RD_SECTOR_SIZE);
    if (!fat32_test_disk.data) {
        return false;
    }
    uint8_t *d = fat32_test_disk.data;
    memset(d, 0, RD_TOTAL_SECTORS * RD_SECTOR_SIZE);

    // BPB
    d[0] = 0xEB; d[1] = 0x58; d[2] = 0x90;
    memcpy(d + 3, "CASTORT ", 8);
    rd_put16(d + 11, RD_SECTOR_SIZE);       // bytes_per_sector
    d[13] = 1;                              // sectors_per_cluster
    rd_put16(d + 14, RD_RESERVED);          // reserved_sectors
    d[16] = 2;                              // fat_count
    d[21] = 0xF8;                           // media_type
    rd_put32(d + 32, RD_TOTAL_SECTORS);     // total_sectors_32
    rd_put32(d + 36, RD_FAT_SECTORS);       // sectors_per_fat_32
    rd_put32(d + 44, 2);                    // root_cluster
    rd_put16(d + 48, 0);                    // fs_info_sector（无）
    d[66] = 0x29;                           // boot_signature
    memcpy(d + 71, "TESTVOL    ", 11);
    memcpy(d + 82, "FAT32   ", 8);
    d[510] = 0x55; d[511] = 0xAA;

    // 两份 FAT：0、1 号保留，簇 2（根目录）为 EOC
    for (uint32_t copy = 0; copy < 2; copy++) {
        uint8_t *fat = d + (RD_RESERVED + copy * RD_FAT_SECTORS) * RD_SECTOR_SIZE;
        rd_put32(fat + 0, 0x0FFFFFF8);
        rd_put32(fat + 4, 0x0FFFFFFF);
        rd_put32(fat + 8, 0x0FFFFFFF);
    }

    memset(&fat32_test_dev, 0, sizeof(fat32_test_dev));
    strcpy(fat32_test_dev.name, "fat32test");
    fat32_test_dev.private_data = &fat32_test_disk;
    fat32_test_dev.block_size = RD_SECTOR_SIZE;
    fat32_test_dev.total_sectors = RD_TOTAL_SECTORS;
    fat32_test_dev.ops = &fat32_ramdisk_ops;
    return true;
}

static void fat32_test_free_disk(void) {
    if (fat32_test_disk.data) {
        kfree(fat32_test_disk.data);
        fat32_test_disk.data = NULL;
    }
}

/** 格式化并挂载，返回根目录节点 */
static fs_node_t *fat32_test_mount(void) {
    if (!fat32_test_format()) {
        return NULL;
    }
    fs_node_t *root = fs::Fat32::init(&fat32_test_dev);
    if (!root) {
        fat32_test_free_disk();
    }
    return root;
}

static void fat32_test_unmount(fs_node_t *root) {
    if (root) {
        fs::Fat32::deinit(root);
    }
    fat32_test_free_disk();
}

/** 直接从内存盘的 FAT #1 数空闲簇 */
static uint32_t fat32_test_free_clusters(void) {
    const uint8_t *fat = fat32_test_disk.data + RD_RESERVED * RD_SECTOR_SIZE;
    uint32_t free_count = 0;
    for (uint32_t c = 2; c < RD_CLUSTERS + 2; c++) {
        if ((rd_get32(fat + c * 4) & 0x0FFFFFFF) == 0) {
            free_count++;
        }
    }
    return free_count;
}

/** 文件 [offset, offset+len) 是否全是字节 value */
static bool fat32_test_content_is(fs_node_t *file, uint32_t offset, uint32_t len, uint8_t value) {
    uint8_t *buf = (uint8_t *)kmalloc(len);
    if (!buf) {
        return false;
    }
    memset(buf, (uint8_t)~value, len);
    bool ok = fs::Vfs::read(file, offset, len, buf) == len;
    for (uint32_t i = 0; ok && i < len; i++) {
        ok = (buf[i] == value);
    }
    kfree(buf);
    return ok;
}

/** 向文件 offset 处写 len 个字节 value，返回写入的字节数 */
static uint32_t fat32_test_fill(fs_node_t *file, uint32_t offset, uint32_t len, uint8_t value) {
    uint8_t *buf = (uint8_t *)kmalloc(len);
    if (!buf) {
        return 0;
    }
    memset(buf, value, len);
    uint32_t written = fs::Vfs::write(file, offset, len, buf);
    kfree(buf);
    return written;
}

/**
 * @brief 多次查找同一文件得到同一个节点，写入不会互相覆盖目录项
 */
TEST_CASE(test_fat32_shared_incore_node) {
    fs_node_t *root = fat32_test_mount();
    ASSERT_NOT_NULL(root);
    if (!root) return;
    ASSERT_EQ_U(RD_FREE_WHEN_EMPTY, fat32_test_free_clusters());

    ASSERT_EQ(0, root->ops->create(root, "a.txt"));
    fs_node_t *a1 = fs::Vfs::finddir(root, "a.txt");
    fs_node_t *a2 = fs::Vfs::finddir(root, "a.txt");
    ASSERT_NOT_NULL(a1);
    ASSERT_EQ_PTR(a1, a2);
    if (!a1 || !a2) { fat32_test_unmount(root); return; }

    // 通过第一个引用写入后，第二个引用立刻看到新的大小
    ASSERT_EQ_U(100, fat32_test_fill(a1, 0, 100, 'A'));
    ASSERT_EQ_U(100, a2->size);

    // 通过第二个引用追加：必须接在同一条簇链上，而不是另起一条
    ASSERT_EQ_U(50, fat32_test_fill(a2, 100, 50, 'B'));
    ASSERT_EQ_U(RD_FREE_WHEN_EMPTY - 1, fat32_test_free_clusters());
    ASSERT_TRUE(fat32_test_content_is(a1, 0, 100, 'A'));
    ASSERT_TRUE(fat32_test_content_is(a1, 100, 50, 'B'));

    fs::Vfs::release_node(a1);
    fs::Vfs::release_node(a2);

    // 全部释放后重新查找：目录项里是合并后的结果
    fs_node_t *a3 = fs::Vfs::finddir(root, "a.txt");
    ASSERT_NOT_NULL(a3);
    if (a3) {
        ASSERT_EQ_U(150, a3->size);
        ASSERT_TRUE(fat32_test_content_is(a3, 0, 100, 'A'));
        ASSERT_TRUE(fat32_test_content_is(a3, 100, 50, 'B'));
        fs::Vfs::release_node(a3);
    }

    fat32_test_unmount(root);
}

/**
 * @brief 删除仍被打开的文件：数据保留到最后一次关闭，且不会破坏复用其槽位的新文件
 */
TEST_CASE(test_fat32_unlink_while_open) {
    fs_node_t *root = fat32_test_mount();
    ASSERT_NOT_NULL(root);
    if (!root) return;

    ASSERT_EQ(0, root->ops->create(root, "a.txt"));
    fs_node_t *a = fs::Vfs::finddir(root, "a.txt");
    ASSERT_NOT_NULL(a);
    if (!a) { fat32_test_unmount(root); return; }
    ASSERT_EQ_U(600, fat32_test_fill(a, 0, 600, 'A'));     // 2 个簇
    fs::Vfs::pin(a);                                       // 模拟一个文件描述符
    ASSERT_EQ_U(RD_FREE_WHEN_EMPTY - 2, fat32_test_free_clusters());

    ASSERT_EQ(0, root->ops->unlink(root, "a.txt"));
    ASSERT_NULL(fs::Vfs::finddir(root, "a.txt"));
    ASSERT_TRUE((a->flags & FS_NODE_FLAG_UNLINKED) != 0);
    // 簇链还在
    ASSERT_EQ_U(RD_FREE_WHEN_EMPTY - 2, fat32_test_free_clusters());

    // 新文件复用刚才的目录项槽位
    ASSERT_EQ(0, root->ops->create(root, "b.txt"));
    fs_node_t *b = fs::Vfs::finddir(root, "b.txt");
    ASSERT_NOT_NULL(b);
    ASSERT_NE_PTR(a, b);
    if (!b) { fat32_test_unmount(root); return; }
    ASSERT_EQ_U(700, fat32_test_fill(b, 0, 700, 'B'));

    // 继续写已删除的文件：不能碰到 b 的数据和目录项
    ASSERT_EQ_U(600, fat32_test_fill(a, 0, 600, 'C'));
    ASSERT_TRUE(fat32_test_content_is(a, 0, 600, 'C'));
    ASSERT_TRUE(fat32_test_content_is(b, 0, 700, 'B'));
    fs::Vfs::release_node(b);
    b = fs::Vfs::finddir(root, "b.txt");
    ASSERT_NOT_NULL(b);
    if (b) {
        ASSERT_EQ_U(700, b->size);
        ASSERT_TRUE(fat32_test_content_is(b, 0, 700, 'B'));
        fs::Vfs::release_node(b);
    }

    // 最后一次关闭：节点销毁，簇链回收
    ASSERT_TRUE(fs::Vfs::unpin(a));
    ASSERT_EQ_U(RD_FREE_WHEN_EMPTY - 2, fat32_test_free_clusters());   // 只剩 b 的 2 个簇

    ASSERT_EQ(0, root->ops->unlink(root, "b.txt"));
    ASSERT_EQ_U(RD_FREE_WHEN_EMPTY, fat32_test_free_clusters());

    fat32_test_unmount(root);
}

/**
 * @brief 扩展失败（卷放不下 / 空闲簇不够）时不留下已分配的簇
 */
TEST_CASE(test_fat32_extend_failure_rolls_back) {
    fs_node_t *root = fat32_test_mount();
    ASSERT_NOT_NULL(root);
    if (!root) return;

    ASSERT_EQ(0, root->ops->create(root, "big.bin"));
    fs_node_t *big = fs::Vfs::finddir(root, "big.bin");
    ASSERT_NOT_NULL(big);
    if (!big) { fat32_test_unmount(root); return; }

    // lseek 到接近 2GB 再写 1 字节：整个卷都放不下，不能占用任何簇
    uint8_t byte = 'x';
    ASSERT_EQ_U(0, fs::Vfs::write(big, 0x7FFFFFF0u, 1, &byte));
    ASSERT_EQ_U(0, big->size);
    ASSERT_EQ_U(RD_FREE_WHEN_EMPTY, fat32_test_free_clusters());

    // 占掉 40 个簇，剩 23 个
    ASSERT_EQ_U(40 * RD_SECTOR_SIZE, fat32_test_fill(big, 0, 40 * RD_SECTOR_SIZE, 'G'));
    ASSERT_EQ_U(RD_FREE_WHEN_EMPTY - 40, fat32_test_free_clusters());

    // 空文件需要 31 个簇但只有 23 个：失败后分配到一半的簇必须全部归还
    ASSERT_EQ(0, root->ops->create(root, "c.bin"));
    fs_node_t *c = fs::Vfs::finddir(root, "c.bin");
    ASSERT_NOT_NULL(c);
    if (c) {
        ASSERT_EQ_U(0, fs::Vfs::write(c, 30 * RD_SECTOR_SIZE, 1, &byte));
        ASSERT_EQ_U(0, c->size);
        ASSERT_EQ_U(RD_FREE_WHEN_EMPTY - 40, fat32_test_free_clusters());
        // 之后的小写入仍然成功
        ASSERT_EQ_U(1, fs::Vfs::write(c, 0, 1, &byte));
        ASSERT_EQ_U(RD_FREE_WHEN_EMPTY - 41, fat32_test_free_clusters());
        fs::Vfs::release_node(c);
    }

    // 已有簇链的文件扩展失败：原链保持完整，链尾仍是 EOC
    ASSERT_EQ_U(0, fs::Vfs::write(big, 62 * RD_SECTOR_SIZE, 1, &byte));
    ASSERT_EQ_U(40 * RD_SECTOR_SIZE, big->size);
    ASSERT_EQ_U(RD_FREE_WHEN_EMPTY - 41, fat32_test_free_clusters());
    ASSERT_TRUE(fat32_test_content_is(big, 39 * RD_SECTOR_SIZE, RD_SECTOR_SIZE, 'G'));
    ASSERT_EQ_U(1, fs::Vfs::write(big, 40 * RD_SECTOR_SIZE, 1, &byte));
    ASSERT_EQ_U(RD_FREE_WHEN_EMPTY - 42, fat32_test_free_clusters());

    fs::Vfs::release_node(big);
    fat32_test_unmount(root);
}

/**
 * @brief 截断：收缩释放尾部簇，截到 0 释放整条链，扩展部分读出 0
 */
TEST_CASE(test_fat32_truncate_real) {
    fs_node_t *root = fat32_test_mount();
    ASSERT_NOT_NULL(root);
    if (!root) return;

    ASSERT_EQ(0, root->ops->create(root, "t.bin"));
    fs_node_t *t = fs::Vfs::finddir(root, "t.bin");
    ASSERT_NOT_NULL(t);
    if (!t) { fat32_test_unmount(root); return; }

    ASSERT_EQ_U(3 * RD_SECTOR_SIZE, fat32_test_fill(t, 0, 3 * RD_SECTOR_SIZE, 'T'));
    ASSERT_EQ_U(RD_FREE_WHEN_EMPTY - 3, fat32_test_free_clusters());

    // 收缩到 700 字节：保留 2 个簇
    ASSERT_EQ(0, fs::Vfs::truncate(t, 700));
    ASSERT_EQ_U(700, t->size);
    ASSERT_EQ_U(RD_FREE_WHEN_EMPTY - 2, fat32_test_free_clusters());
    ASSERT_TRUE(fat32_test_content_is(t, 0, 700, 'T'));

    // 扩展：新增部分是 0，不是截断前的旧数据
    ASSERT_EQ(0, fs::Vfs::truncate(t, 1500));
    ASSERT_EQ_U(RD_FREE_WHEN_EMPTY - 3, fat32_test_free_clusters());
    ASSERT_TRUE(fat32_test_content_is(t, 0, 700, 'T'));
    ASSERT_TRUE(fat32_test_content_is(t, 700, 800, 0));

    // 截到 0：整条链释放，目录项同步
    ASSERT_EQ(0, fs::Vfs::truncate(t, 0));
    ASSERT_EQ_U(0, t->size);
    ASSERT_EQ_U(RD_FREE_WHEN_EMPTY, fat32_test_free_clusters());
    fs::Vfs::release_node(t);

    t = fs::Vfs::finddir(root, "t.bin");
    ASSERT_NOT_NULL(t);
    if (t) {
        ASSERT_EQ_U(0, t->size);
        ASSERT_EQ_U(2, fat32_test_fill(t, 0, 2, 'n'));
        ASSERT_EQ_U(2, t->size);
        ASSERT_EQ_U(RD_FREE_WHEN_EMPTY - 1, fat32_test_free_clusters());
        fs::Vfs::release_node(t);
    }

    fat32_test_unmount(root);
}

/**
 * @brief BPB 与块设备不符或自相矛盾时拒绝挂载
 */
TEST_CASE(test_fat32_rejects_bad_bpb) {
    struct { uint32_t offset; uint32_t width; uint32_t value; } cases[] = {
        { 11, 2, 256 },                     // bytes_per_sector 小于设备块大小
        { 11, 2, 1024 },                    // bytes_per_sector 大于设备块大小
        { 13, 1, 3 },                       // sectors_per_cluster 不是 2 的幂
        { 16, 1, 0 },                       // fat_count = 0
        { 14, 2, 0 },                       // reserved_sectors = 0
        { 36, 4, 0 },                       // sectors_per_fat = 0
        { 36, 4, 0x80000000u },             // fat_count * sectors_per_fat 回绕
        { 32, 4, RD_TOTAL_SECTORS + 100 },  // 文件系统比设备大
        { 44, 4, 1 },                       // root_cluster 太小
        { 44, 4, RD_CLUSTERS + 2 },         // root_cluster 超出数据区
    };

    for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ASSERT_TRUE(fat32_test_format());
        if (!fat32_test_disk.data) return;
        uint8_t *field = fat32_test_disk.data + cases[i].offset;
        if (cases[i].width == 1) {
            field[0] = (uint8_t)cases[i].value;
        } else if (cases[i].width == 2) {
            rd_put16(field, (uint16_t)cases[i].value);
        } else {
            rd_put32(field, cases[i].value);
        }
        fs_node_t *root = fs::Fat32::init(&fat32_test_dev);
        ASSERT_NULL(root);
        if (root) {
            fs::Fat32::deinit(root);
        }
        fat32_test_free_disk();
    }

    // 合法的卷仍然可以挂载
    fs_node_t *root = fat32_test_mount();
    ASSERT_NOT_NULL(root);
    fat32_test_unmount(root);
}

// ============================================================================
// 测试套件定义
// ============================================================================

/**
 * @brief 目录项解析测试套件
 *
 * **Validates: Requirements 4.3**
 */
TEST_SUITE(fat32_dirent_tests) {
    RUN_TEST(test_fat32_dirent_size);
    RUN_TEST(test_fat32_lfn_entry_size);
    RUN_TEST(test_fat32_valid_dirent);
    RUN_TEST(test_fat32_empty_dirent);
    RUN_TEST(test_fat32_deleted_dirent);
    RUN_TEST(test_fat32_volume_id_dirent);
    RUN_TEST(test_fat32_lfn_dirent);
    RUN_TEST(test_fat32_cluster_extraction);
}

/**
 * @brief 短文件名测试套件
 *
 * **Validates: Requirements 4.3**
 */
TEST_SUITE(fat32_shortname_tests) {
    RUN_TEST(test_fat32_format_simple_name);
    RUN_TEST(test_fat32_format_no_extension);
    RUN_TEST(test_fat32_format_full_length);
    RUN_TEST(test_fat32_format_directory);
    RUN_TEST(test_fat32_format_special_dirs);
    RUN_TEST(test_fat32_make_short_simple);
    RUN_TEST(test_fat32_make_short_no_ext);
    RUN_TEST(test_fat32_make_short_uppercase);
    RUN_TEST(test_fat32_make_short_invalid_chars);
    RUN_TEST(test_fat32_make_short_too_long);
    RUN_TEST(test_fat32_make_short_special);
}

/**
 * @brief 长文件名测试套件
 *
 * **Validates: Requirements 4.3**
 */
TEST_SUITE(fat32_lfn_tests) {
    RUN_TEST(test_fat32_lfn_checksum);
    RUN_TEST(test_fat32_lfn_sequence);
    RUN_TEST(test_fat32_lfn_attributes);
}

/**
 * @brief 边界条件测试套件
 *
 * **Validates: Requirements 4.3**
 */
TEST_SUITE(fat32_edge_tests) {
    RUN_TEST(test_fat32_boundary_length);
    RUN_TEST(test_fat32_numeric_name);
    RUN_TEST(test_fat32_allowed_special_chars);
    RUN_TEST(test_fat32_attribute_combinations);
}

/**
 * @brief 在内存块设备上的真实读写测试套件
 */
TEST_SUITE(fat32_volume_tests) {
    RUN_TEST(test_fat32_shared_incore_node);
    RUN_TEST(test_fat32_unlink_while_open);
    RUN_TEST(test_fat32_extend_failure_rolls_back);
    RUN_TEST(test_fat32_truncate_real);
    RUN_TEST(test_fat32_rejects_bad_bpb);
}

// ============================================================================
// 模块运行函数
// ============================================================================

/**
 * @brief 运行所有 FAT32 测试
 *
 * 按功能组织的测试套件：
 *   1. fat32_dirent_tests - 目录项解析测试
 *   2. fat32_shortname_tests - 短文件名测试
 *   3. fat32_lfn_tests - 长文件名测试
 *   4. fat32_edge_tests - 边界条件测试
 *
 * **Feature: test-refactor**
 * **Validates: Requirements 4.3**
 */
void run_fat32_tests(void) {
    // 初始化测试框架
    unittest_init();

    // ========================================================================
    // 功能测试套件
    // ========================================================================

    // 套件 1: 目录项解析测试
    // _Requirements: 4.3_
    RUN_SUITE(fat32_dirent_tests);

    // 套件 2: 短文件名测试
    // _Requirements: 4.3_
    RUN_SUITE(fat32_shortname_tests);

    // 套件 3: 长文件名测试
    // _Requirements: 4.3_
    RUN_SUITE(fat32_lfn_tests);

    // 套件 4: 边界条件测试
    // _Requirements: 4.3_
    RUN_SUITE(fat32_edge_tests);

    // 套件 5: 内存块设备上的真实文件系统测试
    RUN_SUITE(fat32_volume_tests);

    // 打印测试摘要
    unittest_print_summary();
}

// ============================================================================
// 模块注册
// ============================================================================

/**
 * @brief FAT32 测试模块元数据
 *
 * 使用 TEST_MODULE_DESC 宏注册模块到测试框架
 *
 * **Feature: test-refactor**
 * **Validates: Requirements 4.3, 10.1, 10.2**
 */
TEST_MODULE_DESC(fat32, FS, run_fat32_tests,
    "FAT32 file system tests - directory entry parsing, short/long filename handling");
