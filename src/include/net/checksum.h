/**
 * @file checksum.h
 * @brief 网络校验和计算
 * 
 * 实现 Internet 校验和算法（RFC 1071），用于 IP、ICMP、TCP、UDP 等协议。
 */

#ifndef _NET_CHECKSUM_H_
#define _NET_CHECKSUM_H_

#include <types.h>

/**
 * @brief 计算 Internet 校验和
 * @param data 数据指针
 * @param len 数据长度
 * @return 校验和（网络字节序）
 */
uint16_t checksum(void *data, int len);

/**
 * @brief 增量计算校验和（累加部分）
 * @param sum 当前累加值
 * @param data 数据指针
 * @param len 数据长度
 * @return 更新后的累加值
 */
uint32_t checksum_partial(uint32_t sum, void *data, int len);

/**
 * @brief 完成校验和计算（折叠并取反）
 * @param sum 累加值
 * @return 最终校验和
 */
uint16_t checksum_finish(uint32_t sum);

/**
 * @brief 验证校验和是否正确
 * @param data 数据指针
 * @param len 数据长度
 * @return true 校验和正确，false 校验和错误
 */
bool checksum_verify(void *data, int len);

#endif // _NET_CHECKSUM_H_

