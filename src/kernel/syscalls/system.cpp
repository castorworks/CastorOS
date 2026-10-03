#include <kernel/syscalls/system.h>
#include <kernel/utsname.h>
#include <kernel/version.h>
#include <kernel/system.h>
#include <lib/string.h>
#include <lib/klog.h>

uint32_t sys_reboot(void) {
    system_reboot();
    return 0;
}

uint32_t sys_poweroff(void) {
    system_poweroff();
    return 0;
}

/**
 * sys_uname - 获取系统信息
 * @param buf 用户空间 utsname 结构体指针
 * @return 0 成功，(uint32_t)-1 失败
 */
uint32_t sys_uname(struct utsname *buf) {
    if (!buf) {
        LOG_ERROR_MSG("sys_uname: buf is NULL\n");
        return (uint32_t)-1;
    }
    
    // 清零结构体
    memset(buf, 0, sizeof(struct utsname));
    
    // 填充系统信息
    strcpy(buf->sysname, "CastorOS");           // 操作系统名称
    strcpy(buf->nodename, "castor");            // 网络节点名称（主机名）
    strcpy(buf->release, KERNEL_VERSION);       // 内核版本号
    
    // 版本信息：编译日期和时间
    // 使用简单的字符串拼接，因为 snprintf 可能不可用
    strcpy(buf->version, "#1 ");
    strcat(buf->version, __DATE__);
    strcat(buf->version, " ");
    strcat(buf->version, __TIME__);
    
    strcpy(buf->machine, "i386");               // 硬件类型
    
    LOG_DEBUG_MSG("sys_uname: sysname=%s, release=%s, machine=%s\n",
                  buf->sysname, buf->release, buf->machine);
    
    return 0;
}
