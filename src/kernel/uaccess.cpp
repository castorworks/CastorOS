// ============================================================================
// uaccess.cpp - 用户指针校验
// ============================================================================

#include <kernel/uaccess.h>
#include <hal/hal.h>
#include <mm/vmm.h>

namespace kernel {

/* 用户空间下界：第 0 页永不映射，空指针一律拒绝 */
static const uintptr_t USER_ACCESS_START = PAGE_SIZE;

uintptr_t UAccess::user_end() {
#if defined(ARCH_I686)
    return 0x80000000UL;
#else
    return 0x0000800000000000ULL;
#endif
}

/* 区间是否落在用户地址范围内（含回绕检查） */
static bool range_in_user(uintptr_t start, size_t len) {
    uintptr_t end = start + len;
    if (end < start) {
        return false;
    }
    return start >= USER_ACCESS_START && end <= UAccess::user_end();
}

/* 单页是否以用户权限映射；need_write 时要求可写，COW 页先完成复制 */
static bool page_ok(uintptr_t page, bool need_write) {
    uint32_t flags = 0;
    if (!hal::Mmu::query(HAL_ADDR_SPACE_CURRENT, (vaddr_t)page, NULL, &flags)) {
        return false;
    }
    if (!(flags & HAL_PAGE_USER)) {
        return false;
    }
    if (!need_write || (flags & HAL_PAGE_WRITE)) {
        return true;
    }
    if (!(flags & HAL_PAGE_COW)) {
        return false;
    }
    /* error_code 0x7 = 页存在 + 写 + 用户态，与缺页路径一致 */
    if (!mm::Vmm::handle_cow_page_fault(page, 0x7)) {
        return false;
    }
    flags = 0;
    return hal::Mmu::query(HAL_ADDR_SPACE_CURRENT, (vaddr_t)page, NULL, &flags) &&
           (flags & HAL_PAGE_USER) && (flags & HAL_PAGE_WRITE);
}

static bool range_ok(uintptr_t start, size_t len, bool need_write) {
    if (len == 0) {
        return true;
    }
    if (!range_in_user(start, len)) {
        return false;
    }
    uintptr_t last = PAGE_ALIGN_DOWN(start + len - 1);
    for (uintptr_t page = PAGE_ALIGN_DOWN(start); ; page += PAGE_SIZE) {
        if (!page_ok(page, need_write)) {
            return false;
        }
        if (page == last) {
            break;
        }
    }
    return true;
}

bool UAccess::can_read(const void *addr, size_t len) {
    return range_ok((uintptr_t)addr, len, false);
}

bool UAccess::can_write(void *addr, size_t len) {
    return range_ok((uintptr_t)addr, len, true);
}

long UAccess::strnlen(const char *str, size_t max_len) {
    uintptr_t p = (uintptr_t)str;
    size_t n = 0;
    while (n < max_len) {
        /* 每进入新的一页先校验，再读这一页里的字节 */
        if (n == 0 || (p & (PAGE_SIZE - 1)) == 0) {
            if (!range_in_user(p, 1) || !page_ok(PAGE_ALIGN_DOWN(p), false)) {
                return -1;
            }
        }
        if (*(const char *)p == '\0') {
            return (long)n;
        }
        p++;
        n++;
    }
    return -1;
}

} // namespace kernel
