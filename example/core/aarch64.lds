/* aarch64.lds - Linker script to place bootstrap code at specific physical address */

SECTIONS {
    /* Place bootstrap code at the physical address we'll pass to core_on() */
    . = 0x80000000;
    
    .bootstrap : {
        __bootstrap_phys_start = .;
        *(.text.bootstrap)
        *(.data.bootstrap)
        __bootstrap_phys_end = .;
    }
    
    /* Ensure we don't exceed reasonable size (4KB limit) */
    ASSERT(__bootstrap_phys_end - __bootstrap_phys_start <= 0x1000, "Bootstrap code too large")
}