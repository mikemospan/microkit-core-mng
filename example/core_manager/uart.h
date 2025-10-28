#pragma once

#include <stdint.h>

uintptr_t uart_base_vaddr;

#define UART_IRQ_CH             2

#define UART_REG(offset)        (*(volatile uint32_t *)(uart_base_vaddr + (offset)))
#define BIT(nr)                 (1UL << (nr))

#ifdef CONFIG_PLAT_ODROIDC4
#define UART_WFIFO              0x00
#define UART_RFIFO              0x04
#define UART_CTRL               0x08
#define UART_STATUS             0x0c
#define UART_IRQ_CTRL           0x10

#define UART_RX_EMPTY           BIT(20)
#define UART_TX_FULL            BIT(21)
#define UART_RX_INT_EN          BIT(27)

#define UART_RECV_IRQ_MASK      0xff
#define UART_RECV_IRQ(c)        ((c) & 0xff)

static inline void uart_init(void) {
    /* Enable RX byte interrupts */
    UART_REG(UART_IRQ_CTRL) &= ~UART_RECV_IRQ_MASK;
    UART_REG(UART_IRQ_CTRL) |= UART_RECV_IRQ(1);
    UART_REG(UART_CTRL) |= UART_RX_INT_EN;
}

static void uart_putc(char ch) {
    // Wait until UART can accept a new character
    while ((UART_REG(UART_STATUS) & UART_TX_FULL));
    UART_REG(UART_WFIFO) = ch;
}

static char uart_getc(void) {
    // Drain all pending characters from RX FIFO
    char ch = '\0';
    while (!(UART_REG(UART_STATUS) & UART_RX_EMPTY)) {
        ch = UART_REG(UART_RFIFO);
    }

    if (ch == 8) {
        ch = 127; // Map backspace to delete
    }
    return ch;
}

static void uart_handle_irq(void) {
    
}

#elif defined(CONFIG_PLAT_MAAXBOARD)
#define UART_RXD                0x0
#define UART_TRANSMIT           0x40
#define UART_CTRL               0x80
#define UART_FCR                0x90
#define UART_STAT               0x98
#define UART_TS                 0xb4

#define UART_TST_RX_FIFO_EMPTY  BIT(5)
#define UART_CR1_RX_READY_INT   BIT(9)
#define UART_STAT_TDRE          BIT(14)

static void uart_init(void) {
    /* Enable receive interrupts */
    UART_REG(UART_CTRL) |= UART_CR1_RX_READY_INT;
}

static void uart_putc(char ch) {
    while (!(UART_REG(UART_STAT) & UART_STAT_TDRE));
    UART_REG(UART_TRANSMIT) = ch;
}

static char uart_getc(void) {
    char ch = '\0';
    // Wait until UART can accept a new character
    while (!(UART_REG(UART_TS) & UART_TST_RX_FIFO_EMPTY)) {
        ch = UART_REG(UART_RXD);
    }
    
    if (ch == 8) {
        ch = 127; // Map backspace to delete
    }
    return ch;
}

static void uart_handle_irq(void) {
    
}

#elif defined(CONFIG_PLAT_QEMU_ARM_VIRT)
#define UART_DR                 0x00
#define UART_FR                 0x18
#define UART_IMSC               0x38
#define UART_ICR                0x44
#define PL011_UART_FR_TXFF      BIT(5)
#define PL011_UART_FR_RXFE      BIT(4)

static void uart_init(void) {
    /* Enable receive interrupt and receive timeout interrupt. */
    UART_REG(UART_IMSC) = 0b1010000;
}

static void uart_putc(char ch) {
    while ((UART_REG(UART_FR) & PL011_UART_FR_TXFF) != 0);
    UART_REG(UART_DR) = ch;
}

static int uart_getc(void) {
    char ch = '\0';
    while (!(UART_REG(UART_FR) & PL011_UART_FR_RXFE)) {
        ch = UART_REG(UART_DR);
    }

    if (ch == 8) {
        ch = 127; // Map backspace to delete
    }
    return ch;
}

static void uart_handle_irq(void) {
    
}

