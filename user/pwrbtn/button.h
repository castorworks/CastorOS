#ifndef _PWRBTN_BUTTON_H_
#define _PWRBTN_BUTTON_H_

// 电源键这个设备。每种机器一个源文件（acpi_button.cpp、gpio_button.cpp），pwrbtn.cpp 通过
// 这两个函数用它，不关心它是哪一种。

/**
 * 许可给本进程的设备是电源键、而且能用：让它在按下时发中断。
 * @return 没有许可任何设备，或者设备用不了，返回 false
 */
bool button_open(void);

/**
 * 中断来了：是不是电源键按下了。是的话顺带让设备撤销这次中断。
 * 中断线可能和别的设备共用，来的不一定是它。
 */
bool button_pressed(void);

#endif // _PWRBTN_BUTTON_H_
