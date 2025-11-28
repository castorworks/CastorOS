/**
 * @file netbuf.c
 * @brief 网络缓冲区管理实现
 */

#include <net/netbuf.h>
#include <mm/heap.h>
#include <lib/string.h>

netbuf_t *netbuf_alloc(uint32_t size) {
    uint32_t total_size = NETBUF_HEADROOM + size;
    if (total_size > NETBUF_MAX_SIZE) {
        total_size = NETBUF_MAX_SIZE;
    }
    
    netbuf_t *buf = (netbuf_t *)kmalloc(sizeof(netbuf_t));
    if (!buf) {
        return NULL;
    }
    
    buf->head = (uint8_t *)kmalloc(total_size);
    if (!buf->head) {
        kfree(buf);
        return NULL;
    }
    
    memset(buf->head, 0, total_size);
    
    buf->data = buf->head + NETBUF_HEADROOM;
    buf->tail = buf->data;
    buf->end = buf->head + total_size;
    buf->len = 0;
    buf->total_size = total_size;
    
    buf->mac_header = NULL;
    buf->network_header = NULL;
    buf->transport_header = NULL;
    buf->dev = NULL;
    buf->next = NULL;
    
    return buf;
}

void netbuf_free(netbuf_t *buf) {
    if (buf) {
        if (buf->head) {
            kfree(buf->head);
        }
        kfree(buf);
    }
}

uint8_t *netbuf_push(netbuf_t *buf, uint32_t len) {
    if (!buf || buf->data - buf->head < (int)len) {
        return NULL;  // 没有足够的 headroom
    }
    buf->data -= len;
    buf->len += len;
    return buf->data;
}

uint8_t *netbuf_pull(netbuf_t *buf, uint32_t len) {
    if (!buf || buf->len < len) {
        return NULL;
    }
    buf->data += len;
    buf->len -= len;
    return buf->data;
}

uint8_t *netbuf_put(netbuf_t *buf, uint32_t len) {
    if (!buf || buf->end - buf->tail < (int)len) {
        return NULL;  // 没有足够的 tailroom
    }
    uint8_t *old_tail = buf->tail;
    buf->tail += len;
    buf->len += len;
    return old_tail;
}

netbuf_t *netbuf_clone(netbuf_t *buf) {
    if (!buf) {
        return NULL;
    }
    
    netbuf_t *new_buf = netbuf_alloc(buf->len);
    if (!new_buf) {
        return NULL;
    }
    
    memcpy(new_buf->data, buf->data, buf->len);
    new_buf->tail = new_buf->data + buf->len;
    new_buf->len = buf->len;
    new_buf->dev = buf->dev;
    
    return new_buf;
}

void netbuf_reset(netbuf_t *buf) {
    if (!buf) {
        return;
    }
    
    buf->data = buf->head + NETBUF_HEADROOM;
    buf->tail = buf->data;
    buf->len = 0;
    buf->mac_header = NULL;
    buf->network_header = NULL;
    buf->transport_header = NULL;
}

uint32_t netbuf_headroom(netbuf_t *buf) {
    if (!buf) {
        return 0;
    }
    return buf->data - buf->head;
}

uint32_t netbuf_tailroom(netbuf_t *buf) {
    if (!buf) {
        return 0;
    }
    return buf->end - buf->tail;
}

