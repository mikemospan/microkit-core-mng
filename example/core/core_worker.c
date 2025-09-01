#include "core.h"

#define CORE_MANAGER_CHANNEL 1

static void core_off();
static void core_standby();

Instruction *instruction_vaddr;

void init(void) {
    /* No init needed. */
}

void notified(microkit_channel ch) {
    if (ch != CORE_MANAGER_CHANNEL) {
        microkit_dbg_puts("[Core Worker]: Received unexpected notification: ");
        microkit_dbg_put32(ch);
        microkit_dbg_putc('\n');
        return;
    }

    switch (instruction_vaddr[0]) {
    case CORE_DUMP:
        seL4_DebugDumpScheduler();
        break;
    case CORE_OFF:
        microkit_dbg_puts("[Core Worker]: Turning off core.\n");
        core_off();
        break;
    case CORE_STANDBY:
        core_standby();
        break;
    default:
        microkit_dbg_puts("[Core Worker]: Encountered unexpected instruction.\n");
        break;
    }
}

static void core_off() {
    seL4_ARM_SMCContext args = {0};
    seL4_ARM_SMCContext response = {0};
    args.x0 = PSCI_CPU_OFF;

    microkit_arm_smc_call(&args, &response);

    print_error(response);
}

/**
 * Suspend a CPU core to a low "standby" power state
 */
static void core_standby() {
    seL4_ARM_SMCContext args = {.x0 = PSCI_CPU_SUSPEND, .x1 = 0};
    seL4_ARM_SMCContext response = {0};
    
    microkit_arm_smc_call(&args, &response);
    
    print_error(response);
}
