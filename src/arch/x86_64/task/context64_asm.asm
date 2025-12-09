; ============================================================================
; context64.asm - x86_64 Architecture-Specific Task Switching Code
; ============================================================================
;
; This file implements the low-level context switching routines for the
; x86_64 (64-bit) architecture. It provides the HAL context switch
; interface implementation.
;
; Requirements: 7.1, 7.3, 12.1
;
; Context Structure Offsets (must match x86_64_context_t in context64.h):
;   r15:      0
;   r14:      8
;   r13:     16
;   r12:     24
;   r11:     32
;   r10:     40
;   r9:      48
;   r8:      56
;   rbp:     64
;   rdi:     72
;   rsi:     80
;   rdx:     88
;   rcx:     96
;   rbx:    104
;   rax:    112
;   rip:    120
;   cs:     128
;   rflags: 136
;   rsp:    144
;   ss:     152
;   cr3:    160
; ============================================================================

[BITS 64]

section .text

; ============================================================================
; void hal_context_switch_asm(hal_context_t **old_ctx, hal_context_t *new_ctx)
;
; Performs a context switch from the current task to a new task.
; This is the HAL implementation for x86_64 architecture.
;
; Parameters (System V AMD64 ABI):
;   rdi = old_ctx - Pointer to pointer where current context is saved
;   rsi = new_ctx - Pointer to context to switch to
;
; This function:
;   1. Saves all registers of the current task to *old_ctx
;   2. Switches CR3 if necessary (address space switch)
;   3. Restores all registers from new_ctx
;   4. Returns to the new task's execution point
; ============================================================================

global hal_context_switch_asm
hal_context_switch_asm:
    ; ========================================================================
    ; Step 1: Save current task context
    ; ========================================================================
    test rdi, rdi
    jz .load_new_context
    
    mov rax, [rdi]          ; rax = *old_ctx (pointer to context structure)
    test rax, rax
    jz .load_new_context
    
    ; Save general purpose registers
    mov [rax + 0], r15
    mov [rax + 8], r14
    mov [rax + 16], r13
    mov [rax + 24], r12
    mov [rax + 32], r11
    mov [rax + 40], r10
    mov [rax + 48], r9
    mov [rax + 56], r8
    mov [rax + 64], rbp
    mov [rax + 72], rdi
    mov [rax + 80], rsi
    mov [rax + 88], rdx
    mov [rax + 96], rcx
    mov [rax + 104], rbx
    ; Save rax itself (we need to use a temp register)
    push rbx
    mov rbx, rax
    mov [rbx + 112], rax     ; Save original rax
    pop rbx
    
    ; Save return address as RIP
    mov rcx, [rsp]
    mov [rax + 120], rcx
    
    ; Save CS (kernel code segment)
    mov qword [rax + 128], 0x08
    
    ; Save RFLAGS
    pushfq
    pop rcx
    mov [rax + 136], rcx
    
    ; Save RSP (caller's stack position, skip return address)
    lea rcx, [rsp + 8]
    mov [rax + 144], rcx
    
    ; Save SS (kernel data segment)
    mov qword [rax + 152], 0x10
    
    ; Save CR3 (page table base)
    mov rcx, cr3
    mov [rax + 160], rcx
    
    ; rsi still contains new_ctx, continue to load
    
.load_new_context:
    ; ========================================================================
    ; Step 2: Load new task context
    ; ========================================================================
    ; rsi = new_ctx
    mov rax, rsi
    test rax, rax
    jz .done
    
    ; Switch CR3 (address space) - Property 10: Address Space Switch
    mov rcx, [rax + 160]
    mov rdx, cr3
    cmp rcx, rdx
    je .skip_cr3_switch
    mov cr3, rcx
.skip_cr3_switch:
    
    ; Check if returning to user mode or kernel mode
    mov cx, [rax + 128]      ; CS
    test cx, 3               ; Check RPL bits
    jz .restore_kernel
    
    ; ========================================================================
    ; User mode restore (using IRETQ)
    ; ========================================================================
.restore_user:
    ; Build IRETQ stack frame (in reverse order)
    push qword [rax + 152]   ; SS
    push qword [rax + 144]   ; RSP
    push qword [rax + 136]   ; RFLAGS
    push qword [rax + 128]   ; CS
    push qword [rax + 120]   ; RIP
    
    ; Restore general purpose registers
    mov r15, [rax + 0]
    mov r14, [rax + 8]
    mov r13, [rax + 16]
    mov r12, [rax + 24]
    mov r11, [rax + 32]
    mov r10, [rax + 40]
    mov r9, [rax + 48]
    mov r8, [rax + 56]
    mov rbp, [rax + 64]
    mov rdi, [rax + 72]
    mov rsi, [rax + 80]
    mov rdx, [rax + 88]
    mov rcx, [rax + 96]
    mov rbx, [rax + 104]
    mov rax, [rax + 112]     ; Restore rax last
    
    iretq

    
    ; ========================================================================
    ; Kernel mode restore (using JMP/RET)
    ; ========================================================================
.restore_kernel:
    ; Strategy: Switch to new stack, push RIP, restore registers, then RET
    
    ; Switch to new stack
    mov rsp, [rax + 144]
    
    ; Push RIP as return address on new stack
    push qword [rax + 120]
    
    ; Restore RFLAGS
    push qword [rax + 136]
    popfq
    
    ; Restore general purpose registers (restore rax last)
    mov r15, [rax + 0]
    mov r14, [rax + 8]
    mov r13, [rax + 16]
    mov r12, [rax + 24]
    mov r11, [rax + 32]
    mov r10, [rax + 40]
    mov r9, [rax + 48]
    mov r8, [rax + 56]
    mov rbp, [rax + 64]
    mov rdi, [rax + 72]
    mov rsi, [rax + 80]
    mov rdx, [rax + 88]
    mov rcx, [rax + 96]
    mov rbx, [rax + 104]
    mov rax, [rax + 112]     ; Restore rax last
    
    ; Stack top now has return address (RIP), use RET to jump
    ret

.done:
    ret

; ============================================================================
; void hal_context_enter_kernel_thread(void)
;
; Entry point for newly created kernel threads.
; The thread entry function address is expected on the stack.
;
; This function:
;   1. Enables interrupts
;   2. Pops the entry function address from stack
;   3. Calls the entry function
;   4. Calls task_exit(0) when the function returns
; ============================================================================

global hal_context_enter_kernel_thread
hal_context_enter_kernel_thread:
    sti                      ; Enable interrupts
    pop rax                  ; Get entry function address from stack
    call rax                 ; Call the entry function
    
    ; If the function returns, exit the task
    extern task_exit
    xor rdi, rdi             ; Exit code = 0 (first argument in rdi)
    call task_exit
    
    ; Should never reach here
    cli
    hlt

; ============================================================================
; Legacy compatibility symbols
; These are aliases for backward compatibility with existing code
; ============================================================================

global task_switch_context
task_switch_context:
    jmp hal_context_switch_asm

global task_enter_kernel_thread
task_enter_kernel_thread:
    jmp hal_context_enter_kernel_thread
