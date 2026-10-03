#include <kernel/interrupt.h>

static volatile uint32_t interrupt_depth = 0;

void interrupt_enter(void) {
    interrupt_depth = interrupt_depth + 1;
}

void interrupt_exit(void) {
    if (interrupt_depth == 0) {
        return;
    }
    interrupt_depth = interrupt_depth - 1;
}

bool in_interrupt(void) {
    return interrupt_depth != 0;
}

