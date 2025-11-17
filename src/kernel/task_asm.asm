; ============================================================================
; task_asm.asm - 任务切换汇编代码（简化修复版）
; ============================================================================

[BITS 32]

section .text

; ============================================================================
; void task_switch_context(cpu_context_t **old_ctx, cpu_context_t *new_ctx)
; ============================================================================

global task_switch_context
task_switch_context:
    ; 首先读取参数
    mov eax, [esp + 4]      ; eax = old_ctx  
    mov edx, [esp + 8]      ; edx = new_ctx
    
    ; ========================================================================
    ; 第一步：保存旧任务上下文
    ; ========================================================================
    test eax, eax
    jz .load_new_context
    
    mov eax, [eax]          ; eax = *old_ctx (指向结构体)
    test eax, eax
    jz .load_new_context
    
    ; 保存所有寄存器（使用 push/pop 来暂存我们需要的寄存器）
    ; 保存段寄存器
    mov [eax + 0], gs
    mov [eax + 4], fs
    mov [eax + 8], es
    mov [eax + 12], ds
    
    ; 保存通用寄存器
    mov [eax + 16], edi
    mov [eax + 20], esi
    mov [eax + 24], ebp
    ; esp_dummy 跳过
    mov [eax + 32], ebx
    mov [eax + 36], edx
    mov [eax + 40], ecx
    mov [eax + 44], eax     ; 保存 eax 本身
    
    ; 保存返回地址作为 EIP
    mov ecx, [esp]
    mov [eax + 48], ecx
    
    ; 保存 CS
    mov [eax + 52], cs
    
    ; 保存 EFLAGS
    pushfd
    pop ecx
    mov [eax + 56], ecx
    
    ; 保存 ESP（调用者的栈位置）
    lea ecx, [esp + 4]      ; 跳过返回地址
    mov [eax + 60], ecx
    
    ; 保存 SS
    mov [eax + 64], ss
    
    ; 保存 CR3
    mov ecx, cr3
    mov [eax + 68], ecx
    
    ; 现在 edx 已经包含 new_ctx，继续加载
    
.load_new_context:
    ; ========================================================================
    ; 第二步：加载新任务上下文  
    ; ========================================================================
    ; edx = new_ctx
    mov eax, edx
    test eax, eax
    jz .done
    
    ; 切换 CR3
    mov ecx, [eax + 68]
    mov cr3, ecx
    
    ; 检查是用户态还是内核态
    mov cx, [eax + 52]      ; CS
    test cx, 3
    jz .restore_kernel
    
    ; 用户态恢复（使用 iret）
.restore_user:
    ; 构造 iret 栈帧
    push dword [eax + 64]   ; SS
    push dword [eax + 60]   ; ESP
    push dword [eax + 56]   ; EFLAGS
    push dword [eax + 52]   ; CS
    push dword [eax + 48]   ; EIP
    
    ; 恢复段寄存器（需要临时保存）
    mov cx, [eax + 12]
    push ecx                ; DS
    mov cx, [eax + 8]
    push ecx                ; ES
    mov cx, [eax + 4]
    push ecx                ; FS
    mov cx, [eax + 0]
    push ecx                ; GS
    
    ; 恢复通用寄存器
    mov edi, [eax + 16]
    mov esi, [eax + 20]
    mov ebp, [eax + 24]
    mov ebx, [eax + 32]
    mov edx, [eax + 36]
    mov ecx, [eax + 40]
    push dword [eax + 44]   ; 暂存 eax
    
    ; 恢复段寄存器
    pop eax                 ; 恢复 eax
    pop ebx
    mov gs, bx
    pop ebx
    mov fs, bx
    pop ebx
    mov es, bx
    pop ebx
    mov ds, bx
    
    iret
    
    ; 内核态恢复（使用 jmp 而不是 ret）
.restore_kernel:
    ; 策略：在切换栈之前，把 EIP 保存到新栈上
    ; 然后恢复所有寄存器，最后跳转
    
    ; 首先获取新栈位置和 EIP
    mov esp, [eax + 60]     ; 切换到新栈
    
    ; 在新栈上构造返回点
    ; 注意：此时 eax 仍然指向 context 结构体
    push dword [eax + 48]   ; 压入 EIP 作为返回地址
    
    ; 恢复 EFLAGS
    push dword [eax + 56]
    popfd
    
    ; 恢复段寄存器
    mov cx, [eax + 0]
    mov gs, cx
    mov cx, [eax + 4]
    mov fs, cx
    mov cx, [eax + 8]
    mov es, cx
    mov cx, [eax + 12]
    mov ds, cx
    
    ; 恢复通用寄存器（最后恢复 eax）
    mov edi, [eax + 16]
    mov esi, [eax + 20]
    mov ebp, [eax + 24]
    mov ebx, [eax + 32]
    mov edx, [eax + 36]
    mov ecx, [eax + 40]
    mov eax, [eax + 44]     ; 最后恢复 eax
    
    ; 现在栈顶是返回地址（EIP），使用 ret 跳转
    ret

.done:
    ret

; ============================================================================
; void task_enter_kernel_thread(void)
; ============================================================================
global task_enter_kernel_thread
task_enter_kernel_thread:
    sti
    pop eax
    call eax
    
    extern task_exit
    push 0
    call task_exit
    
    cli
    hlt
