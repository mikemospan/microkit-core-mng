#include <stdint.h>
#include "core.h"

#define PD_INIT_ENTRY           0x200000
#define BOOTSTRAP_ENTRY         0x80000000

#define BASE_SCHED_CONTEXT_CAP  394
#define BASE_SCHED_CONTROL_CAP  458

#define MAX_PDS                 63
#define MAX_IRQS                64

static void core_status(uint8_t core);
static void core_migrate(uint8_t pd, uint8_t core);
static void core_on(uint8_t core, seL4_Word cpu_bootstrap);

void *bootstrap_vaddr;
Instruction *instruction_vaddr;
extern char bootstrap_start[];
extern char bootstrap_end[];

// Contains information whether any given PD has a given IRQ value set.
seL4_Bool pd_irqs[MAX_PDS][MAX_IRQS];

static void *memcpy(void *dst, const void *src, uint64_t sz);

void init(void) {
    // Copy the entire bootstrap section to the bootstrap memory region.
    uint64_t bootstrap_size = (uintptr_t)bootstrap_end - (uintptr_t)bootstrap_start;
    memcpy(bootstrap_vaddr, bootstrap_start, bootstrap_size);
    asm volatile("dsb sy" ::: "memory");

    // Migrate all worker PDs to relevant cores
    core_migrate(1, 1);
    core_migrate(2, 2);
    core_migrate(3, 3);
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
        microkit_pd_restart(core, PD_INIT_ENTRY);
        break;
    case CORE_OFF:
        microkit_notify(core + 1);
        break;
    case CORE_STANDBY:
        microkit_notify(core + 1);
        break;
    case CORE_MIGRATE:
        core_migrate(pd, core);
        for (int i = 0; i < MAX_IRQS; i++) {
            seL4_Bool irq_set = pd_irqs[pd][i];
            if (irq_set) {
                seL4_IRQHandler_SetCore(BASE_IRQ_CAP + i, core);
            }
        }
        break;
    case CORE_STATUS:
        core_status(core);
        break;
    case CORE_DUMP:
        if (core == 0) {
            seL4_DebugDumpScheduler();
        } else {
            microkit_notify(core + 1);
        }
        break;
    default:
        err = 1;
        break;
    }

    microkit_mr_set(0, err);
    return microkit_msginfo_new(0, 1);
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo) {
    microkit_dbg_puts("[Core Manager API]: Received fault from worker. Restart and pray it works.\n");
    microkit_pd_restart(child, PD_INIT_ENTRY);
    /* We explicitly restart the thread so we do not need to 'reply' to the fault. */
    return seL4_False;
}

static void core_on(uint8_t core, seL4_Word cpu_bootstrap) {
    seL4_ARM_SMCContext args = {.x0 = PSCI_CPU_ON, .x1 = core, .x2 = cpu_bootstrap};
    seL4_ARM_SMCContext response = {0};

    microkit_arm_smc_call(&args, &response);

    print_error(response);
}

static void core_migrate(uint8_t pd, uint8_t core) {
    seL4_SchedControl_ConfigureFlags(
        BASE_SCHED_CONTROL_CAP + core,
        BASE_SCHED_CONTEXT_CAP + pd,
        microkit_pd_period,
        microkit_pd_budget,
        microkit_pd_extra_refills,
        microkit_pd_badge,
        microkit_pd_flags
    );
}

static void core_status(uint8_t core) {
    seL4_ARM_SMCContext args = {.x0 = PSCI_AFFINITY_INFO, .x1 = core};
    seL4_ARM_SMCContext response = {0};

    microkit_arm_smc_call(&args, &response);

    print_error(response);
}

static void *memcpy(void *dst, const void *src, uint64_t sz) {
    char *dst_ = dst;
    const char *src_ = src;
    while (sz-- > 0) {
        *dst_++ = *src_++;
    }

    return dst;
}
