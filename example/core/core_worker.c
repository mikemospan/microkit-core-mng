#include "core.h"
#include "uart.h"

#define CORE_MANAGER_CHANNEL 1

static void core_off();
static void core_suspend(seL4_Bool power_down);

Instruction *instruction_vaddr;

void init(void) {
    /* No init needed. */
}

void notified(microkit_channel ch) {
    if (ch != CORE_MANAGER_CHANNEL) {
        uart_puts("[Core Worker]: Received unexpected notification: ");
        uart_put64(ch);
        uart_putc('\n');
        return;
    }

    switch (instruction_vaddr[0]) {
    case CORE_DUMP:
        seL4_DebugDumpScheduler();
        break;
    case CORE_OFF:
        uart_puts("[Core Worker]: Turning off core.\n");
        core_off();
        break;
    case CORE_POWERDOWN:
        uart_puts("[Core Worker]: Powering down core.\n");
        core_suspend(1);
        break;
    case CORE_STANDBY:
        core_suspend(0);
        break;
    default:
        uart_puts("[Core Worker]: Encountered unexpected instruction.\n");
        break;
    }
}

static void core_off() {
    seL4_ARM_SMCContext args = {.x0 = PSCI_CPU_OFF};
    seL4_ARM_SMCContext response = {0};

    microkit_arm_smc_call(&args, &response);

    print_error(response);
}

static void core_suspend(seL4_Bool power_down) {
    seL4_ARM_SMCContext args = {.x0 = PSCI_CPU_SUSPEND, .x1 = power_down << 16, .x2 = 0x80000000};
    seL4_ARM_SMCContext response = {0};
    
    microkit_arm_smc_call(&args, &response);
    
    print_error(response);
}
