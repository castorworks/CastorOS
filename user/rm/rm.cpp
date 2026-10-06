// rm - 删除文件

#include <stdio.h>
#include <fs.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        eprintf("usage: rm <file>...\n");
        return 1;
    }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        if (fs_unlink(argv[i]) != 0) {
            eprintf("rm: cannot remove %s (no such file, or a directory that is not empty)\n", argv[i]);
            status = 1;
        }
    }
    return status;
}
