#include "core.h"

#define CORE_MANAGER_CHANNEL 1

uintptr_t buffer_vaddr;

void init(void) {
    microkit_dbg_puts("[Core Worker #1]: Starting!\n");
}

void notified(microkit_channel ch) {
    if (ch != CORE_MANAGER_CHANNEL) {
        microkit_dbg_puts("Received unexpected notification\n");
        return;
    }

    switch (((char *) buffer_vaddr)[0]) {
    case 'd':
        microkit_dbg_puts("\n=== THE FOLLOWING DUMP IS FOR PROTECTION DOMAINS RUNNING ON [PD 2]'s CORE ===\n");
        seL4_DebugDumpScheduler();
        break;
    case 'n':
        core_migrate(1);
        break;
    case 'x':
        microkit_dbg_puts("[Core Worker #1]: Turning off core #");
        uart_print_num(current_cpu);
        microkit_dbg_puts("\n");
        
        core_off();
        break;
    case 's':
        core_standby();
        break;
    }
}
