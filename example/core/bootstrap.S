/* bootstrap.S - Simplified Bootstrap code for CPU core startup */

.section ".text.bootstrap"

.global bootstrap_start
.global bootstrap_end

#define UART_BASE_PHYS           0x09000000
#define PL011_UARTDR             0x000
#define PL011_UARTCR             0x030
#define PL011_CR_UART_EN         (1 << 0)
#define PL011_CR_TX_EN           (1 << 8)

/* Stack configuration */
#define STACK_SIZE               0x1000      // 4KB per CPU
#define STACK_BASE               0x80001000  // Base address for stacks

bootstrap_start:
    // Load UART physical address 0x9_000_000 into x0
    movz x0, #0x0900, LSL #16
   
    // Enable UART transmitter
    mov w1, #(PL011_CR_TX_EN | PL011_CR_UART_EN)
    str w1, [x0, #PL011_UARTCR]
   
    // Print "BOOTING CPU " character by character
    mov w2, #'B'
    str w2, [x0, #PL011_UARTDR]
    mov w2, #'O'
    str w2, [x0, #PL011_UARTDR]
    mov w2, #'O'
    str w2, [x0, #PL011_UARTDR]
    mov w2, #'T'
    str w2, [x0, #PL011_UARTDR]
    mov w2, #'I'
    str w2, [x0, #PL011_UARTDR]
    mov w2, #'N'
    str w2, [x0, #PL011_UARTDR]
    mov w2, #'G'
    str w2, [x0, #PL011_UARTDR]
    mov w2, #' '
    str w2, [x0, #PL011_UARTDR]
    mov w2, #'C'
    str w2, [x0, #PL011_UARTDR]
    mov w2, #'P'
    str w2, [x0, #PL011_UARTDR]
    mov w2, #'U'
    str w2, [x0, #PL011_UARTDR]
    mov w2, #' '
    str w2, [x0, #PL011_UARTDR]

    // Get current CPU ID from MPIDR_EL1
    mrs x1, mpidr_el1
    and x1, x1, #0xFF           // Extract CPU ID (bits [7:0])
   
    // Print CPU ID
    add w2, w1, #'0'            // Convert to ASCII (assumes CPU ID 0-9)
    str w2, [x0, #PL011_UARTDR]
    mov w2, #'\n'
    str w2, [x0, #PL011_UARTDR]

    // Calculate stack pointer for this CPU
    // Stack formula: STACK_BASE + (cpu_id * STACK_SIZE) + STACK_SIZE
    lsl x3, x1, #12             // x3 = cpu_id * 4096
    movz x4, #0x8000, LSL #16   // Load STACK_BASE high bits
    add x4, x4, #0x1000         // Add low bits (0x80001000)
    add x3, x4, x3              // x3 = STACK_BASE + (cpu_id * STACK_SIZE)
    add x3, x3, #STACK_SIZE     // x3 = stack_top for this CPU

    // Set the stack pointer and call secondary entry point
    mov sp, x3
    bl secondary_cpu_entry
    
    // Print "OK"
    mov w2, #'O'
    str w2, [x0, #PL011_UARTDR]
    mov w2, #'K'
    str w2, [x0, #PL011_UARTDR]
    mov w2, #'\n'
    str w2, [x0, #PL011_UARTDR]
    
    b idle

idle:
    wfi
    b idle
   
bootstrap_end:
.size bootstrap_start, bootstrap_end - bootstrap_start
