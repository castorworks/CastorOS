#ifndef _USERLAND_LIB_USB_H_
#define _USERLAND_LIB_USB_H_

// USB：每个设备都听得懂的那部分——标准的设备请求和描述符的种类。
//
// 主机和设备说话都经过“控制传输”：一个 8 字节的请求（类型、编号、两个参数、数据长度），
// 可选的数据，最后是应答。设备用一串“描述符”介绍自己：一个配置描述符后面跟着它的接口
// 描述符，每个接口后面跟着它的端点描述符；每个描述符开头两个字节是长度和种类。
// 控制器（EHCI、UHCI）和设备的种类（U 盘、键盘）各有自己的驱动，这里是它们共用的常量。

// 请求的类型：最高位是数据的方向（1 = 设备到主机），低位是发给谁
#define USB_TYPE_TO_DEVICE          0x00
#define USB_TYPE_FROM_DEVICE        0x80
#define USB_TYPE_TO_ENDPOINT        0x02
#define USB_TYPE_CLASS_TO_INTERFACE 0x21    // 设备种类自己定义的请求，发给一个接口

#define USB_TYPE_CLASS_TO_PORT       0x23    // 集线器的请求，发给它的一个口
#define USB_TYPE_CLASS_FROM_DEVICE  0xA0
#define USB_TYPE_CLASS_FROM_PORT    0xA3

// 标准请求
#define USB_REQ_GET_STATUS          0
#define USB_REQ_CLEAR_FEATURE       1
#define USB_REQ_SET_FEATURE         3
#define USB_REQ_SET_ADDRESS         5
#define USB_REQ_GET_DESCRIPTOR      6
#define USB_REQ_SET_CONFIGURATION   9
#define USB_FEATURE_ENDPOINT_HALT   0

// 描述符的种类（GET_DESCRIPTOR 的 value 的高字节）
#define USB_DESC_DEVICE             1
#define USB_DESC_CONFIGURATION      2
#define USB_DESC_INTERFACE          4
#define USB_DESC_ENDPOINT           5

// 设备的种类（设备描述符的第 4 个字节；大多数设备这里是 0，种类写在接口描述符里）
#define USB_CLASS_HUB               9

// 端点描述符：地址的最高位是方向，属性的低两位是传输的种类
#define USB_ENDPOINT_IN             0x80
#define USB_ENDPOINT_TYPE_MASK      3
#define USB_ENDPOINT_TYPE_BULK      2
#define USB_ENDPOINT_TYPE_INTERRUPT 3

#endif // _USERLAND_LIB_USB_H_
