; ============================================================================
; task_asm.asm - 任务切换汇编代码
; ============================================================================
; 实现底层的上下文切换
; 保存当前任务的 CPU 状态，恢复下一个任务的 CPU 状态

[BITS 32]

section .text

global task_switch

; void task_switch(task_t *current, task_t *next);
; 
; 参数：
;   current - 当前任务 PCB 地址
;   next    - 下一个任务 PCB 地址
;
; cpu_context_t 结构布局（偏移）：
;   0:  eax
;   4:  ebx
;   8:  ecx
;   12: edx
;   16: esi
;   20: edi
;   24: ebp
;   28: esp
;   32: eip
;   36: eflags
;   40: cr3
;   44: cs (uint16_t)
;   46: ds (uint16_t)
;   48: es (uint16_t)
;   50: fs (uint16_t)
;   52: gs (uint16_t)
;   54: ss (uint16_t)
task_switch:
    ; 保存 current 和 next 指针
    push ebx                ; 临时保存 ebx
    mov ebx, [esp + 8]      ; ebx = current (跳过 ebx 和返回地址)
    
    ; 计算 &current->context
    ; context 在 task_t 中的偏移：pid(4) + name(32) + state(4) = 40
    add ebx, 40             ; ebx = &current->context
    
    ; ========================================================================
    ; 保存当前任务的上下文到 current->context
    ; ========================================================================
    
    ; 保存 eax（当前的返回值）
    mov [ebx + 0], eax
    
    ; 恢复并保存 ebx（从栈上）
    mov eax, [esp]          ; eax = 原始 ebx
    mov [ebx + 4], eax      ; 保存原始 ebx
    
    ; 保存其他通用寄存器
    mov [ebx + 8], ecx      ; 保存 ecx
    mov [ebx + 12], edx     ; 保存 edx
    mov [ebx + 16], esi     ; 保存 esi
    mov [ebx + 20], edi     ; 保存 edi
    mov [ebx + 24], ebp     ; 保存 ebp
    
    ; 保存栈指针（调整为 task_switch 调用前的值）
    lea eax, [esp + 8]      ; esp + 8 = 跳过 ebx 和返回地址
    mov [ebx + 28], eax     ; 保存调整后的 esp
    
    ; 保存返回地址（EIP）
    mov eax, [esp + 4]      ; 取出返回地址
    mov [ebx + 32], eax     ; 保存 eip
    
    ; 保存 EFLAGS
    pushfd
    pop eax
    mov [ebx + 36], eax     ; 保存 eflags
    
    ; 保存 CR3
    mov eax, cr3
    mov [ebx + 40], eax     ; 保存 cr3
    
    ; 保存段寄存器
    mov ax, cs
    mov [ebx + 44], ax      ; 保存 cs
    mov ax, ds
    mov [ebx + 46], ax      ; 保存 ds
    mov ax, es
    mov [ebx + 48], ax      ; 保存 es
    mov ax, fs
    mov [ebx + 50], ax      ; 保存 fs
    mov ax, gs
    mov [ebx + 52], ax      ; 保存 gs
    mov ax, ss
    mov [ebx + 54], ax      ; 保存 ss
    
    ; ========================================================================
    ; 恢复下一个任务的上下文从 next->context
    ; ========================================================================
    
    ; 获取 next 指针
    mov edi, [esp + 12]     ; edi = next (跳过 ebx 和返回地址)
    
    ; 计算 &next->context
    add edi, 40             ; edi = &next->context
    
    ; 注意：CR3 已经在调度器（C 代码）中切换，这里不再切换
    ; 避免重复切换导致的问题
    
    ; 检查目标任务的 CS 是否是用户代码段（Ring 3）
    mov ax, [edi + 44]      ; 读取 cs
    test ax, 0x03           ; 检查 RPL 位（低 2 位）
    jz .kernel_task         ; 如果是 Ring 0，跳到内核任务恢复
    
    ; ========================================================================
    ; 切换到用户任务（Ring 3）- 使用 IRET
    ; ========================================================================
