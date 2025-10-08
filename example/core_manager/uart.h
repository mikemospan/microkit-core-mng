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

    // Handle cursor: ensure both LF and CR are sent
    if (ch == '\n') {
        while (!(UART_REG(UART_STAT) & UART_STAT_TDRE));
        UART_REG(UART_TRANSMIT) = '\r';
    } else if (ch == '\r') {
        while (!(UART_REG(UART_STAT) & UART_STAT_TDRE));
        UART_REG(UART_TRANSMIT) = '\n';
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
    if (ch == '\r') {
        uart_putc('\n');
    }
}

static int uart_getc(void) {
    char ch = '\n';
    while (!(UART_REG(UART_FR) & PL011_UART_FR_RXFE)) {
        ch = UART_REG(UART_DR);
    }

    if (ch == 8) {
        ch = 127; // Map backspace to delete
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
