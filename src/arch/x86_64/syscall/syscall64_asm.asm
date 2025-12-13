; ============================================================================
; syscall64_asm.asm - x86_64 System Call Assembly Entry Point
; ============================================================================
;
; This file implements the x86_64-specific system call entry mechanism using
; the SYSCALL/SYSRET instructions. It is part of the HAL (Hardware Abstraction
; Layer) for system calls.
;
; **Feature: multi-arch-support**
; **Validates: Requirements 7.5, 8.1**
;
; On x86_64, system calls are invoked using the SYSCALL instruction:
;   - RAX = system call number
;   - RDI = arg1
;   - RSI = arg2
;   - RDX = arg3
;   - R10 = arg4 (RCX is clobbered by SYSCALL)
;   - R8  = arg5
;   - R9  = arg6
;
; SYSCALL instruction behavior:
;   - RCX <- RIP (return address)
;   - R11 <- RFLAGS
;   - RIP <- IA32_LSTAR MSR
;   - CS  <- IA32_STAR[47:32]
;   - SS  <- IA32_STAR[47:32] + 8
;   - RFLAGS <- RFLAGS AND NOT(IA32_FMASK)
;
; SYSRET instruction behavior:
;   - RIP <- RCX
;   - RFLAGS <- R11
;   - CS  <- IA32_STAR[63:48] + 16
;   - SS  <- IA32_STAR[63:48] + 8
;
; Stack frame layout after syscall_entry saves registers:
;   [rsp + 0x00] = r15
;   [rsp + 0x08] = r14
;   [rsp + 0x10] = r13
;   [rsp + 0x18] = r12
;   [rsp + 0x20] = r11 (saved RFLAGS)
;   [rsp + 0x28] = r10 (arg4)
;   [rsp + 0x30] = r9  (arg6)
;   [rsp + 0x38] = r8  (arg5)
;   [rsp + 0x40] = rbp
;   [rsp + 0x48] = rdi (arg1)
;   [rsp + 0x50] = rsi (arg2)
;   [rsp + 0x58] = rdx (arg3)
;   [rsp + 0x60] = rcx (user RIP)
;   [rsp + 0x68] = rbx
;   [rsp + 0x70] = rax (syscall number)
;   [rsp + 0x78] = user_rsp (saved from swapgs)
; ============================================================================

[BITS 64]

section .data

; Per-CPU kernel stack pointer (for swapgs)
; In a real SMP system, this would be per-CPU data
global kernel_stack_ptr
kernel_stack_ptr:
    dq 0

; Saved user stack pointer during syscall
global user_stack_ptr
user_stack_ptr:
    dq 0

section .text

; ============================================================================
; syscall_entry - SYSCALL Entry Point
; ============================================================================
; This is the entry point for system calls on x86_64. User programs invoke
; system calls using SYSCALL with:
;   - RAX = system call number
;   - RDI = arg1
;   - RSI = arg2
;   - RDX = arg3
;   - R10 = arg4 (RCX is used by SYSCALL for return address)
;   - R8  = arg5
;   - R9  = arg6
;
; Return value is placed in RAX.
; ============================================================================

global syscall_entry
extern syscall_dispatcher

syscall_entry:
    ; ========================================================================
    ; Step 1: Switch to kernel stack
    ; ========================================================================
    ; At this point:
    ;   - RCX = user RIP (return address)
    ;   - R11 = user RFLAGS
    ;   - RSP = user stack (we need to switch to kernel stack)
    ;   - Interrupts are disabled (RFLAGS.IF cleared by SYSCALL)
    
    ; Save user RSP and load kernel RSP using absolute addresses
    ; (We don't use swapgs here because GS base MSRs aren't set up)
    mov [rel user_stack_ptr], rsp
    mov rsp, [rel kernel_stack_ptr]
    
    ; Check if kernel stack is valid (non-zero)
    test rsp, rsp
    jnz .stack_ok
    ; If kernel stack is 0, we have a problem - use a fallback
    ; This should never happen if hal_syscall_set_kernel_stack was called
    hlt
