/* bootstrap.S - Bootstrap code for CPU core startup */

.section ".text.bootstrap"

.global bootstrap_start
.global bootstrap_end

#define UART_BASE_PHYS           0x09000000
#define PL011_UARTDR             0x000
#define PL011_UARTCR             0x030
#define PL011_CR_UART_EN         (1 << 0)
#define PL011_CR_TX_EN           (1 << 8)

bootstrap_start:
    // Load UART physical address 0x9_000_000 into x0
    movz x0, #0x0900, LSL #16
    
    // Enable UART transmitter
    mov w1, #(PL011_CR_TX_EN | PL011_CR_UART_EN)
    str w1, [x0, #PL011_UARTCR]
    
    // Print startup message
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
    
    // CPU 3 idle loop
cpu_idle:
    wfi
    b cpu_idle
    
bootstrap_end:
.size bootstrap_start, bootstrap_end - bootstrap_start