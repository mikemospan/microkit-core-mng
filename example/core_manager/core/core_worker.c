#include <stdatomic.h>
#include "core.h"
#include "uart.h"
#include "profiler_config.h"
#include "profiler.h"

// ============================================================================
// Constants
// ============================================================================

#define CORE_MANAGER_CHANNEL    1

// ============================================================================
// ARM Assembly Macros
// ============================================================================

#define ISB              asm volatile("isb")
#define MRS(reg, v)      asm volatile("mrs %x0," reg : "=r"(v))
#define MSR(reg, v)      do {                                        \
                             uint64_t _v = v;                        \
                             asm volatile("msr " reg ",%x0" :: "r"(_v)); \
                         } while(0)

// ============================================================================
// Global Variables
// ============================================================================

/* Shared instruction memory from Core Manager. */
Instruction *instruction_vaddr;
/* Physical entry point for bootstrapping code. */
uintptr_t bootstrap_entry;
/* Status of every core (ON, OFF, POWERDOWN, STANDBY). */
_Atomic uint8_t *cores_status;
/* The core this worker is in charge of. */
uint8_t core;

// ============================================================================
// Function Prototypes
// ============================================================================

// Core power management
static void core_off(void);
static void core_suspend(seL4_Bool power_down);
static void handle_instruction(Instruction instr);

// PMU (Performance Monitoring Unit) management
static void setup_pmu(void);
static void init_pmu(void);
static void halt_pmu(void);
static void resume_pmu(void);
static void reset_cycle_counter(void);

// ============================================================================
// Microkit API Implementation
// ============================================================================

/**
 * Initialise the core worker.
 * Sets up the PMU for cycle counting if benchmarking is enabled.
 */
void init(void) {
#if CONFIG_BENCHMARK
    setup_pmu();
#endif
}

/**
 * Handle notifications from the Core Manager.
 * Processes power management commands or timer interrupts for benchmarking.
 */
void notified(microkit_channel ch) {
    if (ch != CORE_MANAGER_CHANNEL) {
        uart_puts("[Core Worker]: Received unexpected notification: ");
        uart_put64(ch);
        uart_puts("\n");
        return;
    }

    handle_instruction(*instruction_vaddr);
}

/**
 * Handle protected procedure calls from the Core Manager.
 * Used for synchronous cycle counter reporting.
 */
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo) {
    if (ch != CORE_MANAGER_CHANNEL) {
        uart_puts("[Core Worker]: Received unexpected PPC from channel: ");
        uart_put64(ch);
        uart_puts("\n");

        microkit_mr_set(0, -1); // error
    } else {
        // Read cycle counter
        uint64_t cycles;
        MRS(PMU_CYCLE_CTR, cycles);
        microkit_mr_set(0, cycles);
        
        // Reset counter for next measurement period
        reset_cycle_counter();
    }

    return microkit_msginfo_new(0, 1);
}

// ============================================================================
// Instruction Handling
// ============================================================================

/**
 * Execute the instruction received from the Core Manager.
 * Handles core power management commands and timer-based benchmarking.
 */
static void handle_instruction(Instruction instr) {
    switch (instr) {
        case CORE_OFF:
            core_off();
            break;

        case CORE_POWERDOWN:
            core_suspend(1);  // Power down mode
            break;

        case CORE_STANDBY:
            core_suspend(0);  // Standby mode (retain state)
            break;

        default:
            uart_puts("[Core Worker]: Received unexpected instruction.\n");
            break;
    }
}

// ============================================================================
// Core Power Management (PSCI Interface)
// ============================================================================

/**
 * Power off this core via PSCI.
 * This is a non-returning call - the core will be powered down.
 */
static void core_off(void) {
    /* Mark the core as offline and decrement the number of cores counter. */
    atomic_store(cores_status + 1 + core, CORE_OFF);
    atomic_fetch_sub(cores_status, 1);

    seL4_ARM_SMCContext args = {.x0 = PSCI_CPU_OFF};
    seL4_ARM_SMCContext response;

    microkit_arm_smc_call(&args, &response);
    print_error(response);
}