.stack_ok:
    
    ; ========================================================================
    ; Step 2: Save user context
    ; ========================================================================
    ; Build a stack frame for the syscall
    
    ; Save user RSP
    push qword [rel user_stack_ptr]
    
    ; Save general purpose registers
    push rax                ; syscall number
    push rbx
    push rcx                ; user RIP
    push rdx                ; arg3
    push rsi                ; arg2
    push rdi                ; arg1
    push rbp
    push r8                 ; arg5
    push r9                 ; arg6
    push r10                ; arg4
    push r11                ; user RFLAGS
    push r12
    push r13
    push r14
    push r15
    
    ; ========================================================================
    ; Step 3: Call syscall dispatcher
    ; ========================================================================
    ; syscall_dispatcher(syscall_num, p1, p2, p3, p4, p5, frame)
    ; System V AMD64 ABI: rdi, rsi, rdx, rcx, r8, r9, [stack]
    
    ; CRITICAL: Enable interrupts before calling syscall dispatcher
    ; SYSCALL instruction clears IF via SFMASK, but we need interrupts
    ; enabled during syscall handling (e.g., for keyboard input while
    ; blocking in read()). This matches i686 behavior which uses a trap
    ; gate that doesn't disable interrupts.
    sti
    
    mov rdi, [rsp + 0x70]   ; syscall_num = saved rax
    mov rsi, [rsp + 0x48]   ; p1 = saved rdi
    mov rdx, [rsp + 0x50]   ; p2 = saved rsi
    mov rcx, [rsp + 0x58]   ; p3 = saved rdx
    mov r8,  [rsp + 0x28]   ; p4 = saved r10
    mov r9,  [rsp + 0x38]   ; p5 = saved r8
    
    ; Push frame pointer as 7th argument
    mov rax, rsp
    push rax                ; frame pointer
    
    ; Align stack to 16 bytes (we pushed 17 qwords = 136 bytes, need 8 more)
    sub rsp, 8
    
    call syscall_dispatcher
    
    ; Clean up stack
    add rsp, 16             ; Remove frame pointer and alignment
    
    ; ========================================================================
    ; Step 4: Restore user context and return
    ; ========================================================================
    ; Store return value
    mov [rsp + 0x70], rax   ; Save return value to rax position in frame
    
    ; Restore general purpose registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11                 ; user RFLAGS
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx                 ; user RIP
    pop rbx
    pop rax                 ; return value
    
    ; Restore user RSP
    pop rsp
    
    ; Return to user mode
    ; RCX = return address, R11 = RFLAGS
    ; Note: SYSRET will restore RFLAGS from R11, which includes IF.
    ; We disable interrupts here for the brief window before SYSRET
    ; to prevent any race conditions during the return sequence.
    cli
    o64 sysret


; ============================================================================
; syscall_entry_compat - INT 0x80 Compatibility Entry Point
; ============================================================================
; This provides compatibility with the i686 INT 0x80 system call interface.
; Some legacy code or 32-bit compatibility mode may use this.
;
; Register convention (same as i686):
;   - RAX = system call number
;   - RBX = arg1
;   - RCX = arg2
;   - RDX = arg3
;   - RSI = arg4
;   - RDI = arg5
;   - RBP = arg6
; ============================================================================

global syscall_entry_compat
syscall_entry_compat:
    ; Save all registers
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
    
    ; Save segment registers
    xor rax, rax
    mov ax, ds
    push rax
    
    ; Switch to kernel data segment
    mov ax, 0x10            ; GDT64_KERNEL_DATA_SEGMENT
    mov ds, ax
    mov es, ax
    
    ; Set up frame pointer
    mov rbp, rsp
    
    ; Call syscall_dispatcher(num, p1, p2, p3, p4, p5, frame)
    ; Convert from i686 convention to x86_64 convention
    push rbp                ; frame pointer
    mov r9, [rbp + 24]      ; p5 = rdi
    mov r8, [rbp + 20]      ; p4 = rsi
    mov rcx, [rbp + 16]     ; p3 = rdx
    mov rdx, [rbp + 12]     ; p2 = rcx
    mov rsi, [rbp + 8]      ; p1 = rbx
    mov rdi, [rbp + 4]      ; syscall_num = rax
    
    call syscall_dispatcher
    
    add rsp, 8              ; Remove frame pointer
    
    ; Store return value
    mov [rbp + 4], rax
    
    ; Restore segment registers
    pop rax
    mov ds, ax
    mov es, ax
    
    ; Restore general purpose registers
    pop rax                 ; return value
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    
    iretq


; ============================================================================
; MSR Constants for SYSCALL/SYSRET configuration
; ============================================================================

