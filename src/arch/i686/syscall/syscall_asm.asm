; ============================================================================
; syscall_asm.asm - i686 System Call Assembly Entry Point
; ============================================================================
;
; This file implements the i686-specific system call entry mechanism using
; INT 0x80. It is part of the HAL (Hardware Abstraction Layer) for system calls.
;
; **Feature: multi-arch-support**
; **Validates: Requirements 8.1, 12.1**
;
; Stack frame layout after syscall_handler saves registers:
;   frame[0]  = DS
;   frame[1]  = EAX (syscall_num)
;   frame[2]  = EBX (arg1)
;   frame[3]  = ECX (arg2)
;   frame[4]  = EDX (arg3)
;   frame[5]  = ESI (arg4)
;   frame[6]  = EDI (arg5)
;   frame[7]  = EBP
;   frame[8]  = EIP (IRET)
;   frame[9]  = CS (IRET)
;   frame[10] = EFLAGS (IRET)
;   frame[11] = ESP (IRET, user mode only)
;   frame[12] = SS (IRET, user mode only)
; ============================================================================

[BITS 32]

section .text

global syscall_handler
extern syscall_dispatcher

; ============================================================================
; syscall_handler - INT 0x80 Entry Point
; ============================================================================
; This is the entry point for system calls on i686. User programs invoke
; system calls using INT 0x80 with:
;   - EAX = system call number
;   - EBX = arg1
;   - ECX = arg2
;   - EDX = arg3
;   - ESI = arg4
;   - EDI = arg5
;   - EBP = arg6 (for some syscalls like mmap)
;
; Return value is placed in EAX.
; ============================================================================

syscall_handler:
    ;
    ; Save general-purpose registers (matching C frame layout)
    ;
    push ebp
    push edi
    push esi
    push edx
    push ecx
    push ebx
    push eax          ; frame[1] = eax (syscall_num)

    ;
    ; Save original DS
    ;
    xor eax, eax
    mov ax, ds
    push eax          ; frame[0] = DS     <-- frame base

    ;
    ; Switch to kernel data segment
    ;
    mov ax, 0x10      ; GDT_KERNEL_DATA_SEGMENT
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ;
    ; Set EBP to point to frame[0]
    ;
    mov ebp, esp

    ;
    ; Push arguments for syscall_dispatcher(num, p1, p2, p3, p4, p5, frame)
    ;
    push ebp                ; frame pointer
    push dword [ebp + 24]   ; p5 = edi
    push dword [ebp + 20]   ; p4 = esi
    push dword [ebp + 16]   ; p3 = edx
    push dword [ebp + 12]   ; p2 = ecx
    push dword [ebp + 8]    ; p1 = ebx
    push dword [ebp + 4]    ; syscall_num = eax

    call syscall_dispatcher
    add esp, 28             ; 7 arguments * 4 bytes

    ;
    ; Store return value in frame[1] (will be restored to EAX)
    ;
    mov [ebp + 4], eax      ; frame[1] = return value

    ;
    ; Restore segment registers
    ;
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ;
    ; Restore general-purpose registers
    ; Note: EAX position now contains the return value
    ;
    pop eax
    pop ebx
    pop ecx
    pop edx
    pop esi
    pop edi
    pop ebp

    iret

