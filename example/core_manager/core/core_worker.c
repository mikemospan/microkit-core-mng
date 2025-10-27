#include "core.h"
#include "uart.h"
#include "profiler_config.h"
#include "profiler.h"

#define CORE_MANAGER_CHANNEL    1

#define ISB asm volatile("isb")
#define MRS(reg, v)  asm volatile("mrs %x0," reg : "=r"(v))
#define MSR(reg, v)                                     \
    do {                                                \
        uint64_t _v = v;                                \
        asm volatile("msr " reg ",%x0" ::  "r" (_v));   \
    } while(0)

// Function prototypes
static void core_off(void);
static void core_suspend(seL4_Bool power_down);
static void handle_instruction(Instruction instr);

static void setup_pmu(void);
static void init_pmu(void);
static void halt_pmu(void);
static void resume_pmu(void);
static void configure_clkcnt(uint64_t val);

// Pointer to instruction received from Core Manager
Instruction *instruction_vaddr;

// === Microkit API functions ===
void init(void) {
#if CONFIG_BENCHMARK
    setup_pmu();
#endif
}

void notified(microkit_channel ch) {
    if (ch == CORE_MANAGER_CHANNEL) {
        handle_instruction(*instruction_vaddr);
    } else {
        uart_puts("[Core Worker]: Received unexpected notification: ");
        uart_put64(ch);
        uart_puts("\n");
    }
    
    microkit_irq_ack(ch);
}

#if PRINTING
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo) {
    if (ch == CORE_MANAGER_CHANNEL) {
        uint64_t cycles;
        MRS(PMU_CYCLE_CTR, cycles);
        uart_puts("[Core Worker]: Cycles since last tick: ");
        uart_put64(cycles);
        uart_puts("\n");
        MSR(PMU_CYCLE_CTR, 0);
    } else {
        uart_puts("[Core Worker]: Received unexpected notification: ");
        uart_put64(ch);
        uart_puts("\n");
    }

    return microkit_msginfo_new(0, 0);
}
#endif

static void handle_instruction(Instruction instr) {
    switch (instr) {
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
            // The core manager api must be forwarding a timer IRQ to us
            uint64_t cycles;
            MRS(PMU_CYCLE_CTR, cycles);
            uart_puts("[Core Worker]: Cycles since last tick: ");
            uart_put64(cycles);
            uart_puts("\n");
            MSR(PMU_CYCLE_CTR, 0);
            break;
    }
}

// Power off the core via PSCI call
static void core_off(void) {
    seL4_ARM_SMCContext args = {.x0 = PSCI_CPU_OFF};
    seL4_ARM_SMCContext response;

    microkit_arm_smc_call(&args, &response);
    print_error(response);
}

// Suspend the core (standby or power down) via PSCI call
static void core_suspend(seL4_Bool power_down) {
    // x1 encodes the power state: bit 16 = power down flag
    seL4_ARM_SMCContext args = {.x0 = PSCI_CPU_SUSPEND, .x1 = power_down << 16, .x2 = bootstrap_entry};
    seL4_ARM_SMCContext response;

    microkit_arm_smc_call(&args, &response);
    print_error(response);
}

/* Set up the PMU for cycle counting */
static void setup_pmu(void) {
    halt_pmu();
    init_pmu();
    MSR(PMU_CYCLE_CTR, 0);
    resume_pmu();
}

/* Halt the PMU */
static void halt_pmu(void) {
    uint32_t value = 0;
    uint32_t mask = 0;

    /* Disable Performance Counter */
    MRS(PMCR_EL0, value);
    mask = 0;
    mask |= (1 << 0);
    MSR(PMCR_EL0, (value & ~mask));

    /* Disable cycle counter register */
    MRS(PMCNTENSET_EL0, value);
    mask = 0;
    mask |= (1 << 31);
    MSR(PMCNTENSET_EL0, (value & ~mask));
    ISB;
}

static void init_pmu(void) {
    uint32_t value;
    MRS(PMCCFILTR_EL0, value);
    // Set the NSH Bit. Enables counting in EL2.
    value |= (1 << 27);
    MSR(PMCCFILTR_EL0, value);
}

/* Configure cycle counter*/
static void configure_clkcnt(uint64_t val) {
    uint64_t init_cnt = 0xffffffffffffffff - val;
    MSR(PMU_CYCLE_CTR, init_cnt);
}

/* Resume the PMU */
static void resume_pmu(void) {
    uint64_t val;
    MRS(PMCR_EL0, val);
    val |= BIT(0);
    ISB;
    MSR(PMCR_EL0, val);
    MSR(PMCNTENSET_EL0, (BIT(31)));
    ISB;
}
