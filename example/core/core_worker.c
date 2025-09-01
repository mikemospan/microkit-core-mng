#include "core.h"

#define CORE_MANAGER_CHANNEL 1

uintptr_t buffer_vaddr;

void init(void) {
    microkit_dbg_puts("[Core Worker]: Starting!\n");
}

void notified(microkit_channel ch) {
    if (ch != CORE_MANAGER_CHANNEL) {
        microkit_dbg_puts("Received unexpected notification: ");
        microkit_dbg_put32(ch);
        microkit_dbg_putc('\n');
        return;
    }

    switch (((char *) buffer_vaddr)[0]) {
    case 'd':
        seL4_DebugDumpScheduler();
        break;
    case 'x':
        microkit_dbg_puts("[Core Worker #1]: Turning off core #1\n");
        core_off();
        break;
    case 's':
        core_standby();
        break;
    }
}
