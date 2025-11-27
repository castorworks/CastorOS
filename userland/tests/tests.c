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

// 测试 brk/sbrk 系统调用
static void test_brk(void) {
    printf("\n=== Testing brk()/sbrk() ===\n");
    
    // 测试 1: 获取当前堆位置
    printf("\n[1] Get current heap position:\n");
    void *initial_brk = sbrk(0);
    if (initial_brk == (void *)-1) {
        printf("  Error: sbrk(0) failed\n");
        return;
    }
    printf("  Initial heap end: 0x%x\n", (uint32_t)initial_brk);
    
    // 测试 2: 使用 sbrk 分配内存
    printf("\n[2] Allocate 4096 bytes using sbrk:\n");
    void *ptr1 = sbrk(4096);
    if (ptr1 == (void *)-1) {
        printf("  Error: sbrk(4096) failed\n");
        return;
    }
    printf("  Old heap end: 0x%x\n", (uint32_t)ptr1);
    
    void *new_brk = sbrk(0);
    printf("  New heap end: 0x%x\n", (uint32_t)new_brk);
    printf("  Allocated: %u bytes\n", (uint32_t)new_brk - (uint32_t)ptr1);
    
    // 测试 3: 写入和读取分配的内存
    printf("\n[3] Write and read allocated memory:\n");
    uint32_t *int_ptr = (uint32_t *)ptr1;
    int_ptr[0] = 0xDEADBEEF;
    int_ptr[1] = 0xCAFEBABE;
    int_ptr[2] = 0x12345678;
    
    printf("  Written: 0x%x, 0x%x, 0x%x\n", int_ptr[0], int_ptr[1], int_ptr[2]);
    
    if (int_ptr[0] == 0xDEADBEEF && int_ptr[1] == 0xCAFEBABE && int_ptr[2] == 0x12345678) {
        printf("  OK: Memory read/write successful\n");
    } else {
        printf("  Error: Memory corruption detected!\n");
    }
    
    // 测试 4: 再次分配更多内存
    printf("\n[4] Allocate another 8192 bytes:\n");
    void *ptr2 = sbrk(8192);
    if (ptr2 == (void *)-1) {
        printf("  Error: sbrk(8192) failed\n");
        return;
    }
    printf("  Old heap end: 0x%x\n", (uint32_t)ptr2);
    
    new_brk = sbrk(0);
    printf("  New heap end: 0x%x\n", (uint32_t)new_brk);
    printf("  Total allocated from initial: %u bytes\n", (uint32_t)new_brk - (uint32_t)initial_brk);
    
    // 测试 5: 使用 brk 直接设置堆位置
    printf("\n[5] Use brk() to extend heap:\n");
    uint32_t target_addr = (uint32_t)new_brk + 4096;
    void *result = brk((void *)target_addr);
    if (result == (void *)-1) {
        printf("  Error: brk(0x%x) failed\n", target_addr);
    } else {
        printf("  OK: brk returned 0x%x\n", (uint32_t)result);
        void *current = sbrk(0);
        printf("  Current heap end: 0x%x\n", (uint32_t)current);
    }
    
    // 测试 6: 验证之前的数据没有被破坏
    printf("\n[6] Verify previous data integrity:\n");
    if (int_ptr[0] == 0xDEADBEEF && int_ptr[1] == 0xCAFEBABE && int_ptr[2] == 0x12345678) {
        printf("  OK: Previous data still intact\n");
    } else {
        printf("  Error: Data corruption after heap expansion!\n");
    }
    
    printf("\n[Summary] Heap operations completed successfully\n");
}

