#include "core.h"
#include "uart.h"

// ============================================================================
// Constants
// ============================================================================

#define PD_INIT_ENTRY           0x200000
#define PD_INIT_SP              0x10000000000

#define BASE_SCHED_CONTEXT_CAP  394
#define BASE_SCHED_CONTROL_CAP  458
#define MAX_IRQS                64

// ============================================================================
// External Symbols
// ============================================================================

extern char bootstrap_start[];
extern char bootstrap_end[];

// ============================================================================
// Global Variables
// ============================================================================

// Bootstrap memory region for secondary core initialization
void *bootstrap_vaddr;

// Shared instruction memory for inter-PD communication
Instruction *instruction_vaddr;

// Per-PD scheduling and IRQ configuration
typedef struct {
    uint64_t pd_core;       // The CPU core the PD resides on
    uint64_t pd_irqs;       // Bitmask of IRQs assigned to each PD
    uint64_t pd_budget;     // Scheduling budgets (in microseconds)
    uint64_t pd_period;     // Scheduling periods (in microseconds)
} pd_info;

pd_info pd_infos[MAX_PDS];

// Core management state
uint8_t monitor_core = 0;     // Core currently running the Monitor PD

/* Physical entry point for bootstrapping code. */
uintptr_t bootstrap_entry;

// ============================================================================
// Function Prototypes
// ============================================================================

// Core operations
static inline seL4_Error core_migrate(uint8_t pd, uint8_t core);
static inline void monitor_migrate(uint8_t core);
static void core_on(uint8_t core, seL4_Word cpu_bootstrap);
static inline seL4_Word core_status(uint8_t core);
static microkit_msginfo cores_query(void);
static void cores_restart_pmu(void);
static void manager_restart_pd(microkit_child pd, seL4_Word entry_point, seL4_Word new_sp);

// ============================================================================
// Microkit API Implementation
// ============================================================================

/**
 * Initialise the core manager.
 * - Copies bootstrap code to memory region accessible by secondary cores
 * - Flushes caches to ensure memory coherency
 */
void init(void) {
    // Copy bootstrap code to shared memory region
    uint64_t bootstrap_size = (uintptr_t)bootstrap_end - (uintptr_t)bootstrap_start;
    memcpy(bootstrap_vaddr, bootstrap_start, bootstrap_size);

    /*
     * Flush data cache to ensure memory coherency across cores.
     * We use CleanInvalidate to ensure that if the bootstrap memory is already
     * mapped in secondary cores' page tables, their instruction caches will
     * also be invalidated.
     */
    uintptr_t start = (uintptr_t)bootstrap_vaddr;
    uintptr_t end = start + bootstrap_size;
    for (uintptr_t addr = start; addr < end; addr += (1 << seL4_PageBits)) {
        uintptr_t page_end = addr + (1 << seL4_PageBits);
        seL4_ARM_VSpace_CleanInvalidate_Data(3, addr, page_end);
    }
    
    // Memory barrier to ensure cache operations complete
    asm volatile("dsb ish");
}

/**
 * Handle notifications from other PDs or IRQs.
 * Currently only handles timer interrupts for PMU updating.
 */
void notified(microkit_channel ch) {
    uart_puts("[Core Manager]: Received unexpected notification.\n");
}

