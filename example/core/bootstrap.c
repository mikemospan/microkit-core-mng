#include <stdint.h>

#define ALIGN(n)  __attribute__((__aligned__(n)))

#define PAGE_TABLE_SIZE         (1 << 12)
#define AARCH64_1GB_BLOCK_BITS  30
#define AARCH64_2MB_BLOCK_BITS  21

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
uint64_t bootstrap_lvl0[512] ALIGN(4096);
uint64_t bootstrap_lvl1[512] ALIGN(4096);
/* Bootstrap data needed to later start the kernel */
uintptr_t kernel_entry;
uintptr_t ui_p_reg_start;
uintptr_t ui_p_reg_end;
intptr_t pv_offset;
uintptr_t v_entry;
uintptr_t dtb_addr_p;
uintptr_t dtb_size;
uintptr_t extra_device_addr_p;
uintptr_t extra_device_size;

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

static inline void start_kernel(void) {
    ((sel4_entry)(kernel_entry))(
        ui_p_reg_start,
        ui_p_reg_end,
        pv_offset,
        v_entry,
        0,
        0,
        extra_device_addr_p,
        extra_device_size
    );
}

static inline int current_el(void) {
    /* See: C5.2.1 CurrentEL */
    uint32_t val;
    asm volatile("mrs %x0, CurrentEL" : "=r"(val) :: "cc");
    /* bottom two bits are res0 */
    return val >> 2;
}

static void setup_bootstrap_pagetables(void) {
    // Clear both page tables
    for (int i = 0; i < 512; i++) {
        bootstrap_lvl0[i] = 0;
        bootstrap_lvl1[i] = 0;
    }
    
    // Set up lvl0: point entry 0 to our lvl1 table
    // This covers virtual addresses 0x00000000 - 0x7FFFFFFFFF (512GB)
    bootstrap_lvl0[0] = (uint64_t)bootstrap_lvl1 | 0x3;  // Valid table descriptor
    
    // Set up lvl1: identity map the first several GB in 1GB blocks
    for (int i = 0; i < 4; i++) {  // Map first 4GB (0x00000000 - 0xFFFFFFFF)
        uint64_t pt_entry = ((uint64_t)i << AARCH64_1GB_BLOCK_BITS)  // Physical address
                          | (1 << 10)    // Access flag
                          | (0 << 2)     // Device memory attributes for peripherals
                          | (1);         // 1GB block descriptor
        
        // For the region containing our bootstrap code (0x80000000), use normal memory
        if (i == 2) {  // 0x80000000 - 0xBFFFFFFF range
            pt_entry = ((uint64_t)i << AARCH64_1GB_BLOCK_BITS)
                     | (1 << 10)    // Access flag  
                     | (4 << 2)     // Normal memory attributes
                     | (1);         // 1GB block descriptor
        }
        
        bootstrap_lvl1[i] = pt_entry;
        
        puts("  L1[");
        putc('0' + i);
        puts("]: ");
        put_hex64(pt_entry);
        puts(" (maps ");
        put_hex64((uint64_t)i << AARCH64_1GB_BLOCK_BITS);
        puts(" - ");
        put_hex64(((uint64_t)i << AARCH64_1GB_BLOCK_BITS) + 0x3FFFFFFF);
        puts(")\n");
    }
    
    puts("Bootstrap page table setup complete.\n");
    puts("L0 table at: ");
    put_hex64((uint64_t)bootstrap_lvl0);
    puts("\nL1 table at: ");
    put_hex64((uint64_t)bootstrap_lvl1);
    putc('\n');
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
    start_kernel();

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