// 测试 mmap/munmap 系统调用
static void test_mmap(void) {
    printf("\n=== Testing mmap()/munmap() ===\n");
    
    // 测试 1: 基本匿名映射 - 分配一页内存
    printf("\n[1] Anonymous mmap (4096 bytes):\n");
    void *ptr1 = mmap(NULL, 4096, PROT_READ | PROT_WRITE, 
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr1 == MAP_FAILED) {
        printf("  Error: mmap failed\n");
        return;
    }
    printf("  Mapped at: 0x%x\n", (uint32_t)ptr1);
    
    // 验证映射的内存是零初始化的
    uint32_t *int_ptr = (uint32_t *)ptr1;
    int is_zeroed = 1;
    for (int i = 0; i < 16; i++) {
        if (int_ptr[i] != 0) {
            is_zeroed = 0;
            break;
        }
    }
    if (is_zeroed) {
        printf("  OK: Memory is zero-initialized\n");
    } else {
        printf("  Warning: Memory not zero-initialized\n");
    }
    
    // 测试 2: 写入和读取 mmap 的内存
    printf("\n[2] Write/read mmap memory:\n");
    int_ptr[0] = 0xDEADBEEF;
    int_ptr[1] = 0xCAFEBABE;
    int_ptr[2] = 0x12345678;
    int_ptr[255] = 0xFEEDFACE;  // 最后一个 uint32_t（接近页末尾）
    
    printf("  Written: 0x%x, 0x%x, 0x%x, ..., 0x%x\n", 
           int_ptr[0], int_ptr[1], int_ptr[2], int_ptr[255]);
    
    if (int_ptr[0] == 0xDEADBEEF && int_ptr[1] == 0xCAFEBABE && 
        int_ptr[2] == 0x12345678 && int_ptr[255] == 0xFEEDFACE) {
        printf("  OK: Memory read/write successful\n");
    } else {
        printf("  Error: Memory corruption detected!\n");
    }
    
    // 测试 3: 分配多页内存
    printf("\n[3] Anonymous mmap (16384 bytes = 4 pages):\n");
    void *ptr2 = mmap(NULL, 16384, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr2 == MAP_FAILED) {
        printf("  Error: mmap failed\n");
    } else {
        printf("  Mapped at: 0x%x\n", (uint32_t)ptr2);
        
        // 写入每个页的开头
        uint32_t *mp = (uint32_t *)ptr2;
        mp[0] = 0x11111111;           // 第 1 页
        mp[1024] = 0x22222222;        // 第 2 页
        mp[2048] = 0x33333333;        // 第 3 页
        mp[3072] = 0x44444444;        // 第 4 页
        
        if (mp[0] == 0x11111111 && mp[1024] == 0x22222222 &&
            mp[2048] == 0x33333333 && mp[3072] == 0x44444444) {
            printf("  OK: Multi-page read/write successful\n");
        } else {
            printf("  Error: Multi-page memory corruption!\n");
        }
    }
    
    // 测试 4: munmap 释放第一个映射
    printf("\n[4] munmap first mapping:\n");
    int ret = munmap(ptr1, 4096);
    if (ret == 0) {
        printf("  OK: munmap succeeded\n");
    } else {
        printf("  Error: munmap failed\n");
    }
    
    // 测试 5: 再次分配内存，可能会复用刚释放的地址
    printf("\n[5] Allocate again after munmap:\n");
    void *ptr3 = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr3 == MAP_FAILED) {
        printf("  Error: mmap failed\n");
    } else {
        printf("  Mapped at: 0x%x\n", (uint32_t)ptr3);
        if ((uint32_t)ptr3 == (uint32_t)ptr1) {
            printf("  Note: Address was reused (expected behavior)\n");
        }
    }
    
    // 测试 6: munmap 多页映射
    printf("\n[6] munmap multi-page mapping:\n");
    if (ptr2 != MAP_FAILED) {
        ret = munmap(ptr2, 16384);
        if (ret == 0) {
            printf("  OK: munmap 4 pages succeeded\n");
        } else {
            printf("  Error: munmap failed\n");
        }
    }
    
    // 测试 7: 只读映射测试
    printf("\n[7] Read-only mmap:\n");
    void *ptr_ro = mmap(NULL, 4096, PROT_READ,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr_ro == MAP_FAILED) {
        printf("  Error: mmap failed\n");
    } else {
        printf("  Mapped read-only at: 0x%x\n", (uint32_t)ptr_ro);
        // 读取应该成功
        uint32_t val = *(uint32_t *)ptr_ro;
        printf("  OK: Read value: 0x%x (should be 0)\n", val);
        // 注意：写入会触发页错误，这里不测试
        munmap(ptr_ro, 4096);
    }
    
    // 测试 8: 清理最后一个映射
    printf("\n[8] Cleanup:\n");
    if (ptr3 != MAP_FAILED) {
        ret = munmap(ptr3, 4096);
        if (ret == 0) {
            printf("  OK: Final cleanup succeeded\n");
        }
    }
    
    printf("\n[Summary] mmap/munmap tests completed\n");
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
    
    // 运行 brk/sbrk 测试
    test_brk();
    
    // 运行 mmap/munmap 测试
    test_mmap();
    
    printf("\n========================================\n");
    printf("    All tests completed!\n");
    printf("========================================\n");
    
    exit(0);
}
