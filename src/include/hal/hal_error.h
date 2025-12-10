/**
 * @file hal_error.h
 * @brief HAL 统一错误码定义
 * 
 * 定义所有 HAL 函数使用的统一错误码，确保跨架构的错误处理一致性。
 * 
 * @see Requirements 12.1, 12.2, 12.3, 12.4
 */

#ifndef _HAL_HAL_ERROR_H_
#define _HAL_HAL_ERROR_H_

/**
 * @brief HAL 错误码枚举
 * 
 * 所有可能失败的 HAL 函数应返回此类型。
 * 成功返回 HAL_OK (0)，失败返回负值错误码。
 */
typedef enum hal_error {
    HAL_OK = 0,                 /**< 操作成功 */
    HAL_ERR_INVALID_PARAM = -1, /**< 无效参数 */
    HAL_ERR_NO_MEMORY = -2,     /**< 内存不足 */
    HAL_ERR_NOT_SUPPORTED = -3, /**< 当前架构不支持此操作 */
    HAL_ERR_NOT_FOUND = -4,     /**< 请求的资源未找到 */
    HAL_ERR_BUSY = -5,          /**< 资源忙，请稍后重试 */
    HAL_ERR_TIMEOUT = -6,       /**< 操作超时 */
    HAL_ERR_IO = -7,            /**< I/O 错误 */
    HAL_ERR_PERMISSION = -8,    /**< 权限不足 */
    HAL_ERR_ALREADY_EXISTS = -9,/**< 资源已存在 */
    HAL_ERR_NOT_INITIALIZED = -10, /**< 子系统未初始化 */
} hal_error_t;

/**
 * @brief 检查 HAL 操作是否成功
 * @param err hal_error_t 返回值
 * @return 如果操作成功返回非零值
 */
#define HAL_SUCCESS(err) ((err) == HAL_OK)

/**
 * @brief 检查 HAL 操作是否失败
 * @param err hal_error_t 返回值
 * @return 如果操作失败返回非零值
 */
#define HAL_FAILED(err) ((err) != HAL_OK)

/**
 * @brief 获取错误码的字符串描述
 * @param err 错误码
 * @return 错误描述字符串
 */
static inline const char *hal_error_string(hal_error_t err) {
    switch (err) {
        case HAL_OK:                return "Success";
        case HAL_ERR_INVALID_PARAM: return "Invalid parameter";
        case HAL_ERR_NO_MEMORY:     return "Out of memory";
        case HAL_ERR_NOT_SUPPORTED: return "Operation not supported";
        case HAL_ERR_NOT_FOUND:     return "Resource not found";
        case HAL_ERR_BUSY:          return "Resource busy";
        case HAL_ERR_TIMEOUT:       return "Operation timed out";
        case HAL_ERR_IO:            return "I/O error";
        case HAL_ERR_PERMISSION:    return "Permission denied";
        case HAL_ERR_ALREADY_EXISTS:return "Resource already exists";
        case HAL_ERR_NOT_INITIALIZED: return "Not initialized";
        default:                    return "Unknown error";
    }
}

#endif /* _HAL_HAL_ERROR_H_ */
