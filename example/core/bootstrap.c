#include <stdint.h>

#define NUM_CPUS 4
#define STACK_SIZE 4096

#define PAGE_TABLE_SIZE         (1 << 12)
#define AARCH64_1GB_BLOCK_BITS  30
#define AARCH64_2MB_BLOCK_BITS  21

#define AARCH64_LVL0_BITS 9
#define AARCH64_LVL1_BITS 9
#define AARCH64_LVL2_BITS 9
#define AARCH64_2MB_BLOCK_BITS 21
#define MASK_9BITS 0x1FF

#define ALIGN(n)  __attribute__((__aligned__(n)))
#define START_KERNEL() ((sel4_entry)(kernel_entry))(0, 0, 0, 0, 0, 0, 0, 0)

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
uint64_t boot_lvl1_lower[512] ALIGN(4096);
uint64_t boot_lvl0_upper[512] ALIGN(4096);
uint64_t boot_lvl1_upper[512] ALIGN(4096);
uint64_t boot_lvl2_upper[512] ALIGN(4096);
/* Bootstrap data needed to start the kernel */
uintptr_t kernel_entry;
uintptr_t kernel_first_vaddr;
uintptr_t kernel_first_paddr;
/* Stack of each CPU core in the system */
volatile uint8_t cpu_stacks[NUM_CPUS][STACK_SIZE] ALIGN(16);

static inline void putc(int ch) {
    volatile uint32_t *uart_phys = (volatile uint32_t *)0x09000000;
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

static inline int current_el(void) {
    /* See: C5.2.1 CurrentEL */
    uint32_t val;
    asm volatile("mrs %x0, CurrentEL" : "=r"(val) :: "cc");
    /* bottom two bits are res0 */
    return val >> 2;
}

static inline uintptr_t lvl0_index(uintptr_t vaddr) {
    return (vaddr >> (AARCH64_2MB_BLOCK_BITS + AARCH64_LVL2_BITS + AARCH64_LVL1_BITS)) & MASK_9BITS;
}

static inline uintptr_t lvl1_index(uintptr_t vaddr) {
    return (vaddr >> (AARCH64_2MB_BLOCK_BITS + AARCH64_LVL2_BITS)) & MASK_9BITS;
}

static inline uintptr_t lvl2_index(uintptr_t vaddr) {
    return (vaddr >> AARCH64_2MB_BLOCK_BITS) & MASK_9BITS;
}

static void setup_bootstrap_pagetables(void) {
    // Clear all page tables
    for (int i = 0; i < 512; i++) {
        boot_lvl0_lower[i] = 0;
        boot_lvl1_lower[i] = 0;
        boot_lvl0_upper[i] = 0;
        boot_lvl1_upper[i] = 0;
        boot_lvl2_upper[i] = 0;
    }
    
    puts("Setting up enhanced page tables...\n");
    puts("first_vaddr: "); put_hex64(kernel_first_vaddr); puts("\n");
    puts("first_paddr: "); put_hex64(kernel_first_paddr); puts("\n");
    
    // 1. Setup boot_lvl0_lower - points to boot_lvl1_lower for identity mapping
    boot_lvl0_lower[0] = (uint64_t)boot_lvl1_lower | 3;  // Valid table descriptor
    
    // 2. Setup boot_lvl1_lower - identity map entire lower address space in 1GB blocks
    for (int i = 0; i < 512; i++) {
        uint64_t pt_entry = ((uint64_t)i << AARCH64_1GB_BLOCK_BITS) |
                          (1 << 10) |    // Access flag
                          (0 << 2) |     // Strongly ordered memory (device)
                          (1);           // 1GB block descriptor
        boot_lvl1_lower[i] = pt_entry;
    }

    // 3. Setup upper mapping for kernel virtual addresses
    // boot_lvl0_lower also needs an entry for the upper virtual address space
    uint64_t upper_lvl0_idx = lvl0_index(kernel_first_vaddr);
    boot_lvl0_lower[upper_lvl0_idx] = (uint64_t)boot_lvl1_upper | 3;

    // 4. Setup boot_lvl1_upper - points to boot_lvl2_upper for the specific 1GB region
    uint64_t upper_lvl1_idx = lvl1_index(kernel_first_vaddr);
    boot_lvl1_upper[upper_lvl1_idx] = (uint64_t)boot_lvl2_upper | 3;
    
    // 5. Setup boot_lvl2_upper - map the kernel region in 2MB blocks
    uint64_t lvl2_start_idx = lvl2_index(kernel_first_vaddr);
    for (int i = lvl2_start_idx; i < 512; i++) {
        uint64_t entry_offset = ((uint64_t)(i - lvl2_start_idx)) << AARCH64_2MB_BLOCK_BITS;
        uint64_t pt_entry = (entry_offset + kernel_first_paddr) |
                          (1 << 10) |    // Access flag
                          (3 << 8) |     // Shareability (same as kernel)
                          (4 << 2) |     // MT_NORMAL memory
                          (1 << 0);      // 2MB block descriptor
        boot_lvl2_upper[i] = pt_entry;
    }
    
    puts("Page table setup complete:\n");
    puts("  boot_lvl0_lower at: "); put_hex64((uint64_t)boot_lvl0_lower); puts("\n");
    puts("  boot_lvl1_lower at: "); put_hex64((uint64_t)boot_lvl1_lower); puts("\n");
    puts("  boot_lvl0_upper at: "); put_hex64((uint64_t)boot_lvl0_upper); puts("\n");
    puts("  boot_lvl1_upper at: "); put_hex64((uint64_t)boot_lvl1_upper); puts("\n");
    puts("  boot_lvl2_upper at: "); put_hex64((uint64_t)boot_lvl2_upper); puts("\n");
    puts("  Upper L0 index: "); put_hex64(upper_lvl0_idx); puts("\n");
    puts("  Upper L1 index: "); put_hex64(upper_lvl1_idx); puts("\n");
    puts("  L2 start index: "); put_hex64(lvl2_start_idx); puts("\n");
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

    puts("Setting up bootstrap page table...\n");
    setup_bootstrap_pagetables();

    puts("Enabling the MMU\n");
    el2_mmu_enable();

    puts("Starting the seL4 kernel\n");
    START_KERNEL();

    puts("[Core Manager]: Error - KERNEL RETURNED (CPU ");
    putc(cpu_id + '0');
    puts(")\n");

fail:
    /* Note: can't usefully return to U-Boot once we are here. */
    /* Put the CPU into a low-power wait loop */
    for (;;) {
        asm volatile("wfi");
    }
}
