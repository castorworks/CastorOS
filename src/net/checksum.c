/**
 * @file checksum.c
 * @brief 网络校验和计算实现
 * 
 * 实现 RFC 1071 定义的 Internet 校验和算法。
 * 算法: 将数据按16位求和，高位折叠到低位，最后取反。
 */

#include <net/checksum.h>

uint32_t checksum_partial(uint32_t sum, void *data, int len) {
    uint16_t *ptr = (uint16_t *)data;
    
    // 按16位累加
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    // 处理奇数长度（剩余1字节）
    // 在网络字节序中，最后一个字节应该作为高字节处理
    if (len == 1) {
        // 使用联合体确保字节对齐和正确的字节序处理
        union {
            uint8_t bytes[2];
            uint16_t word;
        } odd;
        odd.bytes[0] = *(uint8_t *)ptr;
        odd.bytes[1] = 0;
        sum += odd.word;
    }
    
    return sum;
}

uint16_t checksum_finish(uint32_t sum) {
    // 将高16位折叠到低16位
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    // 取反
    return (uint16_t)~sum;
}

uint16_t checksum(void *data, int len) {
    uint32_t sum = checksum_partial(0, data, len);
    return checksum_finish(sum);
}

bool checksum_verify(void *data, int len) {
    uint32_t sum = checksum_partial(0, data, len);
    
    // 折叠高位
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    // 正确的数据校验和应为 0xFFFF（因为包含原校验和字段）
    return (sum == 0xFFFF);
}

