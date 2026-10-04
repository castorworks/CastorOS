/* init 启动的模块的 ELF 映像（路径由 Makefile 传入） */
    .section .rodata
    .balign 16
    .global uart_image_start
    .global uart_image_end
uart_image_start:
    .incbin UART_IMAGE
uart_image_end:
