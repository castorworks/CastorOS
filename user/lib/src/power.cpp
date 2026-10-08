/**
 * 关机和重启的客户端：向 init（PID 1）发请求
 */

#include <power.h>
#include <names.h>

int power_request(int action) {
    struct ipc_msg m = {};
    m.label = POWER_REQUEST;
    m.data[0] = (uint64_t)action;
    ipc_call(NAME_SERVER_PID, &m);
    return -1;
}
