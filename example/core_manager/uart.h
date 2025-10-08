#pragma once

#include <stdint.h>

uintptr_t uart_base_vaddr;

#define UART_IRQ_CH             2

#define UART_REG(offset)        (*(volatile uint32_t *)(uart_base_vaddr + (offset)))
#define BIT(nr)                 (1UL << (nr))

#ifdef CONFIG_PLAT_ODROIDC4
#define UART_WFIFO              0x00
#define UART_RFIFO              0x04
#define UART_STATUS             0x0c

#define UART_RX_EMPTY           BIT(20)
#define UART_TX_FULL            BIT(21)

#define UART_RECV_IRQ_MASK      0xff
#define UART_RECV_IRQ(c)        ((c) & 0xff)

static inline void uart_init(void) {
    
}

static void uart_putc(char ch) {
    // Wait until UART can accept a new character
    while ((UART_REG(UART_STATUS) & UART_TX_FULL));
    UART_REG(UART_WFIFO) = ch;

    // Handle cursor: ensure both LF and CR are sent
    if (ch == '\n') {
        while ((UART_REG(UART_STATUS) & UART_TX_FULL));
        UART_REG(UART_WFIFO) = '\r';
    } else if (ch == '\r') {
        while ((UART_REG(UART_STATUS) & UART_TX_FULL));
        UART_REG(UART_WFIFO) = '\n';
    }
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

#elif defined(CONFIG_PLAT_MAAXBOARD)
#define STAT 0x98
#define TRANSMIT 0x40
#define STAT_TDRE (1 << 14)
#define UART_FCR_RXTL_MASK          (0x3F)
#define UART_FCR_RXTL_SHFT          (0)
#define UART_FCR 0x90

#define UART_TS                 0xb4
#define UART_TST_RX_FIFO_EMPTY      BIT(5)          /* Rx FIFO is empty. */
#define UART_RXD    0x0
#define UART_CR2_RX_EN              BIT(1)
#define UART_CR2    0x84

#define UART_CR2_WORD_SZE           BIT(5)
#define UART_CR2_STOP_BITS          BIT(6)

#define UART_CR2_PARITY_EN          BIT(8)          /* Enables the parity generator and checker. */
#define UART_CR2_ESCAPE_EN          BIT(11)         /* Enables the escape sequence detection logic. */
#define UART_CR2_ESCAPE_INT         BIT(15)         /* Enables escape interupts. */
#define UART_CR2_AGE_EN             BIT(3)

static void uart_init(void) {
    /* Enable receiver */
    UART_REG(UART_CR2) |= UART_CR2_RX_EN;

    /* Configure stop bit length to 1 and data length to 8 */
    UART_REG(UART_CR2) &= ~(UART_CR2_STOP_BITS);
    UART_REG(UART_CR2) |= UART_CR2_WORD_SZE;

    /* Disable escape sequence, parity checking and aging rx data interrupts. */
    UART_REG(UART_CR2) &= ~UART_CR2_PARITY_EN;
    UART_REG(UART_CR2) &= ~(UART_CR2_ESCAPE_EN | UART_CR2_ESCAPE_INT);
    UART_REG(UART_CR2) &= ~UART_CR2_AGE_EN;

    /* Enable receive interrupts every byte */
    UART_REG(UART_FCR) &= ~UART_FCR_RXTL_MASK;
    UART_REG(UART_FCR) |= (1 << UART_FCR_RXTL_SHFT);
}

static void uart_putc(char ch) {
    while (!(UART_REG(STAT) & STAT_TDRE));
    UART_REG(TRANSMIT) = ch;

    // Handle cursor: ensure both LF and CR are sent
    if (ch == '\n') {
        while (!(UART_REG(STAT) & STAT_TDRE));
        UART_REG(TRANSMIT) = '\r';
    } else if (ch == '\r') {
        while (!(UART_REG(STAT) & STAT_TDRE));
        UART_REG(TRANSMIT) = '\n';
    }
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

#elif defined(CONFIG_PLAT_QEMU_ARM_VIRT)

#define RHR_MASK               0b111111111
#define UARTDR                 0x000
#define UARTFR                 0x018
#define UARTIMSC               0x038
#define UARTICR                0x044
#define PL011_UARTFR_TXFF      (1 << 5)
#define PL011_UARTFR_RXFE      (1 << 4)

static void uart_init(void) {
    UART_REG(UARTIMSC) = 0x50;
}

static void uart_putc(char ch) {
    while ((UART_REG(UARTFR) & PL011_UARTFR_TXFF) != 0);
    UART_REG(UARTDR) = ch;
    if (ch == '\r') {
        uart_putc('\n');
    }
}

static int uart_getc(void) {
    char ch = '\n';
    if ((UART_REG(UARTFR) & PL011_UARTFR_RXFE) == 0) {
        ch = UART_REG(UARTDR) & RHR_MASK;
    }
    switch (ch) {
    case '\n':
        ch = '\r';
        break;
    case 8:
        ch = 0x7f;
        break;
    }
    return ch;
}
#endif

static void uart_puts(const char *str) {
    for (; *str != '\0'; str++) {
        uart_putc(*str);
    }
}

static void uart_put64(uint64_t num) {
    if (num == 0) {
        uart_putc('0');
        return;
    }
    if (num > 9) {
        uart_put64(num / 10);
    }
    uart_putc('0' + (num % 10));
}

static void uart_puthex64(uint64_t num) {
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
}
