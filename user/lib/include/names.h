#ifndef _USERLAND_LIB_NAMES_H_
#define _USERLAND_LIB_NAMES_H_

// 名字服务：按名字找到服务进程的 PID。
//
// 服务端在 init（PID 1）里，这里是协议和客户端接口。
// 协议：请求的 data[] 里放以 NUL 结尾的名字；应答的 data[0] 是结果。

#define NAME_SERVER_PID 1

/** 名字的最大长度（含结尾 NUL）：正好放满一条消息的 data[] */
#define NAME_MAX        48

enum {
    NAME_REGISTER = 1,  // 把名字登记到发送者名下。应答 data[0]: 0 成功，1 失败（已被占用/表满/名字不合法）
    NAME_LOOKUP   = 2,  // 查询名字。应答 data[0]: 服务的 PID，没有登记则为 0
};

/** 把当前进程登记为 name。@return 0 成功，-1 失败 */
int name_register(const char *name);

/** 查询 name 对应的 PID。@return PID；没有登记返回 0；名字服务不可达返回 -1 */
int name_lookup(const char *name);

/** 等到 name 被登记为止，返回它的 PID；名字服务不可达返回 -1 */
int name_wait(const char *name);

#endif // _USERLAND_LIB_NAMES_H_
