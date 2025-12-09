; ============================================================================
; idt64_asm.asm - IDT Assembly Support Functions (x86_64)
; ============================================================================
; This file contains assembly routines for loading the IDT in 64-bit mode.
;
; In 64-bit mode:
;   - LIDT uses a 10-byte pointer (2-byte limit + 8-byte base)
;   - IDT entries are 16 bytes each (vs 8 bytes in 32-bit mode)
;
; Requirements: 3.4 - 64-bit IDT format with IST support

[BITS 64]

section .text

global idt64_flush

; ============================================================================
; void idt64_flush(uint64_t idt_ptr);
; ============================================================================
; Load the IDT using the LIDT instruction.
;
; Parameters:
;   RDI = pointer to IDT pointer structure (limit:uint16 + base:uint64)
;
; The LIDT instruction loads the IDT register (IDTR) with:
;   - A 16-bit limit (size of IDT - 1)
;   - A 64-bit base address
;
; After loading, the CPU will use this IDT for all interrupt/exception handling.
; ============================================================================
idt64_flush:
    lidt [rdi]                  ; Load IDT from pointer structure
    ret
