#ifndef _USBKBD_UHCI_H_
#define _USBKBD_UHCI_H_

// UHCI：USB 1.1 的主控制器，低速和全速设备（键盘、鼠标）归它管。这里是它对上面
// （usbkbd.cpp）提供的接口：看端口、复位端口、控制传输、定期去问一个端点有没有数据。
// 实现和原理在 uhci.cpp。
//
// 一台机器上常有好几个这样的控制器，每个带两个端口；这里从 0 给它们编号。
// 不认集线器，所以一个端口上最多一个设备。

#include <types.h>

#define UHCI_MAX_CONTROLLERS    4
#define UHCI_PORTS              2       // 每个控制器的端口数

/** 一个设备：在哪个控制器上，怎么和它的控制端点说话 */
struct uhci_device {
    int controller;
    uint8_t address;        // 刚复位的设备在地址 0
    bool low_speed;
    uint8_t max_packet;     // 控制端点的最大包长
};

/** 打开许可给本进程的所有控制器：复位、启动、认领中断线。@return 有几个能用 */
int uhci_open(void);

/** 内核发来的一条中断消息：让发了中断的控制器撤销它，重新打开这条中断线 */
void uhci_irq(int irq);

/** 有没有认领到中断线。没有的话传输做完了没人通知，要靠定时去看 */
bool uhci_has_irq(void);

/** 等 ms 毫秒。这期间来的中断照常应答（中断线可能是和别的驱动共用的，不能关着它睡） */
void uhci_sleep(uint32_t ms);

/** 端口上现在插着设备吗 */
bool uhci_port_connected(int controller, int port);

/** 上次问过之后，端口上的设备被拔掉过或者插上过吗（问完就清掉） */
bool uhci_port_changed(int controller, int port);

/**
 * 复位端口上的设备并打开端口：设备之后在地址 0 上。
 * @return 上面有设备，*low_speed 是它的速度；没有设备或者端口打不开返回 false
 */
bool uhci_port_reset(int controller, int port, bool *low_speed);

/** 关掉端口：上面的设备不再收到任何东西 */
void uhci_port_disable(int controller, int port);

/** 一次控制传输能带的数据 */
#define UHCI_CONTROL_MAX    256

/**
 * 一次控制传输：8 字节的请求，可选的数据阶段，状态阶段。
 * request_type 的最高位是数据的方向（1 = 设备到主机）。
 * @return 数据阶段实际传了多少字节；失败返回 -1
 */
long uhci_control(const struct uhci_device *dev, uint8_t request_type, uint8_t request, uint16_t value,
                  uint16_t index, void *data, uint16_t length);

/** 中断端点一次最多给这么多字节 */
#define UHCI_INTERRUPT_MAX  64

/**
 * 开始定期（每 8 毫秒）去问 port 上那个设备的 endpoint 号端点有没有数据（“中断传输”：
 * 名字如此，其实是主机在轮询）。每个端口同时只能问一个端点。
 */
void uhci_interrupt_start(const struct uhci_device *dev, int port, uint8_t endpoint, uint16_t max_packet);

/**
 * 那个端点给数据了吗。给了的话抄到 data（至少 UHCI_INTERRUPT_MAX 字节），接着问下一次。
 * @return 字节数；-1 还没有；-2 这一次出错了（设备被拔掉了，或者它把端点挂起了）
 */
long uhci_interrupt_poll(int controller, int port, void *data);

/** 不再问了 */
void uhci_interrupt_stop(int controller, int port);

#endif // _USBKBD_UHCI_H_
