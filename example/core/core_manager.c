#include "core.h"

#define API_CHANNEL 1

void init(void) {
    microkit_dbg_puts("[Core Manager]: Starting!\n");
    uart_init();
}

void notified(microkit_channel ch) {
    if (ch != UART_IRQ_CH) {
        microkit_dbg_puts("Received unexpected notification: ");
        microkit_dbg_put32(ch);
        microkit_dbg_putc('\n');
        return;
    }

    char input = uart_get_char();
    uart_handle_irq();

    microkit_mr_set(1, 3);
    microkit_mr_set(2, 1);

    seL4_Bool ppc = 1;
    switch (input) {
    case 'h':
        microkit_dbg_puts(
            "\n=== LIST OF COMMANDS ===\n"
            "h: help\n"
            "i: view the status of core #0\n"
            "d: core dump\n"
            "m: migrate pd\n"
            "x: turn off pd2's core\n"
            "s: put pd2's core in standby\n"
            "y: turn on pd2's core\n"
        );
        ppc = 0;
        break;
    case 'd':
        microkit_mr_set(0, 5);
        break;
    case 's':
        microkit_mr_set(0, 2);
        break;
    case 'm':
        microkit_mr_set(0, 3);
        break;
    case 'x':
        microkit_mr_set(0, 1);
        break;
    case 'y':
        microkit_mr_set(0, 0);
        break;
    case 'i':
        microkit_mr_set(0, 4);
        break;
    default:
        microkit_dbg_puts("Not a valid command! Click 'h' to view all commands.\n");
        ppc = 0;
    }

    if (ppc) {
        microkit_ppcall(API_CHANNEL, microkit_msginfo_new(0, 3));
        seL4_Word success = microkit_mr_get(0);
        if (success != 0) {
            microkit_dbg_puts("Core Manager API did not succeed in your request.\n");
        }
    }
    microkit_irq_ack(ch);
}
