; ============================================================================
; isr64_asm.asm - Interrupt Service Routines Assembly Stubs (x86_64)
; ============================================================================
; Creates assembly entry points for all CPU exceptions (vectors 0-31).
; Saves 64-bit register state, calls C handler, then restores state.
;
; Key differences from 32-bit:
;   - No PUSHA/POPA - must save registers individually
;   - 64-bit registers (RAX-R15)
;   - IRETQ instead of IRET
;   - Red zone consideration (128 bytes below RSP)
;   - Different calling convention (System V AMD64 ABI)
;
; Requirements: 6.1 - Save/restore 64-bit register state

[BITS 64]

section .text

; Export ISR symbols
global isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
global isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
global isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
global isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31

; Import C handler functions
extern isr64_handler
extern interrupt_enter
extern interrupt_exit

; ============================================================================
; ISR Macros
; ============================================================================

; ISR without error code - push dummy error code (0) for stack alignment
%macro ISR_NOERRCODE 1
isr%1:
    push qword 0            ; Push dummy error code
    push qword %1           ; Push interrupt number
    jmp isr_common_stub
%endmacro

; ISR with error code - CPU already pushed error code
%macro ISR_ERRCODE 1
isr%1:
    push qword %1           ; Push interrupt number
    jmp isr_common_stub
%endmacro

; ============================================================================
; Exception Handlers (vectors 0-31)
; ============================================================================

ISR_NOERRCODE 0     ; #DE - Divide Error
ISR_NOERRCODE 1     ; #DB - Debug Exception
ISR_NOERRCODE 2     ; NMI - Non-Maskable Interrupt
ISR_NOERRCODE 3     ; #BP - Breakpoint
ISR_NOERRCODE 4     ; #OF - Overflow
ISR_NOERRCODE 5     ; #BR - Bound Range Exceeded
ISR_NOERRCODE 6     ; #UD - Invalid Opcode
ISR_NOERRCODE 7     ; #NM - Device Not Available
ISR_ERRCODE   8     ; #DF - Double Fault (has error code)
ISR_NOERRCODE 9     ; Coprocessor Segment Overrun (legacy)
ISR_ERRCODE   10    ; #TS - Invalid TSS (has error code)
ISR_ERRCODE   11    ; #NP - Segment Not Present (has error code)
ISR_ERRCODE   12    ; #SS - Stack-Segment Fault (has error code)
ISR_ERRCODE   13    ; #GP - General Protection Fault (has error code)
ISR_ERRCODE   14    ; #PF - Page Fault (has error code)
ISR_NOERRCODE 15    ; Reserved
ISR_NOERRCODE 16    ; #MF - x87 FPU Error
ISR_ERRCODE   17    ; #AC - Alignment Check (has error code)
ISR_NOERRCODE 18    ; #MC - Machine Check
ISR_NOERRCODE 19    ; #XM/#XF - SIMD Floating-Point
ISR_NOERRCODE 20    ; #VE - Virtualization Exception
ISR_ERRCODE   21    ; #CP - Control Protection Exception (has error code)
ISR_NOERRCODE 22    ; Reserved
ISR_NOERRCODE 23    ; Reserved
ISR_NOERRCODE 24    ; Reserved
ISR_NOERRCODE 25    ; Reserved
ISR_NOERRCODE 26    ; Reserved
ISR_NOERRCODE 27    ; Reserved
ISR_NOERRCODE 28    ; Reserved
ISR_NOERRCODE 29    ; Reserved
ISR_ERRCODE   30    ; #SX - Security Exception (has error code)
ISR_NOERRCODE 31    ; Reserved

; ============================================================================
; Common ISR Stub
; ============================================================================
; Stack layout at entry (after pushing int_no and err_code):
;   [RSP+0]   int_no
;   [RSP+8]   err_code
;   [RSP+16]  RIP (pushed by CPU)
;   [RSP+24]  CS
;   [RSP+32]  RFLAGS
;   [RSP+40]  RSP (user, only if privilege change)
;   [RSP+48]  SS (user, only if privilege change)
;
; After saving registers, we pass RSP to the C handler as the registers_t pointer.

isr_common_stub:
    ; Save all general-purpose registers
    ; Order must match registers64_t structure (in reverse, since we push)
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
    ; System V AMD64 ABI: first argument in RDI
    mov rdi, rsp
    call isr64_handler

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
    ; IRETQ pops: RIP, CS, RFLAGS, RSP, SS
    iretq
