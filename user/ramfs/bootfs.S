/* 启动映像：一个 ustar 归档（BOOTFS_IMAGE 由 Makefile 传入） */
    .section .rodata
    .balign 16
    .global bootfs_start
    .global bootfs_end
bootfs_start:
    .incbin BOOTFS_IMAGE
bootfs_end:
