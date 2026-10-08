// clear - 清屏
//
// 屏幕不归程序管，程序能做的是在输出里放转义序列：ESC [ H 把光标移到左上角，ESC [ 2 J
// 清掉整个屏幕。串口另一头的终端认这两个序列，内核的屏幕驱动（PC 的显示器）也认。

#include <stdio.h>

int main() {
    printf("\033[H\033[2J");
    return 0;
}
