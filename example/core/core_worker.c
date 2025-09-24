#include "core.h"
#include "uart.h"

#define CORE_MANAGER_CHANNEL 1

// Function prototypes
static void core_off(void);
static void core_suspend(seL4_Bool power_down);

// Pointer to instruction received from Core Manager
Instruction *instruction_vaddr;

// === Microkit API functions ===
void init(void) {
    // No initialisation needed for the core worker
}

void notified(microkit_channel ch) {
    if (ch != CORE_MANAGER_CHANNEL) {
        uart_puts("[Core Worker]: Received unexpected notification: ");
        uart_put64(ch);
        uart_putc('\n');
        return;
    }

    switch (instruction_vaddr[0]) {
        case CORE_OFF:
            uart_puts("[Core Worker]: Turning off core.\n");
            core_off();
            break;
        case CORE_POWERDOWN:
            uart_puts("[Core Worker]: Powering down core.\n");
            core_suspend(1); // Power down flag set
            break;
        case CORE_STANDBY:
            uart_puts("[Core Worker]: Putting core in standby mode.\n");
            core_suspend(0); // Standby, no power down
            break;
        default:
            uart_puts("[Core Worker]: Encountered unexpected instruction.\n");
            break;
    }
}

// Power off the core via PSCI call
static void core_off(void) {
    seL4_ARM_SMCContext args = {.x0 = PSCI_CPU_OFF};
    seL4_ARM_SMCContext response = {0};

    microkit_arm_smc_call(&args, &response);
    print_error(response);
}

// Suspend the core (standby or power down) via PSCI call
static void core_suspend(seL4_Bool power_down) {
    // x1 encodes the power state: bit 16 = power down flag
    seL4_ARM_SMCContext args = {.x0 = PSCI_CPU_SUSPEND, .x1 = (power_down << 16), .x2 = 0x80000000};
    seL4_ARM_SMCContext response = {0};

    microkit_arm_smc_call(&args, &response);
    print_error(response);
}
