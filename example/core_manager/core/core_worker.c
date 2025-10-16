#include "core.h"
#include "uart.h"
#include "profiler_config.h"
#include "profiler.h"

#define CORE_MANAGER_CHANNEL    1
#define PMU_IRQ_CHANNEL         2

#define ISB asm volatile("isb")
#define MRS(reg, v)  asm volatile("mrs %x0," reg : "=r"(v))
#define MSR(reg, v)                                \
    do {                                           \
        uint64_t _v = v;                             \
        asm volatile("msr " reg ",%x0" ::  "r" (_v));\
    } while(0)

// Function prototypes
static void core_off(void);
static void core_suspend(seL4_Bool power_down);

void init_pmu_regs(void);
void halt_pmu(void);
void resume_pmu(void);
void reset_pmu(void);
void configure_clkcnt(uint64_t val, bool sampling);

// Pointer to instruction received from Core Manager
Instruction *instruction_vaddr;
// === External symbols ===
void *bootstrap_vaddr;
extern char bootstrap_start[];
extern char bootstrap_end[];
// Array of pmu registers available
pmu_reg_t pmu_registers[PMU_NUM_REGS];

// === Microkit API functions ===
void init(void) {
#if CONFIG_BENCHMARK
    halt_pmu(); // Ensure that the PMU is not running
    init_pmu_regs(); // Initialise the PMU register array

    /* INITIALISE WHAT COUNTERS WE WANT TO TRACK IN HERE */
    configure_clkcnt(CYCLE_COUNTER_PERIOD, 1);

    // Start the PMU
    resume_pmu();
#endif
}

void notified(microkit_channel ch) {
    if (ch != CORE_MANAGER_CHANNEL) {
        uart_puts("[Core Worker]: Received unexpected notification: ");
        uart_put64(ch);
        uart_puts("\n");
        return;
    } else if (ch == PMU_IRQ_CHANNEL) {
#if CONFIG_BENCHMARK
        uart_puts("[Core Worker]: Received PMU interrupt.\n");
#endif
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
    
    microkit_irq_ack(ch);
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

/* Halt the PMU */
void halt_pmu(void) {
    uint32_t value = 0;
    uint32_t mask = 0;

    /* Disable Performance Counter */
    MRS(PMCR_EL0, value);
    mask = 0;
    mask |= (1 << 0); /* Enable */
    mask |= (1 << 1); /* Cycle counter reset */
    mask |= (1 << 2); /* Reset all counters */
    MSR(PMCR_EL0, (value & ~mask));

    /* Disable cycle counter register */
    MRS(PMCNTENSET_EL0, value);
    mask = 0;
    mask |= (1 << 31);
    MSR(PMCNTENSET_EL0, (value & ~mask));
}

void init_pmu_regs(void) {
    /* Initialise the register array */
    pmu_registers[CYCLE_CTR].count = 0;
    pmu_registers[CYCLE_CTR].event = 0;
    pmu_registers[CYCLE_CTR].sampling = 0;
    pmu_registers[CYCLE_CTR].overflowed = 0;
}

/* Configure cycle counter*/
void configure_clkcnt(uint64_t val, bool sampling) {
    // Update the struct
    pmu_registers[CYCLE_CTR].count = val;
    pmu_registers[CYCLE_CTR].sampling = sampling;

    uint64_t init_cnt = 0xffffffffffffffff - pmu_registers[CYCLE_CTR].count;
    MSR(PMU_CYCLE_CTR, init_cnt);
}

/* Resume the PMU */
void resume_pmu(void) {
    uint64_t val;

    MRS(PMCR_EL0, val);

    val |= BIT(0);

    ISB;
    MSR(PMCR_EL0, val);

    MSR(PMCNTENSET_EL0, (BIT(31)));
}

void reset_pmu(void) {
    // Loop through the pmu registers, if the overflown flag has been set,
    // and we are sampling on this register, reset to max value - count.
    // Otherwise, reset to 0.
    for (int i = 0; i < PMU_NUM_REGS - 1; i++) {
        if (pmu_registers[i].overflowed == 1 && pmu_registers[i].sampling == 1) {
            pmu_registers[i].config_ctr(pmu_registers[i].event, pmu_registers[i].count);
            pmu_registers[i].overflowed = 0;
        } else if (pmu_registers[i].overflowed == 1) {
            pmu_registers[i].config_ctr(pmu_registers[i].event, 0xffffffff);
            pmu_registers[i].overflowed = 0;
        }
    }

    // Handle the cycle counter.
    if (pmu_registers[CYCLE_CTR].overflowed == 1) {
        uint64_t init_cnt = 0;
        if (pmu_registers[CYCLE_CTR].sampling == 1) {
            init_cnt = 0xffffffffffffffff - CYCLE_COUNTER_PERIOD;
        }
        MSR(PMU_CYCLE_CTR, init_cnt);
        pmu_registers[CYCLE_CTR].overflowed = 0;
    }
}
