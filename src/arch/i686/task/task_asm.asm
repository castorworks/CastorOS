; ============================================================================
; task_asm.asm - i686 Architecture-Specific Task Switching Code
; ============================================================================
;
; This file implements the low-level context switching routines for the
; i686 (x86 32-bit) architecture. It provides the HAL context switch
; interface implementation.
;
; Requirements: 7.1, 12.1
;
; Context Structure Offsets (must match i686_context_t in context.h):
;   gs:       0
;   fs:       4
;   es:       8
;   ds:      12
;   edi:     16
;   esi:     20
;   ebp:     24
;   esp_dummy: 28
;   ebx:     32
;   edx:     36
;   ecx:     40
;   eax:     44
;   eip:     48
;   cs:      52
;   eflags:  56
;   esp:     60
;   ss:      64
;   cr3:     68
; ============================================================================

[BITS 32]

section .text

; ============================================================================
; void hal_context_switch_asm(hal_context_t **old_ctx, hal_context_t *new_ctx)
;
; Performs a context switch from the current task to a new task.
; This is the HAL implementation for i686 architecture.
;
; Parameters:
;   [esp + 4] = old_ctx - Pointer to pointer where current context is saved
;   [esp + 8] = new_ctx - Pointer to context to switch to
;
; This function:
;   1. Saves all registers of the current task to *old_ctx
;   2. Switches CR3 if necessary (address space switch)
;   3. Restores all registers from new_ctx
;   4. Returns to the new task's execution point
; ============================================================================

global hal_context_switch_asm
hal_context_switch_asm:
    ; Read parameters first
    mov eax, [esp + 4]      ; eax = old_ctx (pointer to pointer)
    mov edx, [esp + 8]      ; edx = new_ctx (pointer to context)
    
    ; ========================================================================
    ; Step 1: Save current task context
    ; ========================================================================
    test eax, eax
    jz .load_new_context
    
    mov eax, [eax]          ; eax = *old_ctx (pointer to context structure)
    test eax, eax
    jz .load_new_context
    
    ; Save segment registers
    mov [eax + 0], gs
    mov [eax + 4], fs
    mov [eax + 8], es
    mov [eax + 12], ds
    
    ; Save general purpose registers
    mov [eax + 16], edi
    mov [eax + 20], esi
    mov [eax + 24], ebp
    ; esp_dummy at offset 28 is skipped
    mov [eax + 32], ebx
    mov [eax + 36], edx
    mov [eax + 40], ecx
    mov [eax + 44], eax     ; Save eax itself
    
    ; Save return address as EIP
    mov ecx, [esp]
    mov [eax + 48], ecx
    
    ; Save CS
    mov [eax + 52], cs
    
    ; Save EFLAGS
    pushfd
    pop ecx
    mov [eax + 56], ecx
    
    ; Save ESP (caller's stack position, skip return address)
    lea ecx, [esp + 4]
    mov [eax + 60], ecx
    
    ; Save SS
    mov [eax + 64], ss
    
    ; Save CR3 (page directory)
    mov ecx, cr3
    mov [eax + 68], ecx
    
    ; edx still contains new_ctx, continue to load
    
.load_new_context:
    ; ========================================================================
    ; Step 2: Load new task context
    ; ========================================================================
    ; edx = new_ctx
    mov eax, edx
    test eax, eax
    jz .done
    
    ; Switch CR3 (address space)
    mov ecx, [eax + 68]
    mov cr3, ecx
    
    ; Check if returning to user mode or kernel mode
    mov cx, [eax + 52]      ; CS
    test cx, 3              ; Check RPL bits
    jz .restore_kernel
    
    ; ========================================================================
    ; User mode restore (using IRET)
    ; ========================================================================
.restore_user:
    ; Build IRET stack frame
    push dword [eax + 64]   ; SS
    push dword [eax + 60]   ; ESP
    push dword [eax + 56]   ; EFLAGS
    push dword [eax + 52]   ; CS
    push dword [eax + 48]   ; EIP
    
    ; Save segment registers to stack (need temporary storage)
    mov cx, [eax + 12]
    push ecx                ; DS
    mov cx, [eax + 8]
    push ecx                ; ES
    mov cx, [eax + 4]
    push ecx                ; FS
    mov cx, [eax + 0]
    push ecx                ; GS
    
    ; Restore general purpose registers
    mov edi, [eax + 16]
    mov esi, [eax + 20]
    mov ebp, [eax + 24]
    mov ebx, [eax + 32]
    mov edx, [eax + 36]
    mov ecx, [eax + 40]
    push dword [eax + 44]   ; Temporarily save eax
    
    ; Restore segment registers from stack
    pop eax                 ; Restore eax
    pop ebx
    mov gs, bx
    pop ebx
    mov fs, bx
    pop ebx
    mov es, bx
    pop ebx
    mov ds, bx
    
    iret
    
    ; ========================================================================
    ; Kernel mode restore (using JMP/RET)
    ; ========================================================================
.restore_kernel:
    ; Strategy: Switch to new stack, push EIP, restore registers, then RET
    
    ; Switch to new stack
    mov esp, [eax + 60]
    
    ; Push EIP as return address on new stack
    push dword [eax + 48]
    
    ; Restore EFLAGS
    push dword [eax + 56]
    popfd
    
    ; Restore segment registers
    mov cx, [eax + 0]
    mov gs, cx
    mov cx, [eax + 4]
    mov fs, cx
    mov cx, [eax + 8]
    mov es, cx
    mov cx, [eax + 12]
    mov ds, cx
    
    ; Restore general purpose registers (restore eax last)
    mov edi, [eax + 16]
    mov esi, [eax + 20]
    mov ebp, [eax + 24]
    mov ebx, [eax + 32]
    mov edx, [eax + 36]
    mov ecx, [eax + 40]
    mov eax, [eax + 44]     ; Restore eax last
    
    ; Stack top now has return address (EIP), use RET to jump
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
    sti                     ; Enable interrupts
    pop eax                 ; Get entry function address from stack
    call eax                ; Call the entry function
    
    ; If the function returns, exit the task
    extern task_exit
    push 0                  ; Exit code = 0
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