/**
 * Suspend this core via PSCI.
 * @param power_down If true, power down the core; if false, enter standby mode
 * 
 * In standby mode, the core retains state and can resume quickly.
 * In power down mode, the core loses state and resumes via bootstrap_entry.
 */
static void core_suspend(seL4_Bool power_down) {
    /* Bit 16 describes power level. See Arm PSCI handbook. */
    seL4_Word power_state = power_down << 16;

    /*
     * Platforms may require their own state ID encoding. As far as I can tell,
     * the only way to get this state ID is by looking at the `validate_power_state`
     * platform specific implementations in the Arm Trusted Firmware source code.
     */
#if defined(CONFIG_PLAT_QEMU_ARM_VIRT)
    power_state |= (1 << power_down);
#elif defined(CONFIG_PLAT_MAAXBOARD)
    power_state |= 0x33;
#elif defined(CONFIG_PLAT_ZYNQMP)
    /* Nothing to do here, state ID is expected to be 0. */
#endif

    /* Mark the core as offline and decrement the number of cores counter. */
    atomic_store(cores_status + 1 + core, power_down ? CORE_POWERDOWN : CORE_STANDBY);
    atomic_fetch_sub(cores_status, 1);

    /*
     * PSCI CPU Suspend SMC (x0):
     *  x1: Power state encoding
     *  x2: Entry point address for resume (used in powerdown mode)
     */
    seL4_ARM_SMCContext args = {
        .x0 = PSCI_CPU_SUSPEND,
        .x1 = power_state,
        .x2 = bootstrap_entry
    };
    seL4_ARM_SMCContext response;

    uart_puts("Suspending core...\n");
    microkit_arm_smc_call(&args, &response);
    uart_puts("Core resumed.\n");

    seL4_Error err = print_error(response);
    if (!err && power_down) {
        uart_puts("BUG: We don't expect to get to this point...\n");
    }

    /* Mark the core as online and increment the number of cores counter. */
    atomic_store(cores_status + 1 + core, CORE_ON);
    atomic_fetch_add(cores_status, 1);
}

// ============================================================================
// Performance Monitoring Unit (PMU) Management
// ============================================================================

/**
 * Set up the PMU for cycle counting.
 * Initialises and enables the cycle counter for performance measurements.
 */
static void setup_pmu(void) {
    halt_pmu();              // Disable PMU
    init_pmu();              // Configure PMU settings
    reset_cycle_counter();   // Clear cycle counter
    resume_pmu();            // Enable PMU
}

/**
 * Halt the PMU by disabling the cycle counter.
 * This stops cycle counting on this core.
 */
static void halt_pmu(void) {
    uint32_t value;

    // Disable the performance counter (PMCR_EL0.E = 0)
    MRS(PMCR_EL0, value);
    value &= ~(1 << 0);
    MSR(PMCR_EL0, value);

    // Disable the cycle counter (PMCNTENSET_EL0.C = 0)
    MRS(PMCNTENSET_EL0, value);
    value &= ~(1 << 31);
    MSR(PMCNTENSET_EL0, value);
    
    ISB;
}

/**
 * Initialise PMU configuration.
 * Enables cycle counting in EL2 (hypervisor mode).
 */
static void init_pmu(void) {
    uint32_t value;
    
    // Configure cycle counter filter (PMCCFILTR_EL0)
    MRS(PMCCFILTR_EL0, value);
    value |= (1 << 27);  // Set NSH bit to enable counting in EL2
    MSR(PMCCFILTR_EL0, value);
}

/**
 * Reset the cycle counter to zero.
 */
static void reset_cycle_counter(void) {
    MSR(PMU_CYCLE_CTR, 0);
}

/**
 * Resume the PMU by enabling the cycle counter.
 * Starts counting CPU cycles on this core.
 */
static void resume_pmu(void) {
    uint64_t value;

    // Enable the performance counter (PMCR_EL0.E = 1)
    MRS(PMCR_EL0, value);
    value |= (1 << 0);
    ISB;
    MSR(PMCR_EL0, value);

    // Enable the cycle counter (PMCNTENSET_EL0.C = 1)
    MSR(PMCNTENSET_EL0, (1 << 31));
    ISB;
}
