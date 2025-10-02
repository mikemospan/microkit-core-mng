#pragma once

#include <stdint.h>

uintptr_t uart_base_vaddr;

#define UART_IRQ_CH             2

#define UART_REG(offset)        (*(volatile uint32_t *)(uart_base_vaddr + (offset)))
#define BIT(nr)                 (1UL << (nr))

#define UART_WFIFO              0x00
#define UART_RFIFO              0x04
#define UART_CTRL               0x08
#define UART_STATUS             0x0c
#define UART_IRQ_CTRL           0x10

#define UART_TX_EN              BIT(12)
#define UART_RX_EN              BIT(13)
#define UART_RX_EMPTY           BIT(20)
#define UART_TX_FULL            BIT(21)
#define UART_TX_RST             BIT(22)
#define UART_RX_RST             BIT(23)
#define UART_CLEAR_ERR          BIT(24)
#define UART_TX_BUSY            BIT(25)
#define UART_RX_BUSY            BIT(26)
#define UART_RX_INT_EN          BIT(27)

#define UART_STOP_BIT_LEN_MASK  (0x03 << 16)
#define UART_STOP_BIT_1SB       (0x00 << 16)
#define UART_DATA_LEN_MASK      (0x03 << 20)
#define UART_DATA_LEN_8BIT      (0x00 << 20)

#define UART_RECV_IRQ_MASK      0xff
#define UART_RECV_IRQ(c)        ((c) & 0xff)

static char uart_getc(void) {
    // Drain all pending characters from RX FIFO
    char c = '\0';
    while (!(UART_REG(UART_STATUS) & UART_RX_EMPTY)) {
        c = UART_REG(UART_RFIFO);
    }

    if (c == 8) {
        c = 127; // Map backspace to delete
    }
    return c;
}

static inline void uart_init(void) {
    /* Wait until receive and transmit state machines are no longer busy */
    while (UART_REG(UART_STATUS) & (UART_TX_BUSY | UART_RX_BUSY));

    /* Disable transmit and receive */
    UART_REG(UART_CTRL) &= ~(UART_TX_EN | UART_RX_EN);

    /* Reset UART state machine */
    UART_REG(UART_CTRL) |= (UART_TX_RST | UART_RX_RST | UART_CLEAR_ERR);
    UART_REG(UART_CTRL) &= ~(UART_TX_RST | UART_RX_RST | UART_CLEAR_ERR);

    /* Configure stop bit length to 1 */
    UART_REG(UART_CTRL) &= ~(UART_STOP_BIT_LEN_MASK);
    UART_REG(UART_CTRL) |= UART_STOP_BIT_1SB;

    /* Set data length to 8 */
    UART_REG(UART_CTRL) &= ~UART_DATA_LEN_MASK;
    UART_REG(UART_CTRL) |= UART_DATA_LEN_8BIT;

    /* Enable RX byte interrupts */
    UART_REG(UART_IRQ_CTRL) &= ~UART_RECV_IRQ_MASK;
    UART_REG(UART_IRQ_CTRL) |= UART_RECV_IRQ(1);
    UART_REG(UART_CTRL) |= UART_RX_INT_EN;

    /* Enable transmit and receive */
    UART_REG(UART_CTRL) |= (UART_TX_EN | UART_RX_EN);
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
