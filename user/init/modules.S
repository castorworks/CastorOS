/* init 启动的模块的 ELF 映像（<name>_IMAGE 由 Makefile 传入） */

.macro module name, path
    .balign 16
    .global \name\()_image_start
    .global \name\()_image_end
\name\()_image_start:
    .incbin "\path"
\name\()_image_end:
.endm

    .section .rodata
    module uart, uart_IMAGE
    module demo, demo_IMAGE
