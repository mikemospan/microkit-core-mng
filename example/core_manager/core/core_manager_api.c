#include "core.h"
#include "uart.h"

// ============================================================================
// Constants
// ============================================================================

#define PD_INIT_ENTRY           0x200000

#define BASE_SCHED_CONTEXT_CAP  394
#define BASE_SCHED_CONTROL_CAP  458
#define MAX_IRQS                64

// Timer configuration
#define TIMER_IRQ_CHANNEL       6
#define GPT_FREQ                12u  // 12 MHz peripheral clock

// GPT register offsets (indexed as uint32_t array)
#define GPT_CR                  0    // Control Register
#define GPT_PR                  1    // Prescaler Register
#define GPT_SR                  2    // Status Register
#define GPT_IR                  3    // Interrupt Register
#define GPT_OCR1                4    // Output Compare Register 1
#define GPT_OCR2                5    // Output Compare Register 2
#define GPT_OCR3                6    // Output Compare Register 3
#define GPT_ICR1                7    // Input Capture Register 1
#define GPT_ICR2                8    // Input Capture Register 2
#define GPT_CNT                 9    // Counter Register

#define GPT_STATUS_REG_CLEAR    0x3F // Clear all status bits

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
uint64_t pd_irqs[MAX_PDS];    // Bitmask of IRQs assigned to each PD
uint64_t pd_budget[MAX_PDS];  // Scheduling budgets (in microseconds)
uint64_t pd_period[MAX_PDS];  // Scheduling periods (in microseconds)

// Core management state
uint8_t monitor_core = 0;     // Core currently running the Monitor PD
uint8_t cores_on = NUM_CPUS;  // Number of cores currently powered on

// Memory-mapped GPT registers
volatile uint32_t *gpt_base_vaddr;

// ============================================================================
// Function Prototypes
// ============================================================================

// Timer handling
static void setup_timer(void);
static void handle_timer_irq(void);

// Core operations
static inline void core_migrate(uint8_t pd, uint8_t core);
static inline void monitor_migrate(uint8_t core);
static void core_on(uint8_t core, seL4_Word cpu_bootstrap);
static seL4_Word core_status(uint8_t core, seL4_Bool print);
static uint32_t psci_version(void);

// ============================================================================
// Microkit API Implementation
// ============================================================================

/**
 * Initialise the core manager.
 * - Copies bootstrap code to memory region accessible by secondary cores
 * - Flushes caches to ensure memory coherency
 * - Sets up timer if the microkit is built in benchmark mode
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

#if CONFIG_BENCHMARK
    setup_timer();
#endif

#if PRINTING
    // Print PSCI version information
    uint32_t ver = psci_version();
    uint32_t major = (ver >> 16) & 0xFFFF;
    uint32_t minor = ver & 0xFFFF;

    uart_puts("Using PSCI v");
    uart_put64(major);
    uart_puts(".");
    uart_put64(minor);
    uart_puts(".\n");
#endif
}

/**
 * Handle notifications from other PDs or IRQs.
 * Currently only handles timer interrupts for PMU updating.
 */
void notified(microkit_channel ch) {
#if CONFIG_BENCHMARK
    if (ch == TIMER_IRQ_CHANNEL) {
        handle_timer_irq();
        microkit_irq_ack(ch);
        return;
    }
#endif
    uart_puts("[Core Manager]: Received unexpected notification.\n");
}

