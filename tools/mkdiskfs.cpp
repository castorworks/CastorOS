// mkdiskfs - 在宿主机上做出磁盘文件系统（user/diskfs）的映像
//
//   mkdiskfs <映像文件> <大小，MB> <目录>
//
// 把 <目录> 下的整棵树放进映像：这就是根文件系统，系统从它启动。内核里的 diskfs 从不
// 格式化，文件系统只在这里产生。
//
// 映像已经存在、而且是同样大小的一个文件系统时，不是推倒重来：原来的文件留着，只有
// <目录> 里有的那些被换成新的。这样重新构建之后系统自带的文件更新了，自己放进去的还在。
//
// 用宿主机的编译器编译（make 自动做），和 diskfs 共用 <diskfs_format.h> 里的格式定义。

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>

#include "../user/lib/include/diskfs_format.h"

struct item {
    bool is_dir = false;
    std::vector<char> data;
};

// 路径（从根写起，不带开头的 '/'） -> 内容。map 按路径排序，所以目录排在它里面的东西前面
static std::map<std::string, item> files;

[[noreturn]] static void fail(const char *what, const std::string &detail) {
    fprintf(stderr, "mkdiskfs: %s: %s\n", what, detail.c_str());
    exit(1);
}

// ---------------------------------------------------------------------------
// 读：宿主机上的目录树，和已有的映像
// ---------------------------------------------------------------------------

static void read_tree(const std::string &host_dir, const std::string &prefix) {
    DIR *d = opendir(host_dir.c_str());
    if (!d) {
        fail("cannot read directory", host_dir);
    }
    while (struct dirent *e = readdir(d)) {
        std::string name = e->d_name;
        if (name[0] == '.') {
            continue;           // "."、".."，以及 .DS_Store 之类不该进映像的东西
        }
        std::string host_path = host_dir + "/" + name;
        std::string path = prefix.empty() ? name : prefix + "/" + name;
        if (path.size() >= DISKFS_NAME_MAX) {
            fail("path too long", path);
        }
        struct stat st;
        if (stat(host_path.c_str(), &st) != 0) {
            fail("cannot stat", host_path);
        }
        item it;
        if (S_ISDIR(st.st_mode)) {
            it.is_dir = true;
            files[path] = it;
            read_tree(host_path, path);
        } else {
            FILE *f = fopen(host_path.c_str(), "rb");
            if (!f) {
                fail("cannot open", host_path);
            }
            it.data.resize((size_t)st.st_size);
            if (st.st_size > 0 && fread(it.data.data(), 1, it.data.size(), f) != it.data.size()) {
                fail("cannot read", host_path);
            }
            fclose(f);
            files[path] = it;
        }
    }
    closedir(d);
}

/** 映像里已有的文件系统的全部内容。不是一个 total_blocks 块的文件系统就什么都不读 */
static void read_image(const char *image, uint32_t total_blocks) {
    FILE *f = fopen(image, "rb");
    if (!f) {
        return;
    }
    std::vector<char> disk((size_t)total_blocks * DISKFS_BLOCK_SIZE);
    size_t got = fread(disk.data(), 1, disk.size(), f);
    bool whole = got == disk.size() && fgetc(f) == EOF;
    fclose(f);

    struct diskfs_superblock sb;
    memcpy(&sb, disk.data(), sizeof(sb));
    if (!whole || memcmp(sb.magic, DISKFS_MAGIC, DISKFS_MAGIC_SIZE) != 0 || sb.version != DISKFS_VERSION ||
        sb.total_blocks != total_blocks || sb.dir_blocks != DISKFS_DIR_BLOCKS ||
        sb.dir_start + sb.dir_blocks > total_blocks || sb.fat_start + sb.fat_blocks > total_blocks) {
        return;
    }
    const uint32_t *fat = (const uint32_t *)(disk.data() + (size_t)sb.fat_start * DISKFS_BLOCK_SIZE);
    const struct diskfs_dir_entry *dir =
        (const struct diskfs_dir_entry *)(disk.data() + (size_t)sb.dir_start * DISKFS_BLOCK_SIZE);
    for (size_t i = 0; i < DISKFS_MAX_FILES; i++) {
        if (dir[i].name[0] == '\0') {
            continue;
        }
        item it;
        it.is_dir = (dir[i].flags & DISKFS_ENTRY_DIR) != 0;
        uint32_t block = dir[i].first_block;
        while (it.data.size() < dir[i].size && block >= sb.data_start && block < total_blocks) {
            const char *p = disk.data() + (size_t)block * DISKFS_BLOCK_SIZE;
            size_t n = dir[i].size - it.data.size();
            it.data.insert(it.data.end(), p, p + (n < DISKFS_BLOCK_SIZE ? n : DISKFS_BLOCK_SIZE));
            block = fat[block];
        }
        files[std::string(dir[i].name, strnlen(dir[i].name, DISKFS_NAME_MAX))] = it;
    }
}