/**
 * Handle protected calls from other PDs:
 * - CORE_ON: Power on a core (PSCI_CPU_ON; secondary boots and joins scheduler)
 * - CORE_OFF/POWERDOWN/STANDBY: Request core enter low-power; validate and notify worker
 * - CORE_MIGRATE: Move a PD to the target core (set affinity)
 * - CORE_MIGRATE_MONITOR: Move the Monitor PD to the target core
 * - CORE_STATUS: Return the target core’s power state
 * - CORE_RESTART_PDS: Restart all PDs on the core via manager_restart_pd to avoid post-CPU_ON race
 * - CORES_QUERY: Return a summary of core states
 * - CORES_RESTART_PMU: Reinitialise PMU on all cores
 */
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo) {
    // Read command and parameters from message registers
    instruction_vaddr[0] = microkit_mr_get(0);
    uint8_t core = microkit_mr_get(1);
    uint8_t pd = microkit_mr_get(2);
    seL4_Word ret = 0;

    switch (instruction_vaddr[0]) {
        case CORE_ON:
            core_on(core, bootstrap_entry);
            break;

        case CORE_OFF:
        case CORE_POWERDOWN:
        case CORE_STANDBY:
            if (core == monitor_core) {
                uart_puts("Cannot power down core containing the Monitor.\n");
                ret = 1;
                break;
            }

            // Notify the core to shut down
            seL4_Bool online = core_status(core);
            if (online) {
                microkit_notify(core + 2);
            } else {
                uart_puts("The core you are putting into a lower power state, is not on\n");
                ret = 1;
            }
            break;

        case CORE_MIGRATE:
            ret = core_migrate(pd, core);
            break;

        case CORE_MIGRATE_MONITOR:
            // Migrate the Monitor PD to the specified core
            monitor_core = core;
            monitor_migrate(monitor_core);
            break;

        case CORE_STATUS:
            // Query the power state of the specified core
            ret = core_status(core);
            break;

        case CORE_RESTART_PDS:
            // Restart all PDs on the specified core
            for (int pd_id = 0; pd_id < MAX_PDS; pd_id++) {
                if (pd_infos[pd_id].pd_core == core) {
                    manager_restart_pd(pd_id, PD_INIT_ENTRY, PD_INIT_SP);
                }
            }
            break;

        case CORES_QUERY:
            return cores_query();

        case CORES_RESTART_PMU:
            cores_restart_pmu();
            break;

        default:
            ret = 1;
            break;
    }

    microkit_mr_set(0, ret);
    return microkit_msginfo_new(0, 1);
}

/**
 * Handle faults from child PDs.
 * Logs the fault type and faulting PD, but does not reply.
 */
seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo) {
    uart_puts("[Core Manager]: Received ");

    seL4_Word fault = microkit_msginfo_get_label(msginfo);
    switch (fault) {
        case seL4_Fault_NullFault:          uart_puts("seL4_Fault_NullFault"); break;
        case seL4_Fault_CapFault:           uart_puts("seL4_Fault_CapFault"); break;
        case seL4_Fault_UnknownSyscall:     uart_puts("seL4_Fault_UnknownSyscall"); break;
        case seL4_Fault_UserException:      uart_puts("seL4_Fault_UserException"); break;
        case seL4_Fault_Timeout:            uart_puts("seL4_Fault_Timeout"); break;
        case seL4_Fault_VMFault:            uart_puts("seL4_Fault_VMFault"); break;
        case seL4_Fault_VGICMaintenance:    uart_puts("seL4_Fault_VGICMaintenance"); break;
        case seL4_Fault_VCPUFault:          uart_puts("seL4_Fault_VCPUFault"); break;
        case seL4_Fault_VPPIEvent:          uart_puts("seL4_Fault_VPPIEvent"); break;
        default:                            uart_puts("unknown fault"); break;
    }

    uart_puts(" fault from child PD: ");
    uart_put64(child);
    uart_puts(".\n");
    
    return seL4_False;  // Do not reply to fault
}

// ============================================================================
// Core Operations
// ============================================================================

/**
 * Power on a secondary core using PSCI.
 * @param core Core number to power on
 * @param cpu_bootstrap Entry point address for the core
 */
static void core_on(uint8_t core, seL4_Word cpu_bootstrap) {
    seL4_ARM_SMCContext args = {
        .x0 = PSCI_CPU_ON,
        .x1 = core,
        .x2 = cpu_bootstrap
    };
    seL4_ARM_SMCContext response = {0};
    
    microkit_arm_smc_call(&args, &response);
    print_error(response);
}

/**
 * Migrate a PD's scheduling context and IRQs to a different core.
 * @param pd Protection domain to migrate
 * @param core Target core number
 * @return Success of failure. This function cannot migrate core worker PDs.
 */
