#include "core.h"

#define WORKER_CHANNEL  2
#define PD_INIT_ENTRY   0x200000
#define BOOTSTRAP_ENTRY 0x80000000

enum Instruction {
    CORE_ON,
    CORE_OFF,
    CORE_STANDBY,
    CORE_MIGRATE,
    CORE_STATUS,
    CORE_DUMP,
};

uintptr_t bootstrap_vaddr;
uintptr_t buffer_vaddr;
extern char bootstrap_start[];
extern char bootstrap_end[];

static void *memcpy(void *dst, const void *src, uint64_t sz);

void init(void) {
    microkit_dbg_puts("[Core Manager API]: Starting!\n");
    
    // Copy the entire bootstrap section to the bootstrap memory region.
    uint64_t bootstrap_size = (uintptr_t)bootstrap_end - (uintptr_t)bootstrap_start;
    memcpy((void*)bootstrap_vaddr, bootstrap_start, bootstrap_size);
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
    enum Instruction code = microkit_mr_get(0);
    uint8_t core = microkit_mr_get(1);
    uint8_t pd = microkit_mr_get(2);
    int err = 0;

    switch (code) {
    case CORE_ON:
        core_on(core, BOOTSTRAP_ENTRY);
        microkit_pd_restart(1, PD_INIT_ENTRY);
        break;
    case CORE_OFF:
        ((char *) buffer_vaddr)[0] = 'x';
        microkit_notify(WORKER_CHANNEL);
        break;
    case CORE_STANDBY:
        ((char *) buffer_vaddr)[0] = 's';
        microkit_notify(WORKER_CHANNEL);
        break;
    case CORE_MIGRATE:
        core_migrate(pd, core);
        // seL4_IRQHandler_SetCore(BASE_IRQ_CAP + UART_IRQ_CH, core);
        break;
    case CORE_STATUS:
        core_status(core);
        break;
    case CORE_DUMP:
        // TODO: Get every core worker to perform this dump
        seL4_DebugDumpScheduler();
        break;
    default:
        err = 1;
        break;
    }

    microkit_mr_set(0, err);
    return microkit_msginfo_new(0, 1);
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo) {
    microkit_dbg_puts("[Core Manager API]: Received fault from worker. Restart and pray it works.");
    microkit_pd_restart(child, PD_INIT_ENTRY);
    /* We explicitly restart the thread so we do not need to 'reply' to the fault. */
    return seL4_False;
}

static void *memcpy(void *dst, const void *src, uint64_t sz) {
    char *dst_ = dst;
    const char *src_ = src;
    while (sz-- > 0) {
        *dst_++ = *src_++;
    }

    return dst;
}
