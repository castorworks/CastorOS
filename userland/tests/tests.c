// tests.c - 系统调用测试程序
// 用于测试 CastorOS 的各种系统调用功能

#include <syscall.h>
#include <stdio.h>
#include <types.h>

// 辅助函数：获取文件类型字符串
static const char *get_file_type(uint32_t mode) {
    if (S_ISDIR(mode))  return "directory";
    if (S_ISREG(mode))  return "regular file";
    if (S_ISCHR(mode))  return "character device";
    if (S_ISBLK(mode))  return "block device";
    if (S_ISFIFO(mode)) return "FIFO/pipe";
    if (S_ISLNK(mode))  return "symbolic link";
    return "unknown";
}

// 测试 stat 系统调用
static void test_stat(void) {
    printf("\n=== Testing stat() ===\n");
    
    struct stat st;
    
    // 测试根目录
    printf("\n[1] stat(\"/\"):\n");
    if (stat("/", &st) == 0) {
        printf("  Type: %s\n", get_file_type(st.st_mode));
        printf("  Size: %u bytes\n", st.st_size);
        printf("  Inode: %u\n", st.st_ino);
        printf("  Mode: 0%o\n", st.st_mode & 0777);
    } else {
        printf("  Error: stat failed\n");
    }
    
    // 测试 /dev 目录
    printf("\n[2] stat(\"/dev\"):\n");
    if (stat("/dev", &st) == 0) {
        printf("  Type: %s\n", get_file_type(st.st_mode));
        printf("  Size: %u bytes\n", st.st_size);
        printf("  Inode: %u\n", st.st_ino);
    } else {
        printf("  Error: stat failed\n");
    }
    
    // 测试 /dev/console（字符设备）
    printf("\n[3] stat(\"/dev/console\"):\n");
    if (stat("/dev/console", &st) == 0) {
        printf("  Type: %s\n", get_file_type(st.st_mode));
        printf("  Size: %u bytes\n", st.st_size);
        printf("  Inode: %u\n", st.st_ino);
    } else {
        printf("  Error: stat failed\n");
    }
    
    // 测试不存在的文件
    printf("\n[4] stat(\"/nonexistent\"):\n");
    if (stat("/nonexistent", &st) == 0) {
        printf("  Error: should have failed!\n");
    } else {
        printf("  OK: stat correctly returned error for nonexistent file\n");
    }
}

// 测试 fstat 系统调用
static void test_fstat(void) {
    printf("\n=== Testing fstat() ===\n");
    
    struct stat st;
    
    // 测试 stdout (fd=1)
    printf("\n[1] fstat(STDOUT_FILENO):\n");
    if (fstat(STDOUT_FILENO, &st) == 0) {
        printf("  Type: %s\n", get_file_type(st.st_mode));
        printf("  Size: %u bytes\n", st.st_size);
        printf("  Inode: %u\n", st.st_ino);
    } else {
        printf("  Error: fstat failed\n");
    }
    
    // 打开文件并测试 fstat
    printf("\n[2] Open / and fstat:\n");
    int fd = open("/", 0, 0);  // O_RDONLY = 0
    if (fd >= 0) {
        printf("  Opened / as fd=%d\n", fd);
        if (fstat(fd, &st) == 0) {
            printf("  Type: %s\n", get_file_type(st.st_mode));
            printf("  Size: %u bytes\n", st.st_size);
            printf("  Inode: %u\n", st.st_ino);
        } else {
            printf("  Error: fstat failed\n");
        }
        close(fd);
    } else {
        printf("  Error: open failed\n");
    }
    
    // 测试无效的文件描述符
    printf("\n[3] fstat(invalid fd=999):\n");
    if (fstat(999, &st) == 0) {
        printf("  Error: should have failed!\n");
    } else {
        printf("  OK: fstat correctly returned error for invalid fd\n");
    }
}

// 主函数
void _start(void) {
    printf("========================================\n");
    printf("    CastorOS System Call Tests\n");
    printf("========================================\n");
    printf("PID: %d, Parent PID: %d\n", getpid(), getppid());
    
    // 运行 stat/fstat 测试
    test_stat();
    test_fstat();
    
    printf("\n========================================\n");
    printf("    All tests completed!\n");
    printf("========================================\n");
    
    exit(0);
}