static inline seL4_Error core_migrate(uint8_t pd, uint8_t core) {
    if (pd >= 1 && pd <= NUM_CPUS) {
        return 1;
    }

    seL4_SchedControl_ConfigureFlags(
        BASE_SCHED_CONTROL_CAP + core,  // Target core's scheduling control
        BASE_SCHED_CONTEXT_CAP + pd,    // PD's scheduling context
        pd_infos[pd].pd_period,         // Scheduling period
        pd_infos[pd].pd_budget,         // Scheduling budget
        0,                              // Extra refills
        0x100 + pd,                     // Badge
        0                               // Flags
    );

    for (int i = 0; i < MAX_IRQS; i++) {
        if (pd_infos[pd].pd_irqs & (1ULL << i)) {
            seL4_IRQHandler_SetCore(BASE_IRQ_CAP + i, core);
        }
    }

    pd_infos[pd].pd_core = core;

    return 0;
}

/**
 * Migrate the Monitor PD to a different core.
 * The Monitor uses scheduling context 63 with fixed period/budget of 1000us.
 * @param core Target core number
 */
static inline void monitor_migrate(uint8_t core) {
    seL4_SchedControl_ConfigureFlags(
        BASE_SCHED_CONTROL_CAP + core,  // Target core's scheduling control
        BASE_SCHED_CONTEXT_CAP + 63,    // Monitor's scheduling context
        1000,                           // Period: 1000 us
        1000,                           // Budget: 1000 us
        0,                              // Extra refills
        0,                              // Badge
        0                               // Flags
    );
}

/**
 * Query the power state of a core.
 * @param core Core number to query
 * @return 0 if offline, 1 if online
 */
static inline seL4_Word core_status(uint8_t core) {
    seL4_SchedControl_CoreStatus_t res = seL4_SchedControl_CoreStatus(BASE_SCHED_CONTROL_CAP + core);
    return res.status;
}

/**
 * Query the core workers for the cycle counts on each core.
 * @return The message info after setting each message register to
 * a corresponding core's cycle count.
 */
static microkit_msginfo cores_query(void) {
    uint64_t core_cycles[NUM_CPUS];
    for (uint8_t i = 0; i < NUM_CPUS; i++) {
        seL4_Word online = core_status(i);
        if (online) {
            microkit_ppcall(i + 2, microkit_msginfo_new(0, 0));
            core_cycles[i] = microkit_mr_get(0);
        }
    }

    for (uint8_t i = 0; i < NUM_CPUS; i++) {
        microkit_mr_set(i, core_cycles[i]);
    }

    return microkit_msginfo_new(0, NUM_CPUS);
}

/**
 * Query the core workers to restart the cycle counts on each core.
 */
static void cores_restart_pmu(void) {
    for (uint8_t i = 0; i < NUM_CPUS; i++) {
        microkit_ppcall(i + 2, microkit_msginfo_new(0, 0));
    }
}

/**
 * Restart a protection domain on its assigned core.
 * 
 * Note: We cannot use microkit_pd_restart here because of a race.
 * After CPU_ON the secondary core may join the scheduler and run the PD
 * before we rewrite its context. Instead we:
 *   (1) Suspend the PD’s TCB,
 *   (2) write a full context (PC + SP) with resume=true,
 * 
 * @param pd Protection domain to restart
 * @param entry_point New entry point for the PD
 * @param new_sp New stack pointer for the PD
 */
static void manager_restart_pd(microkit_child pd, seL4_Word entry_point, seL4_Word new_sp) {
    seL4_CPtr tcb = BASE_TCB_CAP + pd;

    seL4_TCB_Suspend(tcb);

    seL4_UserContext c = {0};
    c.sp  = new_sp;
    c.pc  = entry_point;

    seL4_Error err = seL4_TCB_WriteRegisters(tcb,
                                             seL4_True,
                                             0, /* No flags */
                                             sizeof c / sizeof(seL4_Word),
                                             &c);
    if (err != seL4_NoError) {
        microkit_dbg_puts("microkit_pd_restart: WriteRegisters failed\n");
        microkit_internal_crash(err);
    }
}
