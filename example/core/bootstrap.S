/* bootstrap.S - Simple bootstrap code for newly started CPU core (AArch64) */

.section .text.bootstrap, "ax", %progbits

.global __bootstrap_phys_start
.global __bootstrap_phys_end
.global assembly_print_start
.global assembly_print_end

__bootstrap_phys_start:
assembly_print_start:
    ldr x0, =0x02000000    /* UART base in QEMU virt */

    mov w1, #'B'
    str w1, [x0]

    mov w1, #'O'
    str w1, [x0]

    mov w1, #'O'
    str w1, [x0]

    mov w1, #'T'
    str w1, [x0]

    mov w1, #'\n'
    str w1, [x0]

assembly_print_end:
    ret
__bootstrap_phys_end:
.size assembly_print_start, assembly_print_end - assembly_print_start
