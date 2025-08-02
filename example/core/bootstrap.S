/* bootstrap.S - Simple bootstrap code for newly started CPU core (AArch64) */

.section ".text.bootstrap"

.global __bootstrap_phys_start
.global __bootstrap_phys_end
.global assembly_print_start
.global assembly_print_end

#define UART_BASE                 0x2000000
#define PL011_UARTDR              0x000
#define PL011_UARTCR              0x030
#define PL011_CR_UART_EN          (1 << 0)
#define PL011_CR_TX_EN            (1 << 8)

__bootstrap_phys_start:
assembly_print_start:
    ldr x0, =UART_BASE
   
    // Enable UART and TX
    mov w1, #(PL011_CR_TX_EN | PL011_CR_UART_EN)
    str w1, [x0, #PL011_UARTCR]
   
    mov w2, #'B'
    str w2, [x0, #PL011_UARTDR]
    
    mov w2, #'O'
    str w2, [x0, #PL011_UARTDR]
    
    mov w2, #'O'
    str w2, [x0, #PL011_UARTDR]
    
    mov w2, #'T'
    str w2, [x0, #PL011_UARTDR]
    
    mov w2, #'\n'
    str w2, [x0, #PL011_UARTDR]
   
    ret

assembly_print_end:
__bootstrap_phys_end:
.size assembly_print_start, assembly_print_end - assembly_print_start
