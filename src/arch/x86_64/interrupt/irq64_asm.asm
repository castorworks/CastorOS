; ============================================================================
; irq64_asm.asm - Hardware Interrupt Request Assembly Stubs (x86_64)
; ============================================================================
; Creates assembly entry points for hardware IRQs (vectors 32-47).
; Uses the same register save/restore pattern as ISR stubs.
;
; Requirements: 6.1 - Save/restore 64-bit register state for IRQs

[BITS 64]

section .text

; Export IRQ symbols
global irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7
global irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15

; Import C handler functions
extern irq64_handler
extern interrupt_enter
extern interrupt_exit

; ============================================================================
; IRQ Macro
; ============================================================================
; IRQs don't push error codes, so we push 0 for consistency

%macro IRQ 2
irq%1:
    push qword 0            ; Push dummy error code
    push qword %2           ; Push interrupt number (32 + IRQ#)
    jmp irq_common_stub
%endmacro

; ============================================================================
; IRQ Handlers (vectors 32-47)
; ============================================================================

IRQ  0, 32    ; IRQ 0: Timer
IRQ  1, 33    ; IRQ 1: Keyboard
IRQ  2, 34    ; IRQ 2: Cascade
IRQ  3, 35    ; IRQ 3: COM2/COM4
IRQ  4, 36    ; IRQ 4: COM1/COM3
IRQ  5, 37    ; IRQ 5: LPT2
IRQ  6, 38    ; IRQ 6: Floppy
IRQ  7, 39    ; IRQ 7: LPT1 / Spurious
IRQ  8, 40    ; IRQ 8: RTC
IRQ  9, 41    ; IRQ 9: Free
IRQ 10, 42    ; IRQ 10: Free
IRQ 11, 43    ; IRQ 11: Free
IRQ 12, 44    ; IRQ 12: PS/2 Mouse
IRQ 13, 45    ; IRQ 13: FPU
IRQ 14, 46    ; IRQ 14: Primary ATA
IRQ 15, 47    ; IRQ 15: Secondary ATA

; ============================================================================
; Common IRQ Stub
; ============================================================================
; Same structure as ISR stub - saves all registers, calls C handler

irq_common_stub:
    ; Save all general-purpose registers
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Mark entering interrupt context
    call interrupt_enter

    ; Call C handler with pointer to registers structure
    mov rdi, rsp
    call irq64_handler

    ; Mark exiting interrupt context
    call interrupt_exit

    ; Restore all general-purpose registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; Clean up interrupt number and error code from stack
    add rsp, 16

    ; Return from interrupt
    iretq
