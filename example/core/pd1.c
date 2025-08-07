#include "core.h"

#define PD2_CHANNEL 2

#define ALIGN(n)  __attribute__((__aligned__(n)))
#define SECTION(n)  __attribute__((section(n)))


/* This macro embeds a string literal directly into the bootstrap code section and 
* returns a position-independent pointer to it. This is needed because normal strings 
* can get placed at virtual addresses that are inaccessible when bootstrap code 
* runs from copied physical memory at 0x80000000. Uses ADR instruction to calculate 
* the string address relative to the current program counter, bypassing linker 
* virtual addressing and compiler optimisations.
*/
#define BOOTSTRAP_STR(string_literal) ({ \
    const char *__str_ptr; \
    asm volatile( \
        "adr %0, 1f\n\t" \
        "b 2f\n" \
        "1: .asciz " #string_literal "\n\t" \
        ".balign 4\n" \
        "2:" \
        : "=r" (__str_ptr) \
        : \
        : "memory" \
    ); \
    __str_ptr; \
})

uintptr_t buffer_vaddr;
uintptr_t bootstrap_vaddr;
extern char bootstrap_start[];
extern char bootstrap_end[];

static void *memcpy(void *dst, const void *src, uint64_t sz);
void switch_to_el1(void);
void el2_mmu_enable(void);

/* Paging structures for kernel mapping */
uint64_t boot_lvl0_upper[1 << 9] ALIGN(1 << 12);
uint64_t boot_lvl1_upper[1 << 9] ALIGN(1 << 12);
uint64_t boot_lvl2_upper[1 << 9] ALIGN(1 << 12);

/* Paging structures for identity mapping */
uint64_t boot_lvl0_lower[1 << 9] ALIGN(1 << 12);
uint64_t boot_lvl1_lower[1 << 9] ALIGN(1 << 12);

void init(void) {
    microkit_dbg_puts("[PD 1]: Starting!\n");

    uart_init();

    // Copy to the mapped bootstrap region and flush the cache
    uint64_t bootstrap_size = (uintptr_t)bootstrap_end - (uintptr_t)bootstrap_start;
    memcpy((void*)bootstrap_vaddr, bootstrap_start, bootstrap_size);
    asm volatile("dsb sy" ::: "memory");
}

void notified(microkit_channel ch) {
    if (ch != UART_IRQ_CH) {
        microkit_dbg_puts("Received unexpected notification\n");
        return;
    }

    ((char *) buffer_vaddr)[0] = uart_get_char();
    uart_handle_irq();

    switch (((char *) buffer_vaddr)[0]) {
    case 'h':
        microkit_dbg_puts(
            "\n=== LIST OF COMMANDS ===\n"
            "h: help\n"
            "p: print psci version\n"
            "i: view the status of core #0\n"
            "d: core dump\n"
            "m: migrate pd1\n"
            "n: migrate pd2\n"
            "x: turn off pd2's core\n"
            "s: put pd2's core in standby\n"
            "y: turn on pd2's core\n"
        );
        break;
    case 'p':
        print_psci_version();
        break;
    case 'd':
        microkit_dbg_puts("=== THE FOLLOWING DUMP IS FOR PROTECTION DOMAINS RUNNING ON PD1's CORE ===\n");
        seL4_DebugDumpScheduler();
        microkit_notify(PD2_CHANNEL);
        break;
    case 's':
        microkit_notify(PD2_CHANNEL);
        break;
    case 'm':
        core_migrate(0);
        seL4_IRQHandler_SetCore(BASE_IRQ_CAP + UART_IRQ_CH, 2);
        break;
    case 'n':
        microkit_notify(PD2_CHANNEL);
        break;
    case 'x':
        microkit_notify(PD2_CHANNEL);
        break;
    case 'y':
        core_on(3, 0x80000000);
        break;
    case 'i':
        microkit_dbg_puts("[PD 1]: Viewing status of core #3\n");
        core_status(3);
        break;
    }

    microkit_irq_ack(ch);
}

static void *memcpy(void *dst, const void *src, uint64_t sz)
{
    char *dst_ = dst;
    const char *src_ = src;
    while (sz-- > 0) {
        *dst_++ = *src_++;
    }

    return dst;
}

SECTION(".text.bootstrap")
static void uart_put_char_phys(int ch) {
    volatile uint32_t *uart_phys = (volatile uint32_t *)0x09000000;
    uart_phys[UARTDR/4] = ch;
}

SECTION(".text.bootstrap")
static void uart_put_str_phys(const char *str) {
    while (*str) {
        uart_put_char_phys(*str);
        str++;
    }
}

SECTION(".text.bootstrap")
static void uart_put_hex_phys(uint64_t value) {
    uart_put_str_phys("0x");
    
    // Print 16 hex digits (64 bits)
    for (int i = 15; i >= 0; i--) {
        uint8_t nibble = (value >> (i * 4)) & 0xF;
        if (nibble < 10) {
            uart_put_char_phys('0' + nibble);
        } else {
            uart_put_char_phys('A' + nibble - 10);
        }
    }
}

SECTION(".text.bootstrap")
static int current_el(void) {
    /* See: C5.2.1 CurrentEL */
    uint32_t val;
    asm volatile("mrs %x0, CurrentEL" : "=r"(val) :: "cc");
    /* bottom two bits are res0 */
    return val >> 2;
}

SECTION(".text.bootstrap")
void secondary_cpu_entry(uint64_t cpu_id) {
    asm volatile("dsb sy" ::: "memory");
    
    uart_put_str_phys(BOOTSTRAP_STR("[Core Manager]: Booting CPU #"));
    uart_put_char_phys(cpu_id + '0');
    uart_put_char_phys('\n');

    int el = current_el();
    uart_put_str_phys(BOOTSTRAP_STR("CurrentEL = EL"));
    uart_put_char_phys(el + '0');
    uart_put_char_phys('\n');

    if (el == 2) {
        /* seL4 relies on the timer to be set to a useful value */
        uart_put_str_phys(BOOTSTRAP_STR("Resetting CNTVOFF\n"));
        asm volatile("msr cntvoff_el2, xzr");
    } else {
        uart_put_str_phys(BOOTSTRAP_STR("We're not in EL2!!\n"));
        goto fail;
    }

    /* Get this CPU's ID and save it to TPIDR_EL1 for seL4. */
    /* Whether or not seL4 is booting in EL2 does not matter, as it always looks at tpidr_el1 */
    asm volatile("msr tpidr_el1" ",%0" :: "r" (cpu_id));

    uart_put_str_phys(BOOTSTRAP_STR("Enabling the MMU\n"));
    el2_mmu_enable();

fail:
    /* Note: can't usefully return to U-Boot once we are here. */
    /* IMPROVEMENT: use SMC SVC call to try and power-off / reboot system.
     * or at least go to a WFI loop
     */
    for (;;) {
    }
}
