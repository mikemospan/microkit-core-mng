#include <stdint.h>

#define ALIGN(n)  __attribute__((__aligned__(n)))

#define UARTDR                 0x000
#define PAGE_TABLE_SIZE (1 << 12)
#define AARCH64_1GB_BLOCK_BITS 30
#define AARCH64_2MB_BLOCK_BITS 21

void el2_mmu_enable(void);

/* Paging structures for kernel mapping */
uint64_t boot_lvl0_upper[1 << 9] ALIGN(1 << 12);
uint64_t boot_lvl1_upper[1 << 9] ALIGN(1 << 12);
uint64_t boot_lvl2_upper[1 << 9] ALIGN(1 << 12);

/* Paging structures for identity mapping */
uint64_t boot_lvl0_lower[1 << 9] ALIGN(1 << 12);
uint64_t boot_lvl1_lower[1 << 9] ALIGN(1 << 12);

int hey = 3;

static void putc(int ch) {
    volatile uint32_t *uart_phys = (volatile uint32_t *)0x09000000;
    uart_phys[UARTDR/4] = ch;
}

static void puts(const char *str) {
    while (*str) {
        putc(*str++);
    }
}

static void *memset(void *dst, uint64_t val, uint64_t sz)
{
    char *dst_ = dst;
    while (sz-- > 0) {
        *dst_++ = val;
    }

    return dst;
}

static inline uint64_t lvl0_index(uint64_t vaddr) {
    return (vaddr >> 39) & 0x1FF;
}

static inline uint64_t lvl1_index(uint64_t vaddr) {
    return (vaddr >> 30) & 0x1FF;
}

static inline uint64_t lvl2_index(uint64_t vaddr) {
    return (vaddr >> 21) & 0x1FF;
}

void aarch64_setup_pagetables(uint64_t first_vaddr, uint64_t first_paddr,
                               uint64_t boot_lvl0_lower_addr, uint64_t boot_lvl0_lower_size,
                               uint64_t boot_lvl1_lower_addr, uint64_t boot_lvl1_lower_size,
                               uint64_t boot_lvl0_upper_addr, uint64_t boot_lvl0_upper_size,
                               uint64_t boot_lvl1_upper_addr, uint64_t boot_lvl1_upper_size,
                               uint64_t boot_lvl2_upper_addr, uint64_t boot_lvl2_upper_size) {
    // Set up boot_lvl0_lower: point to lvl1 lower
    memset(boot_lvl0_lower, 0, PAGE_TABLE_SIZE);
    boot_lvl0_lower[0] = boot_lvl1_lower_addr | 3;

    // Set up boot_lvl1_lower: identity map 0..512 GiB in 1GiB blocks
    for (int i = 0; i < 512; i++) {
        uint64_t pt_entry = ((uint64_t)i << AARCH64_1GB_BLOCK_BITS)
                          | (1 << 10)  // access flag
                          | (0 << 2)   // memory attributes
                          | (1);       // block descriptor
        boot_lvl1_lower[i] = pt_entry;
    }

    // Set up boot_lvl0_upper: point to lvl1 upper
    memset(boot_lvl0_upper, 0, PAGE_TABLE_SIZE);
    uint64_t l0_idx = lvl0_index(first_vaddr);
    boot_lvl0_upper[l0_idx] = boot_lvl1_upper_addr | 3;

    // Set up boot_lvl1_upper: point to lvl2 upper
    memset(boot_lvl1_upper, 0, PAGE_TABLE_SIZE);
    uint64_t l1_idx = lvl1_index(first_vaddr);
    boot_lvl1_upper[l1_idx] = boot_lvl2_upper_addr | 3;

    // Set up boot_lvl2_upper: 2 MiB blocks from first_vaddr to 1GiB
    memset(boot_lvl2_upper, 0, PAGE_TABLE_SIZE);
    uint64_t l2_start = lvl2_index(first_vaddr);
    for (uint64_t i = l2_start; i < 512; i++) {
        uint64_t entry_idx = (i - l2_start) << AARCH64_2MB_BLOCK_BITS;
        uint64_t pt_entry = (first_paddr + entry_idx)
                          | (1 << 10) // access flag
                          | (3 << 8)  // shareability
                          | (4 << 2)  // MT_NORMAL memory
                          | (1);      // 2MB block
        boot_lvl2_upper[i] = pt_entry;
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

    puts("Setting up the hardware page table\n");
    aarch64_setup_pagetables(
        0x8060000000,
        0x60000000,
        (uint64_t)&boot_lvl0_lower, PAGE_TABLE_SIZE,
        (uint64_t)&boot_lvl1_lower, PAGE_TABLE_SIZE,
        (uint64_t)&boot_lvl0_upper, PAGE_TABLE_SIZE,
        (uint64_t)&boot_lvl1_upper, PAGE_TABLE_SIZE,
        (uint64_t)&boot_lvl2_upper, PAGE_TABLE_SIZE
    );

    puts("Enabling the MMU\n");
    el2_mmu_enable();

fail:
    /* Note: can't usefully return to U-Boot once we are here. */
    /* Put the CPU into a low-power wait loop */
    for (;;) {
        asm volatile("wfi");
    }
}
