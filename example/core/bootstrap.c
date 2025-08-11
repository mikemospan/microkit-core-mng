#include <stdint.h>

#define ALIGN(n)  __attribute__((__aligned__(n)))

#define UARTDR                 0x000
#define PAGE_TABLE_SIZE (1 << 12)
#define AARCH64_1GB_BLOCK_BITS 30
#define AARCH64_2MB_BLOCK_BITS 21

void el2_mmu_enable(void);

/* Paging structures for identity mapping */
uint64_t boot_lvl0_lower[1 << 9] ALIGN(1 << 12);

static void putc(int ch) {
    volatile uint32_t *uart_phys = (volatile uint32_t *)0x09000000;
    uart_phys[UARTDR/4] = ch;
}

static void puts(const char *str) {
    while (*str) {
        putc(*str++);
    }
}

static void *memset(void *dst, uint64_t val, uint64_t sz) {
    char *dst_ = dst;
    while (sz-- > 0) {
        *dst_++ = val;
    }

    return dst;
}

static int current_el(void) {
    /* See: C5.2.1 CurrentEL */
    uint32_t val;
    asm volatile("mrs %x0, CurrentEL" : "=r"(val) :: "cc");
    /* bottom two bits are res0 */
    return val >> 2;
}

void secondary_cpu_entry(uint64_t cpu_id) {
    asm volatile("dsb sy" ::: "memory");
    
    puts("[Core Manager]: Booting CPU #");
    putc(cpu_id + '0');
    putc('\n');

    int el = current_el();
    puts("CurrentEL = EL");
    putc(el + '0');
    putc('\n');

    if (el == 2) {
        /* seL4 relies on the timer to be set to a useful value */
        puts("Resetting CNTVOFF\n");
        asm volatile("msr cntvoff_el2, xzr");
    } else {
        puts("We're not in EL2!!\n");
        goto fail;
    }

    /* Get this CPU's ID and save it to TPIDR_EL1 for seL4. */
    /* Whether or not seL4 is booting in EL2 does not matter, as it always looks at tpidr_el1 */
    asm volatile("msr tpidr_el1" ",%0" :: "r" (cpu_id));

    puts("Enabling the MMU\n");
    el2_mmu_enable();

fail:
    /* Note: can't usefully return to U-Boot once we are here. */
    /* Put the CPU into a low-power wait loop */
    for (;;) {
        asm volatile("wfi");
    }
}
