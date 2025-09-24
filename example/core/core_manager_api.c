#include <stdint.h>
#include "core.h"

#define PD_INIT_ENTRY           0x200000
#define BOOTSTRAP_ENTRY         0x80000000

#define BASE_SCHED_CONTEXT_CAP  394
#define BASE_SCHED_CONTROL_CAP  458

#define MAX_PDS                 63
#define MAX_IRQS                64

static seL4_Word core_status(uint8_t core, seL4_Bool print);
static inline void core_migrate(uint8_t pd, uint8_t core);
static inline void monitor_migrate(uint8_t core);
static void core_on(uint8_t core, seL4_Word cpu_bootstrap);

void *bootstrap_vaddr;
Instruction *instruction_vaddr;
extern char bootstrap_start[];
extern char bootstrap_end[];

// Contains information necessary for core migration.
uint64_t pd_irqs[MAX_PDS];
uint64_t pd_budget[MAX_PDS];
uint64_t pd_period[MAX_PDS];

// The core the initial task (Monitor) is currently on
uint8_t monitor_core = 0;
// The number of cores that are on
uint8_t cores_on = NUM_CPUS;

static void *memcpy(void *dst, const void *src, uint64_t sz);

void init(void) {
    /* Copy the entire bootstrap section to the bootstrap memory region. */
    uint64_t bootstrap_size = (uintptr_t)bootstrap_end - (uintptr_t)bootstrap_start;
    memcpy(bootstrap_vaddr, bootstrap_start, bootstrap_size);
    asm volatile("dsb sy" ::: "memory");

    /* Migrate all worker PDs to relevant cores */
    core_migrate(2, 1);
    core_migrate(3, 2);
    core_migrate(4, 3);
}

void notified(microkit_channel ch) {
    microkit_dbg_puts("ERROR: Core Manager API should not receive notifications!\n");
    /* For now, don't acknowledge the notification and crash. */
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
            microkit_dbg_puts("Could not perform operation since only 1 core remains.");
            err = 1;
            break;
        } else if (core == monitor_core) {
            microkit_dbg_puts("Could not perform operation since the core being powered down contains the Monitor.");
            err = 1;
            break;
        }
    case CORE_DUMP:
        microkit_notify(core + 2);
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
    microkit_dbg_puts("[Core Manager API]: Received ");

    seL4_Word fault = microkit_msginfo_get_label(msginfo);

    switch (fault) {
    case seL4_Fault_NullFault:
        microkit_dbg_puts("seL4_Fault_NullFault");
        break;
    case seL4_Fault_CapFault:
        microkit_dbg_puts("seL4_Fault_CapFault");
        break;
    case seL4_Fault_UnknownSyscall:
        microkit_dbg_puts("seL4_Fault_UnknownSyscall");
        break;
    case seL4_Fault_UserException:
        microkit_dbg_puts("seL4_Fault_UserException");
        break;
    case seL4_Fault_Timeout:
        microkit_dbg_puts("seL4_Fault_Timeout");
        break;
    case seL4_Fault_VMFault:
        microkit_dbg_puts("seL4_Fault_VMFault");
        break;
    case seL4_Fault_VGICMaintenance:
        microkit_dbg_puts("seL4_Fault_VGICMaintenance");
        break;
    case seL4_Fault_VCPUFault:
        microkit_dbg_puts("seL4_Fault_VCPUFault");
        break;
    case seL4_Fault_VPPIEvent:
        microkit_dbg_puts("seL4_Fault_VPPIEvent");
        break;
    default:
        microkit_dbg_puts("unknown fault");
        break;
    }

    microkit_dbg_puts(" fault from child PD.\n");

    /* Don't reply to fault; crash. */
    return seL4_False;
}

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
        if (response.x0 == 0) {
            microkit_dbg_puts("Core ");
            microkit_dbg_putc('0' + core);
            microkit_dbg_puts(" is ON\n");
        } else if (response.x0 == 1) {
            microkit_dbg_puts("Core ");
            microkit_dbg_putc('0' + core);
            microkit_dbg_puts(" is OFF\n");
        } else if (response.x0 == 2) {
            microkit_dbg_puts("Core ");
            microkit_dbg_putc('0' + core);
            microkit_dbg_puts(" is PENDING\n");
        }
    }

    return response.x0;
}

static void *memcpy(void *dst, const void *src, uint64_t sz) {
    char *dst_ = dst;
    const char *src_ = src;
    while (sz-- > 0) {
        *dst_++ = *src_++;
    }

    return dst;
}
