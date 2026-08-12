.section .text
.global _start
.type _start, %function
_start:
    mov x0, sp
    bl omega_start
1:  wfe
    b 1b