/**
 * Handle protected procedure calls from other PDs.
 * Provides core management operations:
 * - CORE_ON: Power on a secondary core
 * - CORE_OFF/POWERDOWN/STANDBY: Power down a core
 * - CORE_MIGRATE: Migrate a PD to a different core
 * - CORE_MIGRATE_MONITOR: Migrate the Monitor PD to a different core
 * - CORE_STATUS: Query core power state
 */
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo) {
    // Read command and parameters from message registers
    instruction_vaddr[0] = microkit_mr_get(0);
    uint8_t core = microkit_mr_get(1);
    uint8_t pd = microkit_mr_get(2);
    int err = 0;

    switch (instruction_vaddr[0]) {
        case CORE_ON:
            // Power on the specified core and restart its worker PD
            core_on(core, bootstrap_entry);
            microkit_pd_restart(core + 1, PD_INIT_ENTRY);
            break;

        case CORE_OFF:
        case CORE_POWERDOWN:
        case CORE_STANDBY:
            // Validate that we can power down this core
            if (cores_on == 1) {
                uart_puts("Cannot power down: only 1 core remains.\n");
                err = 1;
                break;
            } else if (core == monitor_core) {
                uart_puts("Cannot power down core containing the Monitor.\n");
                err = 1;
                break;
            }
            // Notify the core to shut down
            microkit_notify(core + 2);
            cores_on--;
            break;

        case CORE_MIGRATE:
            core_migrate(pd, core);
            break;

        case CORE_MIGRATE_MONITOR:
            // Migrate the Monitor PD to the specified core
            monitor_core = core;
            monitor_migrate(monitor_core);
            break;

        case CORE_STATUS:
            // Query and print the power state of the specified core
            core_status(core, 1);
            break;

        default:
            err = 1;
            break;
    }

    // Return error status to caller
    microkit_mr_set(0, err);
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
// Timer Management
// ============================================================================

/**
 * Handle timer interrupt.
 * Notifies all core worker PDs to collect performance statistics.
 */
static void handle_timer_irq(void) {
    uint32_t sr = gpt_base_vaddr[GPT_SR];
    gpt_base_vaddr[GPT_SR] = sr;  // Clear interrupt status bits

    if (sr & (1 << 0)) {  // Compare1 interrupt fired
#if PRINTING
        /*
         * Use blocking PPCs when printing is enabled to ensure
         * synchronous access to UART registers.
         */
        microkit_ppcall(2, microkit_msginfo_new(0, 0));
        microkit_ppcall(3, microkit_msginfo_new(0, 0));
        microkit_ppcall(4, microkit_msginfo_new(0, 0));
        microkit_ppcall(5, microkit_msginfo_new(0, 0));
#else
        /*
         * Use non-blocking notifications when printing is disabled.
         * Set instruction to 0 so core workers enter default case.
         */
        instruction_vaddr[0] = 0;
        microkit_notify(2);
        microkit_notify(3);
        microkit_notify(4);
        microkit_notify(5);
#endif
    }
}

/**
 * Configure and start the GPT timer for 1-second periodic interrupts.
 */
static void setup_timer(void) {
    // Disable GPT and clear any pending interrupts
    gpt_base_vaddr[GPT_CR] = 0;
    gpt_base_vaddr[GPT_SR] = GPT_STATUS_REG_CLEAR;

    // Perform software reset
    gpt_base_vaddr[GPT_CR] = (1 << 15);
    while (gpt_base_vaddr[GPT_CR] & (1 << 15));  // Wait for reset to complete

    // Configure prescaler (no division)
    gpt_base_vaddr[GPT_PR] = 0;
    
    // Set compare value for 1-second period at 12 MHz
    gpt_base_vaddr[GPT_OCR1] = 1 * 1000 * 1000 * GPT_FREQ;

    /*
     * Configure Control Register:
     *  [15] SWR    = 0 (software reset complete)
     *  [9]  FRR    = 0 (restart mode - auto-reset on compare)
     *  [6]  CLKSRC = 1 (use peripheral clock)
     *  [0]  EN     = 1 (enable timer)
     */
    gpt_base_vaddr[GPT_CR] =
        (0 << 9) |  // Restart mode
        (1 << 6) |  // Peripheral clock source
        (1 << 0);   // Enable

    // Enable only Compare1 interrupt
    gpt_base_vaddr[GPT_IR] = (1 << 0);
}

// ============================================================================
// Core Operations (PSCI Interface)
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
 */
static inline void core_migrate(uint8_t pd, uint8_t core) {
    seL4_SchedControl_ConfigureFlags(
        BASE_SCHED_CONTROL_CAP + core,  // Target core's scheduling control
        BASE_SCHED_CONTEXT_CAP + pd,    // PD's scheduling context
        pd_period[pd],                  // Scheduling period
        pd_budget[pd],                  // Scheduling budget
        0,                              // Extra refills
        0x100 + pd,                     // Badge
        0                               // Flags
    );

    for (int i = 0; i < MAX_IRQS; i++) {
        if (pd_irqs[pd] & (1ULL << i)) {
            seL4_IRQHandler_SetCore(BASE_IRQ_CAP + i, core);
        }
    }
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
 * Query the power state of a core using PSCI.
 * @param core Core number to query
 * @param print Whether to print the status
 * @return Core status (0=ON, 1=OFF, 2=PENDING)
 */
static seL4_Word core_status(uint8_t core, seL4_Bool print) {
    seL4_ARM_SMCContext args = {
        .x0 = PSCI_AFFINITY_INFO,
        .x1 = core
    };
    seL4_ARM_SMCContext response;
    
    microkit_arm_smc_call(&args, &response);

    int err = print_error(response);
    if (!err && print) {
        const char *status_str = (response.x0 == 0) ? "ON" :
                                 (response.x0 == 1) ? "OFF" : "PENDING";
        uart_puts("Core ");
        uart_put64(core);
        uart_puts(" is ");
        uart_puts(status_str);
        uart_puts("\n");
    }

    return response.x0;
}

/**
 * Query the PSCI version from firmware.
 * @return PSCI version (upper 16 bits = major, lower 16 bits = minor)
 */
static uint32_t psci_version(void) {
    seL4_ARM_SMCContext args = {.x0 = PSCI_VERSION_FID};
    seL4_ARM_SMCContext response;
    
    microkit_arm_smc_call(&args, &response);
    print_error(response);

    return response.x0;
}