%define MSR_EFER        0xC0000080  ; Extended Feature Enable Register
%define MSR_STAR        0xC0000081  ; Segment selectors for SYSCALL/SYSRET
%define MSR_LSTAR       0xC0000082  ; Long mode SYSCALL target RIP
%define MSR_CSTAR       0xC0000083  ; Compatibility mode SYSCALL target RIP
%define MSR_SFMASK      0xC0000084  ; SYSCALL RFLAGS mask

%define EFER_SCE        (1 << 0)    ; System Call Extensions enable

; ============================================================================
; syscall_init_msr - Initialize MSRs for SYSCALL/SYSRET
; ============================================================================
; This function configures the MSRs required for SYSCALL/SYSRET operation.
;
; MSR_STAR layout:
;   Bits 63:48 = SYSRET CS and SS selector base (user CS = base+16, SS = base+8)
;   Bits 47:32 = SYSCALL CS and SS selector base (kernel CS = base, SS = base+8)
;   Bits 31:0  = Reserved
;
; For our GDT layout:
;   Kernel CS = 0x08, Kernel SS = 0x10
;   User CS = 0x1B (0x18 | 3), User SS = 0x23 (0x20 | 3)
;
; STAR[47:32] = 0x08 (SYSCALL loads CS=0x08, SS=0x10)
; STAR[63:48] = 0x10 (SYSRET loads CS=0x10+16=0x20|3=0x23... wait, that's wrong)
;
; Actually for SYSRET:
;   CS = STAR[63:48] + 16 (for 64-bit mode)
;   SS = STAR[63:48] + 8
;
; So we need STAR[63:48] = 0x08 to get:
;   User CS = 0x08 + 16 = 0x18, with RPL=3 -> 0x1B
;   User SS = 0x08 + 8 = 0x10, with RPL=3 -> 0x13... that's wrong
;
; The correct setup requires GDT to be laid out as:
;   0x00: Null
;   0x08: Kernel Code (64-bit)
;   0x10: Kernel Data
;   0x18: User Code (64-bit)  <- SYSRET will use this
;   0x20: User Data           <- SYSRET will use this
;
; STAR[47:32] = 0x08 (kernel CS)
; STAR[63:48] = 0x10 (user base, so CS=0x10+16=0x20... still wrong)
;
; Let me recalculate:
; SYSRET in 64-bit mode:
;   CS = (STAR[63:48] + 16) | 3
;   SS = (STAR[63:48] + 8) | 3
;
; We want CS = 0x1B (0x18 | 3) and SS = 0x23 (0x20 | 3)
; So: STAR[63:48] + 16 = 0x18 -> STAR[63:48] = 0x08
;     STAR[63:48] + 8 = 0x20... but 0x08 + 8 = 0x10, not 0x20
;
; This is a known quirk. The GDT must be laid out specifically:
;   0x08: Kernel Code
;   0x10: Kernel Data  
;   0x18: User Data (32-bit compat, or padding)
;   0x20: User Code (64-bit)
;   0x28: User Data (64-bit)
;
; But our current GDT has:
;   0x08: Kernel Code
;   0x10: Kernel Data
;   0x18: User Code
;   0x20: User Data
;
; For SYSRET with STAR[63:48] = 0x08:
;   CS = 0x08 + 16 = 0x18 | 3 = 0x1B ✓
;   SS = 0x08 + 8 = 0x10 | 3 = 0x13 ✗ (should be 0x23)
;
; The issue is that SYSRET assumes a specific GDT layout.
; We need to reorder our GDT or use IRETQ for user mode return.
;
; For now, let's use the standard Linux-like layout:
;   STAR[47:32] = 0x08 (kernel code base)
;   STAR[63:48] = 0x10 (user code base - 16)
;
; This gives:
;   SYSCALL: CS = 0x08, SS = 0x10
;   SYSRET:  CS = 0x10 + 16 | 3 = 0x23, SS = 0x10 + 8 | 3 = 0x1B
;
; Hmm, that swaps CS and SS. The GDT needs to be:
;   0x08: Kernel Code
;   0x10: Kernel Data
;   0x18: User Data (for SYSRET SS)
;   0x20: User Code (for SYSRET CS)
;
; With STAR[63:48] = 0x08:
;   SYSRET CS = 0x08 + 16 | 3 = 0x1B (index 3 = User Code at 0x18)
;   SYSRET SS = 0x08 + 8 | 3 = 0x13 (index 2 = Kernel Data at 0x10)
;
; This still doesn't work. Let me check the Intel manual again...
;
; From Intel SDM:
; SYSRET (64-bit mode):
;   CS.Selector = IA32_STAR[63:48] + 16
;   CS.RPL = 3
;   SS.Selector = IA32_STAR[63:48] + 8
;   SS.RPL = 3
;
; So if STAR[63:48] = 0x10:
;   CS = 0x10 + 16 = 0x20, with RPL=3 -> 0x23
;   SS = 0x10 + 8 = 0x18, with RPL=3 -> 0x1B
;
; This means GDT should be:
;   0x18: User Data (SS for SYSRET)
;   0x20: User Code (CS for SYSRET)
;
; But our GDT has User Code at 0x18 and User Data at 0x20.
; We need to swap them or adjust STAR.
;
; Alternative: Use IRETQ instead of SYSRET for returning to user mode.
; This is simpler and doesn't require GDT reordering.
; ============================================================================

global syscall_init_msr
syscall_init_msr:
    ; Enable SYSCALL/SYSRET in EFER
    mov ecx, MSR_EFER
    rdmsr
    or eax, EFER_SCE
    wrmsr
    
    ; Set up STAR MSR
    ; Bits 47:32 = Kernel CS (0x08)
    ; Bits 63:48 = User CS base for SYSRET (0x10, so CS=0x20|3, SS=0x18|3)
    ; Note: This requires GDT to have User Data at 0x18 and User Code at 0x20
    ; Our current GDT has them swapped, so we'll use IRETQ for now
    mov ecx, MSR_STAR
    xor eax, eax            ; Low 32 bits = 0
    mov edx, 0x00100008     ; High 32 bits: [63:48]=0x10, [47:32]=0x08
    wrmsr
    
    ; Set LSTAR to syscall_entry
    mov ecx, MSR_LSTAR
    lea rax, [rel syscall_entry]
    mov rdx, rax
    shr rdx, 32
    wrmsr
    
    ; Set CSTAR for compatibility mode (optional, use same entry)
    mov ecx, MSR_CSTAR
    lea rax, [rel syscall_entry_compat]
    mov rdx, rax
    shr rdx, 32
    wrmsr
    
    ; Set SFMASK to clear IF, TF, DF on SYSCALL
    ; This ensures interrupts are disabled during syscall entry
    mov ecx, MSR_SFMASK
    mov eax, 0x00000700     ; Clear IF (bit 9), TF (bit 8), DF (bit 10)
    xor edx, edx
    wrmsr
    
    ret


; ============================================================================
; set_kernel_stack - Set the kernel stack for syscall entry
; ============================================================================
; void set_kernel_stack(uint64_t stack_ptr)
;
; Parameters:
;   rdi = kernel stack pointer
; ============================================================================

global set_kernel_stack
set_kernel_stack:
    mov [rel kernel_stack_ptr], rdi
    ret


; ============================================================================
; enter_usermode64 - Enter user mode (x86_64)
; ============================================================================
; void enter_usermode64(uint64_t entry_point, uint64_t user_stack)
;
; This function transitions from kernel mode to user mode using IRETQ.
; It sets up the stack frame required by IRETQ and jumps to user code.
;
; Parameters:
;   rdi = entry_point (user code address)
;   rsi = user_stack (user stack pointer)
;
; IRETQ stack frame (from top to bottom):
;   SS
;   RSP
;   RFLAGS
;   CS
;   RIP
; ============================================================================

global enter_usermode64
enter_usermode64:
    cli                     ; Disable interrupts
    
    ; Clear all general purpose registers for security
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rbp, rbp
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15
    
    ; Build IRETQ stack frame
    ; GDT Layout (SYSRET compatible):
    ;   0x18: User Data (SS = 0x18 | 3 = 0x1B)
    ;   0x20: User Code (CS = 0x20 | 3 = 0x23)
    push 0x1B               ; SS = User Data Segment (0x18 | RPL=3)
    push rsi                ; RSP = user stack
    
    ; Set RFLAGS: IF=1 (enable interrupts), reserved bit 1 = 1
    push 0x202              ; RFLAGS
    
    push 0x23               ; CS = User Code Segment (0x20 | RPL=3)
    push rdi                ; RIP = entry point
    
    ; Clear segment registers (set to user data segment)
    mov ax, 0x1B            ; User Data Segment (0x18 | RPL=3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Clear rdi and rsi (they contained parameters)
    xor rdi, rdi
    xor rsi, rsi
    
    ; Return to user mode
    iretq
