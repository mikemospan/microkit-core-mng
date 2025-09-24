#include "core.h"
#include "uart.h"

// === Constants ===
#define PD_INIT_ENTRY           0x200000
#define BOOTSTRAP_ENTRY         0x80000000

#define BASE_SCHED_CONTEXT_CAP  394
#define BASE_SCHED_CONTROL_CAP  458

#define MAX_IRQS                64

// === External symbols ===
extern char bootstrap_start[];
extern char bootstrap_end[];

// === Globals ===
void *bootstrap_vaddr;
Instruction *instruction_vaddr;

// Contains information necessary for core migration
uint64_t pd_irqs[MAX_PDS];
uint64_t pd_budget[MAX_PDS];
uint64_t pd_period[MAX_PDS];

// The core the initial task (Monitor) is currently on
uint8_t monitor_core = 0;
// Number of cores currently on
uint8_t cores_on = NUM_CPUS;

// === Core operation prototypes ===
static inline void core_migrate(uint8_t pd, uint8_t core);
static inline void monitor_migrate(uint8_t core);
static void core_on(uint8_t core, seL4_Word cpu_bootstrap);
static seL4_Word core_status(uint8_t core, seL4_Bool print);

// === Microkit API functions ===
void init(void) {
    // Copy the entire bootstrap section to the bootstrap memory region.
    uint64_t bootstrap_size = (uintptr_t)bootstrap_end - (uintptr_t)bootstrap_start;
    memcpy(bootstrap_vaddr, bootstrap_start, bootstrap_size);
    asm volatile("dsb sy" ::: "memory");
}

void notified(microkit_channel ch) {
    uart_puts("ERROR: Core Manager API should not receive notifications!\n");
    // For now, don't acknowledge the notification and crash.
}

microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo) {
    instruction_vaddr[0] = microkit_mr_get(0);
    uint8_t core = microkit_mr_get(1);
    uint8_t pd = microkit_mr_get(2);
    int err = 0;

    switch (instruction_vaddr[0]) {
        case CORE_ON:
            core_on(core, BOOTSTRAP_ENTRY);
            microkit_pd_restart(core + 1, PD_INIT_ENTRY);
            break;
        case CORE_OFF:
        case CORE_POWERDOWN:
        case CORE_STANDBY:
            if (cores_on == 1) {
                uart_puts("Could not perform operation: only 1 core remains.\n");
                err = 1;
                break;
            } else if (core == monitor_core) {
                uart_puts("Cannot power down core containing the Monitor.\n");
                err = 1;
                break;
            }
            microkit_notify(core + 2);
            cores_on--;
            break;
        case CORE_MIGRATE:
            core_migrate(pd, core);
            for (int i = 0; i < MAX_IRQS; i++) {
                if (pd_irqs[pd] & (1ULL << i)) {
                    seL4_IRQHandler_SetCore(BASE_IRQ_CAP + i, core);
                }
            }
            break;
        case CORE_MIGRATE_MONITOR:
            monitor_core = core;
            monitor_migrate(monitor_core);
            break;
        case CORE_STATUS:
            core_status(core, 1);
            break;
        default:
            err = 1;
            break;
    }

    microkit_mr_set(0, err);
    return microkit_msginfo_new(0, 1);
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo) {
    uart_puts("[Core Manager API]: Received ");

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

    uart_puts(" fault from child PD.\n");
    return seL4_False; // don't reply to fault
}

// === Core operation helpers ===
static void core_on(uint8_t core, seL4_Word cpu_bootstrap) {
    seL4_ARM_SMCContext args = {.x0 = PSCI_CPU_ON, .x1 = core, .x2 = cpu_bootstrap};
    seL4_ARM_SMCContext response = {0};
    microkit_arm_smc_call(&args, &response);
    print_error(response);
}

static inline void core_migrate(uint8_t pd, uint8_t core) {
    seL4_SchedControl_ConfigureFlags(
        BASE_SCHED_CONTROL_CAP + core,
        BASE_SCHED_CONTEXT_CAP + pd,
        pd_period[pd],
        pd_budget[pd],
        0,
        0x100 + pd,
        0
    );
}

static inline void monitor_migrate(uint8_t core) {
    seL4_SchedControl_ConfigureFlags(
        BASE_SCHED_CONTROL_CAP + core, BASE_SCHED_CONTEXT_CAP + 63, 1000, 1000, 0, 0, 0
    );
}

static seL4_Word core_status(uint8_t core, seL4_Bool print) {
    seL4_ARM_SMCContext args = {.x0 = PSCI_AFFINITY_INFO, .x1 = core};
    seL4_ARM_SMCContext response = {0};
    microkit_arm_smc_call(&args, &response);

    int err = print_error(response);
    if (!err && print) {
        const char *status_str = (response.x0 == 0) ? "ON" :
                                 (response.x0 == 1) ? "OFF" : "PENDING";
        uart_puts("Core ");
        uart_put64(core);
        uart_puts(" is ");
        uart_puts(status_str);
        uart_putc('\n');
    }

    return response.x0;
}
