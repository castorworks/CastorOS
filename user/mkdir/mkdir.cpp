// mkdir - 创建目录
//
//   mkdir notes             内存文件系统里的 notes
//   mkdir /home/notes       从根写起的路径
//   mkdir a a/b a/b/c       上一级必须已经存在，所以要一级一级地建

#include <stdio.h>
#include <fs.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        eprintf("usage: mkdir <directory>...\n");
        return 1;
    }
    int failed = 0;
    for (int i = 1; i < argc; i++) {
        if (fs_mkdir(argv[i]) != 0) {
            eprintf("mkdir: cannot create %s\n", argv[i]);
            failed = 1;
        }
    }
    return failed;
}
