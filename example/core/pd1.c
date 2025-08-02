#include "core.h"
#define PD2_CHANNEL 2

uintptr_t buffer_vaddr;
uintptr_t cpu_bootstrap_phys_start = 0x80000000;

extern void assembly_print_start(void);

void init(void) {
    microkit_dbg_puts("[PD 1]: Starting!\n");
    uart_init();
    
    /* Print bootstrap info for debugging */
    microkit_dbg_puts("[PD 1]: Bootstrap code available at physical address: ");
    uart_print_hex(cpu_bootstrap_phys_start);
    microkit_dbg_puts("\n");

    assembly_print_start();
}

void notified(microkit_channel ch) {
    if (ch != UART_IRQ_CH) {
        microkit_dbg_puts("Received unexpected notification\n");
        return;
    }
    ((char *) buffer_vaddr)[0] = uart_get_char();
    uart_handle_irq();
    switch (((char *) buffer_vaddr)[0]) {
    case 'h':
        microkit_dbg_puts(
            "\n=== LIST OF COMMANDS ===\n"
            "h: help\n"
            "p: print psci version\n"
            "i: view the status of core #0\n"
            "d: core dump\n"
            "m: migrate pd1\n"
            "n: migrate pd2\n"
            "x: turn off pd2's core\n"
            "s: put pd2's core in standby\n"
            "y: turn on pd2's core\n"
        );
        break;
    case 'p':
        print_psci_version();
        break;
    case 'd':
        microkit_dbg_puts("=== THE FOLLOWING DUMP IS FOR PROTECTION DOMAINS RUNNING ON PD1's CORE ===\n");
        seL4_DebugDumpScheduler();
        microkit_notify(PD2_CHANNEL);
        break;
    case 's':
        microkit_notify(PD2_CHANNEL);
        break;
    case 'm':
        core_migrate(0);
        seL4_IRQHandler_SetCore(BASE_IRQ_CAP + UART_IRQ_CH, 2);
        break;
    case 'n':
        microkit_notify(PD2_CHANNEL);
        break;
    case 'x':
        microkit_notify(PD2_CHANNEL);
        break;
    case 'y':
        microkit_dbg_puts("[PD 1]: Turning on core #3");
        
        /* Use the actual bootstrap physical address */
        core_on(3, (uintptr_t)cpu_bootstrap_phys_start);
        break;
    case 'i':
        microkit_dbg_puts("[PD 1]: Viewing status of core #3\n");
        core_status(3);
        break;
    }

    microkit_irq_ack(ch);
}
