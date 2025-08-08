#include "core.h"

#define PD2_CHANNEL 2

uintptr_t buffer_vaddr;
uintptr_t bootstrap_vaddr;

extern char bootstrap_start[];
extern char bootstrap_end[];

static void *memcpy(void *dst, const void *src, uint64_t sz);

void init(void) {
    microkit_dbg_puts("[PD 1]: Starting!\n");
    uart_init();
    
    // Copy the entire bootstrap section to the bootstrap memory region.
    uint64_t bootstrap_size = (uintptr_t)bootstrap_end - (uintptr_t)bootstrap_start;
    memcpy((void*)bootstrap_vaddr, bootstrap_start, bootstrap_size);
    
    asm volatile("dsb sy" ::: "memory");
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
            "m: migrate core_manager\n"
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
        microkit_dbg_puts("=== THE FOLLOWING DUMP IS FOR PROTECTION DOMAINS RUNNING ON THE CORE MANAGER's CORE ===\n");
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
        core_on(3, 0x80000000);
        break;
    case 'i':
        microkit_dbg_puts("[Core Manager]: Viewing status of core #3\n");
        core_status(3);
        break;
    }

    microkit_irq_ack(ch);
}

static void *memcpy(void *dst, const void *src, uint64_t sz)
{
    char *dst_ = dst;
    const char *src_ = src;
    while (sz-- > 0) {
        *dst_++ = *src_++;
    }

    return dst;
}
