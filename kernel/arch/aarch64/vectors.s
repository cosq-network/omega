/* AArch64 Exception Vector Table */
.section .text.vectors, "ax"
.align 11 /* Must be 2048-byte aligned (2^11) */
.global el1_vector_table
el1_vector_table:

/* Current EL with SP0 */
.align 7
    b unhandled_exception
.align 7
    b unhandled_exception
.align 7
    b unhandled_exception
.align 7
    b unhandled_exception

/* Current EL with SPx */
.align 7
    b unhandled_exception
.align 7
    b unhandled_exception
.align 7
    b unhandled_exception
.align 7
    b unhandled_exception

/* Lower EL using AArch64 */
.align 7
    b unhandled_exception
.align 7
    b unhandled_exception
.align 7
    b unhandled_exception
.align 7
    b unhandled_exception

/* Lower EL using AArch32 */
.align 7
    b unhandled_exception
.align 7
    b unhandled_exception
.align 7
    b unhandled_exception
.align 7
    b unhandled_exception

unhandled_exception:
    wfe
    b unhandled_exception
