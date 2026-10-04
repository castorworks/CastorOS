/* 把 init 的 ELF 映像嵌入内核（INIT_IMAGE 由 Makefile 传入） */
    .section .rodata
    .balign 16
    .global init_image_start
    .global init_image_end
init_image_start:
    .incbin INIT_IMAGE
init_image_end:
