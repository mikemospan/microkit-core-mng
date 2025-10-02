/*
 * Copyright 2021, Breakaway Consulting Pty. Ltd.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdint.h>
#include <microkit.h>
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

void init(void)
{
    microkit_dbg_puts("hello, world\n");
    while(1) {
        uart_putc(uart_getc());
    }
}

void notified(microkit_channel ch)
{
}
