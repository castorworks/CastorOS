// wc - 数标准输入的行数、词数、字节数
//
//   ls | wc        wc < readme.txt

#include <stdio.h>

int main() {
    unsigned lines = 0, words = 0, bytes = 0;
    bool in_word = false;
    char buf[256];
    long n;
    while ((n = read_input(buf, sizeof(buf))) > 0) {
        for (long i = 0; i < n; i++) {
            char c = buf[i];
            bytes++;
            if (c == '\n') {
                lines++;
            }
            bool space = c == ' ' || c == '\n' || c == '\t' || c == '\r';
            if (!space && !in_word) {
                words++;
            }
            in_word = !space;
        }
    }
    printf("%u %u %u\n", lines, words, bytes);
    return 0;
}
