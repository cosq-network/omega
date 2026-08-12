.section .text
.global _start
.type _start, @function
_start:
    mv a0, sp
    call omega_start
1:  wfi
    j 1b
