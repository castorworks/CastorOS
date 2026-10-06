// ramfs - 内存文件系统服务
//
// 非特权的用户态服务，以 "fs" 登记。协议的处理在 user/lib 的 fs_server 里，
// 这里只是存储后端：一张平的表，每一项是一个文件或者一个目录，名字是完整的路径
// （目录的规则在 fs_server 里）；文件内容放在自己 mmap 来的内存里。
// 启动时装载构建时嵌进来的启动映像（user/bootfs 里的文件加上 Makefile 里列出的程序）。

#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <fs.h>
#include <fs_server.h>

#define MAX_FILES       128
#define MAX_FILE_SIZE   (4u * 1024 * 1024)

struct file {
    bool used;
    bool dir;               // 是目录（没有内容）
    char name[FS_NAME_MAX]; // 完整的路径
    char *data;             // mmap 来的，capacity 字节
    uint32_t size;
    uint32_t capacity;
};

static struct file files[MAX_FILES];

// ============================================================================
// 存储后端（文件号就是 files[] 的下标）
// ============================================================================

static int ramfs_find(const char *name) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int ramfs_create(const char *name, bool directory) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (!files[i].used) {
            memset(&files[i], 0, sizeof(files[i]));
            files[i].used = true;
            files[i].dir = directory;
            strcpy(files[i].name, name);
            return i;
        }
    }
    return -1;
}

static bool ramfs_is_dir(int file) {
    return files[file].dir;
}

static const char *ramfs_path(int file) {
    return files[file].name;
}

// 保证文件至少能放下 size 字节
static bool reserve(struct file *f, uint32_t size) {
    if (size <= f->capacity) {
        return true;
    }
    uint32_t capacity = f->capacity ? f->capacity : 4096;
    while (capacity < size) {
        capacity *= 2;
    }
    char *data = (char *)mmap(NULL, capacity, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (data == MAP_FAILED) {
        return false;
    }
    if (f->data) {
        memcpy(data, f->data, f->size);
        munmap(f->data, f->capacity);
    }
    f->data = data;
    f->capacity = capacity;
    return true;
}

static long ramfs_size(int file) {
    return files[file].size;
}

static long ramfs_read(int file, uint32_t offset, char *buf, uint32_t len) {
    struct file *f = &files[file];
    if (offset >= f->size) {
        return 0;
    }
    if (len > f->size - offset) {
        len = f->size - offset;
    }
    memcpy(buf, f->data + offset, len);
    return len;
}

static long ramfs_write(int file, uint32_t offset, const char *buf, uint32_t len) {
    struct file *f = &files[file];
    if (offset > MAX_FILE_SIZE || len > MAX_FILE_SIZE - offset || !reserve(f, offset + len)) {
        return -1;
    }
    if (offset > f->size) {
        memset(f->data + f->size, 0, offset - f->size);     // 空洞填 0
    }
    memcpy(f->data + offset, buf, len);
    if (offset + len > f->size) {
        f->size = offset + len;
    }
    return len;
}

static int ramfs_truncate(int file) {
    files[file].size = 0;
    return 0;
}

static int ramfs_remove(int file) {
    if (files[file].data) {
        munmap(files[file].data, files[file].capacity);
    }
    files[file].used = false;
    return 0;
}

static int ramfs_entry(int index) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && index-- == 0) {
            return i;
        }
    }
    return -1;
}

static const struct fs_backend ramfs_backend = {
    ramfs_find, ramfs_create, ramfs_is_dir, ramfs_path, ramfs_size, ramfs_read, ramfs_write,
    ramfs_truncate, ramfs_remove, ramfs_entry,
};

// ============================================================================
// 启动映像：构建时打好的 ustar 归档（bootfs.S），启动时展开成文件
// ============================================================================

extern "C" const char bootfs_start[], bootfs_end[];

// ustar 头里的数字是八进制 ASCII
static uint32_t parse_octal(const char *field, size_t len) {
    uint32_t value = 0;
    for (size_t i = 0; i < len && field[i] >= '0' && field[i] <= '7'; i++) {
        value = value * 8 + (uint32_t)(field[i] - '0');
    }
    return value;
}

static int load_bootfs(void) {
    int count = 0;
    const char *p = bootfs_start;
    // 每个成员：512 字节的头（名字在 0，大小在 124，类型在 156），后面是按 512 对齐的内容
    while (p + 512 <= bootfs_end && p[0] != '\0') {
        uint32_t size = parse_octal(p + 124, 12);
        char type = p[156];
        const char *content = p + 512;
        if (content + size > bootfs_end) {
            break;
        }

        // 名字是完整的路径；目录成员（类型 '5'）的名字以 '/' 结尾。归档里目录排在它里面的
        // 文件前面，所以照顺序建就行
        char name[FS_NAME_MAX];
        const char *raw = p;
        if (raw[0] == '.' && raw[1] == '/') {
            raw += 2;
        }
        size_t len = strlen(raw) < 100 ? strlen(raw) : 100;     // 头里的名字字段是 100 字节
        while (len > 0 && raw[len - 1] == '/') {
            len--;
        }
        if (len > 0 && len < FS_NAME_MAX) {
            memcpy(name, raw, len);
            name[len] = '\0';
            if (type == '5' && ramfs_find(name) < 0) {
                ramfs_create(name, true);
            } else if (type == '0' || type == '\0') {
                int index = ramfs_create(name, false);
                if (index >= 0 && reserve(&files[index], size ? size : 1)) {
                    memcpy(files[index].data, content, size);
                    files[index].size = size;
                    count++;
                }
            }
        }
        p = content + ((size + 511) & ~511u);
    }
    return count;
}

int main() {
    int boot_files = load_bootfs();
    printf("ramfs: ready (pid %d), %d files from the boot image\n", getpid(), boot_files);
    fs_serve(FS_SERVICE_NAME, &ramfs_backend);
    printf("ramfs: cannot register name\n");
    return 1;
}
