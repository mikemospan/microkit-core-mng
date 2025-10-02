#pragma once

#include <stdint.h>

uintptr_t uart_base_vaddr;

#define UART_IRQ_CH             2

#define UART_REG(offset)        (*(volatile uint32_t *)(uart_base_vaddr + (offset)))
#define BIT(nr)                 (1UL << (nr))

#define UART_WFIFO              0x0
#define UART_RFIFO              0x4
#define UART_CTRL               0x8
#define UART_STATUS             0xC

#define UART_TX_EN              BIT(12)
#define UART_RX_EN              BIT(13)
#define UART_RX_EMPTY           BIT(20)
#define UART_TX_FULL            BIT(21)
#define UART_TX_RST             BIT(22)
#define UART_RX_RST             BIT(23)
#define UART_CLEAR_ERR          BIT(24)
#define UART_TX_BUSY            BIT(25)
#define UART_RX_BUSY            BIT(26)

#define UART_STOP_BIT_LEN_MASK  (0x03 << 16)
#define UART_STOP_BIT_1SB       (0x00 << 16)
#define UART_DATA_LEN_MASK      (0x03 << 20)
#define UART_DATA_LEN_8BIT      (0x00 << 20)

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

    /* Enable transmit and receive */
    UART_REG(UART_CTRL) |= (UART_TX_EN | UART_RX_EN);
}

static char uart_getc(void) {
    // Wait for character
    while (UART_REG(UART_STATUS) & UART_RX_EMPTY);

    char c = UART_REG(UART_RFIFO) & 0xFF;
    if (c == 8) {
        c = 127; // Map backspace to delete
    }
    return c;
}

static void uart_putc(char ch) {
    while ((UART_REG(UART_STATUS) & UART_TX_FULL));

    UART_REG(UART_WFIFO) = ch;
    if (ch == '\r') {
        uart_putc('\n');
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
