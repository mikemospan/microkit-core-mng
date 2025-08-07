#include <stdint.h>

#define ALIGN(n)  __attribute__((__aligned__(n)))

#define UARTDR                 0x000

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

void switch_to_el1(void);
void el2_mmu_enable(void);

/* Paging structures for kernel mapping */
uint64_t boot_lvl0_upper[1 << 9] ALIGN(1 << 12);
uint64_t boot_lvl1_upper[1 << 9] ALIGN(1 << 12);
uint64_t boot_lvl2_upper[1 << 9] ALIGN(1 << 12);

/* Paging structures for identity mapping */
uint64_t boot_lvl0_lower[1 << 9] ALIGN(1 << 12);
uint64_t boot_lvl1_lower[1 << 9] ALIGN(1 << 12);

static void uart_put_char_phys(int ch) {
    volatile uint32_t *uart_phys = (volatile uint32_t *)0x09000000;
    uart_phys[UARTDR/4] = ch;
}

static void uart_put_str_phys(const char *str) {
    while (*str) {
        uart_put_char_phys(*str);
        str++;
    }
}

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

static int current_el(void) {
    /* See: C5.2.1 CurrentEL */
    uint32_t val;
    asm volatile("mrs %x0, CurrentEL" : "=r"(val) :: "cc");
    /* bottom two bits are res0 */
    return val >> 2;
}

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
