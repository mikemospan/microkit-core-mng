#include <stdint.h>

#define STACK_SIZE 4096
#define ALIGN(n) __attribute__((__aligned__(n)))

/* Start the kernel entry point */
#define START_KERNEL() ((sel4_entry)(kernel_entry))(0, 0, 0, 0, 0, 0, 0, 0)

/* Put the CPU into a low-power wait loop when failed */
#define FAIL() for (;;) { asm volatile("wfi"); }

/* Kernel entry function type */
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

/* Enable EL2 MMU */
void el2_mmu_enable(void);

/* Temporary hardware page tables for boot */
uint64_t boot_lvl0_lower[512] ALIGN(4096);
uint64_t boot_lvl0_upper[512] ALIGN(4096);
uint64_t boot_lvl1_lower[512] ALIGN(4096);
uint64_t boot_lvl1_upper[512] ALIGN(4096);
uint64_t boot_lvl2_upper[512] ALIGN(4096);

/* Kernel entry point address */
uintptr_t kernel_entry;

/* Stack for each CPU core */
volatile uint8_t cpu_stacks[NUM_CPUS][STACK_SIZE] ALIGN(16);

/* --- Simple UART output --- */
static inline void putc(int ch) {
    volatile uint32_t *uart_phys = (volatile uint32_t *)0xff803000;
    *uart_phys = ch;
}

static inline void puts(const char *str) {
    while (*str) {
        putc(*str++);
    }
}

/* Get current exception level */
static inline uint32_t current_el(void) {
    uint32_t val;
    asm volatile("mrs %x0, CurrentEL" : "=r"(val) :: "cc");
    return val >> 2; // EL encoded in top bits
}

/* Entry point for secondary CPUs */
void secondary_cpu_entry(uint64_t cpu_id) {
    asm volatile("dsb sy" ::: "memory");

    puts("[Core Manager]: Booting CPU #");
    putc(cpu_id + '0');
    putc('\n');

    uint32_t el = current_el();
    puts("CurrentEL = EL");
    putc(el + '0');
    putc('\n');

    if (el != 2) {
        puts("Error: not in EL2!\n");
        FAIL();
    }

    /* Reset virtual offset timer for EL2 */
    puts("Resetting CNTVOFF\n");
    asm volatile("msr cntvoff_el2, xzr");

    /* Save CPU ID in TPIDR_EL1 for seL4 */
    asm volatile("msr tpidr_el1, %0" :: "r"(cpu_id));

    puts("Enabling the MMU\n");
    el2_mmu_enable();

    puts("Starting the seL4 kernel\n");
    START_KERNEL();

    /* Should never return from kernel */
    puts("[Core Manager]: Error - Kernel returned (CPU ");
    putc(cpu_id + '0');
    puts(")\n");
    FAIL();
}