#elif defined (CONFIG_PLAT_ZYNQMP)
#define UART_CHANNEL_STS_TXEMPTY 0x8
#define UART_CHANNEL_STS         0x2C
#define UART_TX_RX_FIFO          0x30
#define ZYNQMP_UART_IDR           0x0C  /* Interrupt Disable */
#define ZYNQMP_UART_IXR_MASK    0x00001FFFU /**< Valid bit mask */
#define ZYNQMP_UART_ISR           0x14  /* Interrupt Status */
#define ZYNQMP_UART_RXWM          0x20  /* RX FIFO Trigger Level */
#define ZYNQMP_UART_IER           0x08  /* Interrupt Enable */
#define ZYNQMP_UART_IXR_RXOVR   0x00000001U /**< RX FIFO trigger interrupt. */

#define UART_CR             0x00
#define UART_CR_TX_EN       BIT(4)
#define UART_CR_TX_DIS      BIT(5)
#define UART_CHANNEL_STS_RXEMPTY  BIT(1)

static void uart_init(void) {
    /* Turn off all the interrupts, then only turn on the ones we need. */
    UART_REG(ZYNQMP_UART_IDR) = ZYNQMP_UART_IXR_MASK;
    UART_REG(ZYNQMP_UART_ISR) = ZYNQMP_UART_IXR_MASK;

    /* Set the watermark to raise an interrupt for every received byte. */
    UART_REG(ZYNQMP_UART_RXWM) = 1;
    /* Enable IRQ on every bytes received. */
    UART_REG(ZYNQMP_UART_IER) = ZYNQMP_UART_IXR_RXOVR;
}

static void uart_putc(uint8_t ch) {
    while (!(UART_REG(UART_CHANNEL_STS) & UART_CHANNEL_STS_TXEMPTY));
    UART_REG(UART_TX_RX_FIFO) = ch;
}

static char uart_getc(void) {
    char ch = '\0';
    while (!(UART_REG(UART_CHANNEL_STS) & UART_CHANNEL_STS_RXEMPTY)) {
        ch = UART_REG(UART_TX_RX_FIFO);
    }

    if (ch == 8) {
        ch = 127; // Map backspace to delete
    }
    return ch;
}

static void uart_handle_irq(void) {
    /* Read and clear the IRQ status bits so we don't get infinitely interrupted. */
    uint32_t irq_status = UART_REG(ZYNQMP_UART_ISR);
    UART_REG(ZYNQMP_UART_ISR) = irq_status;
}
#endif

static void uart_puts(const char *str) {
#if PRINTING
    for (; *str != '\0'; str++) {
        if (*str == '\n') {
            uart_putc('\r');
        } else if (*str == '\r') {
            uart_putc('\n');
        }
        uart_putc(*str);
    }
#endif
}

static void uart_put64(uint64_t num) {
#if PRINTING
    if (num == 0) {
        uart_putc('0');
        return;
    }
    if (num > 9) {
        uart_put64(num / 10);
    }
    uart_putc('0' + (num % 10));
#endif
}

static void uart_puthex64(uint64_t num) {
#if PRINTING
    uart_puts("0x");
    int started = 0;
    for (int i = 15; i >= 0; i--) {
        uint8_t nibble = (num >> (i * 4)) & 0xF;
        if (nibble || started || i == 0) {
            started = 1;
            if (nibble < 10) {
                uart_putc('0' + nibble);
            } else {
                uart_putc('a' + (nibble - 10));
            }
        }
    }
#endif
}

static void uart_putfloat(uint64_t numerator, uint64_t denominator, int decimal_places) {
#if PRINTING
    // Print integer part
    uint64_t integer_part = numerator / denominator;
    uart_put64(integer_part);
    
    // Print decimal point
    uart_putc('.');
    
    // Print decimal part
    uint64_t remainder = numerator % denominator;
    for (int i = 0; i < decimal_places; i++) {
        remainder *= 10;
        uint64_t digit = remainder / denominator;
        uart_putc('0' + digit);
        remainder = remainder % denominator;
    }
#endif
}