.user_task:
    ; 构造 IRET 栈帧
    ; IRET 期望：[SS] [ESP] [EFLAGS] [CS] [EIP]（从高地址到低地址）
    
    ; 压入 SS（用户栈段）
    movzx eax, word [edi + 54]
    push eax
    
    ; 压入 ESP（用户栈指针）
    push dword [edi + 28]
    
    ; 压入 EFLAGS
    push dword [edi + 36]
    
    ; 压入 CS（用户代码段）
    movzx eax, word [edi + 44]
    push eax
    
    ; 压入 EIP（用户入口点）
    push dword [edi + 32]
    
    ; 设置用户数据段寄存器
    mov ax, [edi + 46]      ; ds
    mov ds, ax
    mov ax, [edi + 48]      ; es
    mov es, ax
    mov ax, [edi + 50]      ; fs
    mov fs, ax
    mov ax, [edi + 52]      ; gs
    mov gs, ax
    
    ; 恢复通用寄存器
    mov eax, [edi + 0]      ; eax
    mov ebx, [edi + 4]      ; ebx
    mov ecx, [edi + 8]      ; ecx
    mov edx, [edi + 12]     ; edx
    mov esi, [edi + 16]     ; esi
    mov ebp, [edi + 24]     ; ebp
    mov edi, [edi + 20]     ; edi（最后恢复）
    
    ; 使用 IRET 切换到用户模式
    iret
    
    ; ========================================================================
    ; 切换到内核任务（Ring 0）- 直接恢复
    ; ========================================================================
.kernel_task:
    ; 在恢复栈之前，先保存 EIP 和原始 edx
    mov edx, [edi + 32]     ; edx = next->context.eip（临时保存 EIP）
    mov eax, [edi + 12]     ; eax = next->context.edx（保存原始 edx，此时 edi 还是指针）
    
    ; 恢复段寄存器（先恢复数据段）
    mov cx, [edi + 46]      ; cx = ds（使用 cx 避免破坏 eax）
    mov ds, cx
    mov cx, [edi + 48]      ; cx = es
    mov es, cx
    mov cx, [edi + 50]      ; cx = fs
    mov fs, cx
    mov cx, [edi + 52]      ; cx = gs
    mov gs, cx
    
    ; 恢复栈指针（必须在恢复 SS 之前）
    mov esp, [edi + 28]
    
    ; 恢复栈段（必须在恢复 ESP 之后）
    mov cx, [edi + 54]      ; cx = ss
    mov ss, cx
    
    ; 恢复 EFLAGS
    push dword [edi + 36]
    popfd
    
    ; 恢复通用寄存器（除了 edx 和 edi，它们需要特殊处理）
    mov ebx, [edi + 4]      ; ebx
    mov ecx, [edi + 8]      ; ecx
    mov esi, [edi + 16]     ; esi
    mov ebp, [edi + 24]     ; ebp
    
    ; 将 EIP 写入栈顶（替换返回地址或占位符）
    ; 对于首次运行的任务，栈顶是占位符 0，会被替换为 EIP
    ; 对于被切换回来的任务，栈顶是调用 task_switch 的返回地址，会被替换为任务的 EIP
    mov [esp], edx          ; 将 EIP 写入栈顶
    
    ; 将原始 edx 压入栈（保存，稍后恢复）
    push eax                ; push 原始 edx（eax 中保存的值）
    
    ; 恢复 eax 和 edi
    mov eax, [edi + 0]      ; eax
    mov edi, [edi + 20]     ; edi（最后恢复，因为之前用它作为指针）
    
    ; 恢复 edx（从栈中弹出）
    pop edx                 ; 恢复原始 edx
    
    ; 跳转到下一个任务（ret 会从栈顶读取 EIP 并跳转）
    ret