// ---------------------------------------------------------------------------
// 写
// ---------------------------------------------------------------------------

static void write_image(const char *image, uint32_t total_blocks) {
    struct diskfs_superblock sb = {};
    memcpy(sb.magic, DISKFS_MAGIC, DISKFS_MAGIC_SIZE);
    sb.version = DISKFS_VERSION;
    sb.total_blocks = total_blocks;
    sb.fat_start = 1;
    sb.fat_blocks = (uint32_t)DISKFS_FAT_BLOCKS(total_blocks);
    sb.dir_start = sb.fat_start + sb.fat_blocks;
    sb.dir_blocks = DISKFS_DIR_BLOCKS;
    sb.data_start = sb.dir_start + sb.dir_blocks;
    if (sb.data_start >= total_blocks) {
        fail("image too small", std::to_string(total_blocks) + " blocks");
    }
    if (files.size() > DISKFS_MAX_FILES) {
        fail("too many files", std::to_string(files.size()));
    }

    std::vector<char> disk((size_t)total_blocks * DISKFS_BLOCK_SIZE);
    memcpy(disk.data(), &sb, sizeof(sb));
    uint32_t *fat = (uint32_t *)(disk.data() + (size_t)sb.fat_start * DISKFS_BLOCK_SIZE);
    struct diskfs_dir_entry *dir =
        (struct diskfs_dir_entry *)(disk.data() + (size_t)sb.dir_start * DISKFS_BLOCK_SIZE);
    for (uint32_t b = 0; b < sb.data_start; b++) {
        fat[b] = DISKFS_FAT_RESERVED;
    }

    uint32_t next = sb.data_start;      // 文件一个接一个地放
    size_t slot = 0;
    for (const auto &[path, it] : files) {
        struct diskfs_dir_entry *e = &dir[slot++];
        memcpy(e->name, path.c_str(), path.size());
        e->size = (uint32_t)it.data.size();
        e->flags = it.is_dir ? DISKFS_ENTRY_DIR : 0;
        e->first_block = DISKFS_FAT_END;
        uint32_t blocks = (uint32_t)((it.data.size() + DISKFS_BLOCK_SIZE - 1) / DISKFS_BLOCK_SIZE);
        if (blocks > total_blocks - next) {
            fail("image too small for", path);
        }
        if (blocks > 0) {
            e->first_block = next;
            memcpy(disk.data() + (size_t)next * DISKFS_BLOCK_SIZE, it.data.data(), it.data.size());
            for (uint32_t i = 0; i < blocks; i++) {
                fat[next + i] = i + 1 < blocks ? next + i + 1 : DISKFS_FAT_END;
            }
            next += blocks;
        }
    }

    FILE *f = fopen(image, "wb");
    if (!f || fwrite(disk.data(), 1, disk.size(), f) != disk.size() || fclose(f) != 0) {
        fail("cannot write", image);
    }
    printf("[OK] file system image: %s (%zu files and directories, %u of %u blocks used)\n", image,
           files.size(), next, total_blocks);
}

int main(int argc, char **argv) {
    if (argc != 4 || atoi(argv[2]) <= 0) {
        fprintf(stderr, "usage: mkdiskfs <image> <size in MB> <directory>\n");
        return 1;
    }
    uint64_t blocks = (uint64_t)atoi(argv[2]) * 1024 * 1024 / DISKFS_BLOCK_SIZE;
    if (blocks > DISKFS_MAX_BLOCKS) {
        fail("image too large", argv[2]);
    }
    read_image(argv[1], (uint32_t)blocks);      // 原来有的
    read_tree(argv[3], "");                     // 被这次的盖掉
    write_image(argv[1], (uint32_t)blocks);
    return 0;
}
