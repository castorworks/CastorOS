; ============================================================================
; user_asm.asm - 用户模式切换
; ============================================================================
; 实现从内核态（Ring 0）到用户态（Ring 3）的切换

[BITS 32]

section .text

global enter_usermode

; void enter_usermode(uint32_t entry_point, uint32_t user_stack);
;
; 通过构造伪造的中断返回栈帧，使用 IRET 实现特权级切换
;
; 参数：
;   entry_point - 用户程序入口地址（EIP）
;   user_stack  - 用户栈顶地址（ESP）
enter_usermode:
    cli                         ; 禁用中断（在设置完栈之前）
    
    mov eax, [esp + 4]          ; eax = entry_point
    mov ebx, [esp + 8]          ; ebx = user_stack
    
    ; 构造 IRET 栈帧（从下到上压入）
    ; IRET 会按以下顺序弹出（Ring 3 返回）：
    ;   1. EIP
    ;   2. CS
    ;   3. EFLAGS
    ;   4. ESP
    ;   5. SS
    
    push 0x23                   ; SS (用户数据段)
    push ebx                    ; ESP (用户栈指针)
    pushfd                      ; EFLAGS
    
    ; 设置 EFLAGS 中的 IF 位（启用中断）
    pop ecx
    or ecx, 0x200               ; IF = 1
    push ecx
    
    push 0x1B                   ; CS (用户代码段：0x18 | 0x03 = 0x1B)
    push eax                    ; EIP (用户程序入口)
    
    ; 设置数据段寄存器为用户数据段
    ; 注意：必须在构造完栈帧之后，IRET之前设置
    ; 用户数据段选择子：0x20（索引 4）| 0x03（RPL=3）= 0x23
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; 使用 IRET 切换到用户模式
    ; IRET 检测到 CS.RPL = 3，会执行：
    ;   1. 弹出 EIP, CS, EFLAGS
    ;   2. 检查 CS.RPL = 3（特权级切换）
    ;   3. 弹出 ESP, SS
    ;   4. 切换到用户栈
    ;   5. 继续执行用户代码
    iret
