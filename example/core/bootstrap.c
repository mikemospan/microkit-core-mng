#include <stdint.h>

#define ALIGN(n)  __attribute__((__aligned__(n)))

#define UARTDR                 0x000
#define PAGE_TABLE_SIZE (1 << 12)
#define AARCH64_1GB_BLOCK_BITS 30
#define AARCH64_2MB_BLOCK_BITS 21

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

/* Physical address to the underlying hardware page table */
uintptr_t boot_lvl0_lower[1 << 9] ALIGN(1 << 12);
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

static void putc(int ch) {
    volatile uint32_t *uart_phys = (volatile uint32_t *)0x09000000;
    uart_phys[UARTDR/4] = ch;
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

static void start_kernel(void)
{
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

    start_kernel();

    puts("seL4 Loader: Error - KERNEL RETURNED (CPU ");
    putc(cpu_id + '0');
    puts(")\n");

fail:
    /* Note: can't usefully return to U-Boot once we are here. */
    /* Put the CPU into a low-power wait loop */
    for (;;) {
        asm volatile("wfi");
    }
}
