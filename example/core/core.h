#pragma once

#include <microkit.h>
#include <stdint.h>

#define MAX_PDS             63

#define PSCI_VERSION_FID    0x84000000
#define PSCI_CPU_SUSPEND    0x84000001
#define PSCI_CPU_OFF        0x84000002

#ifdef ARCH_aarch64
#define PSCI_CPU_ON         0xC4000003
#define PSCI_AFFINITY_INFO  0xC4000004
#else
#define PSCI_CPU_ON         0x84000003
#define PSCI_AFFINITY_INFO  0x84000004
#endif

/* Possible Error Codes */
#define PSCI_SUCCESS                0
#define PSCI_E_NOT_SUPPORTED        ((unsigned long) -1)
#define PSCI_E_INVALID_PARAMETERS   ((unsigned long) -2)
#define PSCI_E_DENIED               ((unsigned long) -3)
#define PSCI_E_ALREADY_ON           ((unsigned long) -4)
#define PSCI_E_ON_PENDING           ((unsigned long) -5)
#define PSCI_E_INTERNAL_FAILURE     ((unsigned long) -6)
#define PSCI_E_DISABLED             ((unsigned long) -8)
#define PSCI_E_INVALID_ADDRESS      ((unsigned long) -9)

typedef enum {
    CORE_ON,
    CORE_OFF,
    CORE_POWERDOWN,
    CORE_STANDBY,
    CORE_MIGRATE,
    CORE_MIGRATE_MONITOR,
    CORE_STATUS
} Instruction;

static int print_error(seL4_ARM_SMCContext response) {
    switch (response.x0) {
    case PSCI_E_NOT_SUPPORTED:
        microkit_dbg_puts("Your request is not supported.\n");
        break;
    case PSCI_E_INVALID_PARAMETERS:
        microkit_dbg_puts("Your request had invalid parameters.\n");
        break;
    case PSCI_E_DENIED:
        microkit_dbg_puts("Your request was denied due to firmware enforced policy.\n");
        break;
    case PSCI_E_ALREADY_ON:
        microkit_dbg_puts("The core you are trying to turn on, is already on.\n");
        break;
    case PSCI_E_ON_PENDING:
        microkit_dbg_puts("A previous instruction is still being completed on this core.\n");
        break;
    case PSCI_E_INTERNAL_FAILURE:
        microkit_dbg_puts("This specific core cannot be operated on due to physical reasons.\n");
        break;
    case PSCI_E_DISABLED:
        microkit_dbg_puts("This specific core cannot be operated on as it is disabled.\n");
        break;
    case PSCI_E_INVALID_ADDRESS:
        microkit_dbg_puts("The provided entry point address for the core is invalid.\n");
        break;
    default:
        return 0; // Assume it's not an error
    }

    return 1;
}

static void *memcpy(void *dst, const void *src, uint64_t sz) {
    char *d = dst;
    const char *s = src;
    while (sz--) *d++ = *s++;
    return dst;
}
