/**
 * 名字服务的客户端：向 init（PID 1）发请求
 */

#include <names.h>
#include <syscall.h>
#include <string.h>

/** 发一条带名字的请求，返回应答的 data[0]；失败返回 -1 */
static long name_request(uint32_t label, const char *name) {
    if (!name || strlen(name) >= NAME_MAX) {
        return -1;
    }
    struct ipc_msg m = {};
    m.label = label;
    strcpy((char *)m.data, name);
    if (ipc_call(NAME_SERVER_PID, &m) != 0) {
        return -1;
    }
    return (long)m.data[0];
}

int name_register(const char *name) {
    return name_request(NAME_REGISTER, name) == 0 ? 0 : -1;
}

int name_lookup(const char *name) {
    return (int)name_request(NAME_LOOKUP, name);
}

int name_wait(const char *name) {
    for (;;) {
        int pid = name_lookup(name);
        if (pid != 0) {
            return pid;
        }
        usleep(20000);
    }
}
