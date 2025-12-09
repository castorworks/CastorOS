; ============================================================================
; gdt64_asm.asm - GDT and TSS Assembly Support Functions (x86_64)
; ============================================================================
; This file contains assembly routines for loading the GDT and TSS in 64-bit mode.
;
; In 64-bit mode:
;   - LGDT uses a 10-byte pointer (2-byte limit + 8-byte base)
;   - Segment registers are loaded differently (only CS needs far jump)
;   - LTR loads the Task Register with TSS selector
;
; Requirements: 3.3 - Configure 64-bit GDT with appropriate code and data segments

[BITS 64]

section .text

global gdt64_flush
global tss64_load

; ============================================================================
; void gdt64_flush(uint64_t gdt_ptr);
; ============================================================================
; Load the GDT and reload all segment registers.
;
; Parameters:
;   RDI = pointer to GDT pointer structure (limit:uint16 + base:uint64)
;
; In 64-bit mode:
;   - CS must be reloaded via far return (retfq)
;   - DS, ES, FS, GS, SS are loaded normally
;   - Base/limit in data segments are ignored, only selector matters
; ============================================================================
gdt64_flush:
    ; Load the GDT
    lgdt [rdi]
    
    ; Reload data segment registers with kernel data selector (0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Reload CS via far return
    ; Push the new CS selector and return address, then do a far return
    pop rax                     ; Get return address
    push 0x08                   ; Push kernel code selector
    push rax                    ; Push return address
    retfq                       ; Far return to reload CS

; ============================================================================
; void tss64_load(uint16_t selector);
; ============================================================================
; Load the Task Register with the TSS selector.
;
; Parameters:
;   DI = TSS segment selector (lower 16 bits of RDI)
;
; The LTR instruction loads the Task Register, which points to the TSS.
; This is required for:
;   - RSP0 stack switching during privilege transitions (user -> kernel)
;   - IST (Interrupt Stack Table) for dedicated interrupt stacks
; ============================================================================
tss64_load:
    ltr di                      ; Load Task Register with selector in DI
    ret
