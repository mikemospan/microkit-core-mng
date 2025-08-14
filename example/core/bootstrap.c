#include <stdint.h>

#define NUM_CPUS 4
#define STACK_SIZE 4096

#define ALIGN(n)  __attribute__((__aligned__(n)))
#define START_KERNEL() ((sel4_entry)(kernel_entry))(0, 0, 0, 0, 0, 0, 0, 0)
/* Put the CPU into a low-power wait loop when failed */
#define FAIL() for (;;) { asm volatile("wfi"); }

typedef void (*sel4_entry)(
    uintptr_t ui_p_reg_start,
    uintptr_t ui_p_reg_end,
    intptr_t pv_offset,
    uintptr_t v_entry,
    uintptr_t dtb_addr_p,
    uintptr_t dtb_size,
    uintptr_t extra_device_addr_p,
    uintptr_t extra_device_size
);

void el2_mmu_enable(void);

/* Reference to temporary hardware page table after MMU setup */
uint64_t boot_lvl0_lower[512] ALIGN(4096);
uint64_t boot_lvl0_upper[512] ALIGN(4096);
uint64_t boot_lvl1_lower[512] ALIGN(4096);
uint64_t boot_lvl1_upper[512] ALIGN(4096);
uint64_t boot_lvl2_upper[512] ALIGN(4096);
/* Virtual memory into the kernel initialisation function */
uintptr_t kernel_entry;
/* Stack of each CPU core in the system */
volatile uint8_t cpu_stacks[NUM_CPUS][STACK_SIZE] ALIGN(16);

static inline void putc(int ch) {
    volatile uint32_t *uart_phys = (volatile uint32_t *)0x9000000;
    *uart_phys = ch;
}

static void puts(const char *str) {
    while (*str) {
        putc(*str++);
    }
}

static void put_hex64(uint64_t num) {
    puts("0x");

    int started = 0;
    for (int i = 15; i >= 0; i--) {
        uint8_t nibble = (num >> (i * 4)) & 0xF;
        if (nibble || started || i == 0) {
            started = 1;
            if (nibble < 10) {
                putc('0' + nibble);
            } else {
                putc('a' + (nibble - 10));
            }
        }
    }
}

static inline uint32_t current_el(void) {
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

    uint32_t el = current_el();
    puts("CurrentEL = EL");
    putc(el + '0');
    putc('\n');

    if (el == 2) {
        /* seL4 relies on the timer to be set to a useful value */
        puts("Resetting CNTVOFF\n");
        asm volatile("msr cntvoff_el2, xzr");
    } else {
        puts("We're not in EL2!!\n");
        FAIL();
    }

    /* Get this CPU's ID and save it to TPIDR_EL1 for seL4. */
    /* Whether or not seL4 is booting in EL2 does not matter, as it always looks at tpidr_el1 */
    asm volatile("msr tpidr_el1" ",%0" :: "r" (cpu_id));

    puts("Enabling the MMU\n");
    el2_mmu_enable();

    // For DEBUGING
    // volatile uint64_t blah = 0;
    // while (blah < 10000000) {}

    puts("Starting the seL4 kernel\n");
    START_KERNEL();

    puts("[Core Manager]: Error - KERNEL RETURNED (CPU ");
    putc(cpu_id + '0');
    puts(")\n");

    /* Note: can't usefully return to U-Boot once we are here. */
    FAIL();
}
