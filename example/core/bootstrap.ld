/* bootstrap.ld - Custom linker script for bootstrap sections */

SECTIONS
{
    .bootstrap : ALIGN(4096) {
        bootstrap_start = .;
        *bootstrap_s.o(.*)  /* Assembly sections go first */
        *bootstrap_c.o(.*)  /* C sections go second */
        bootstrap_end = .;
    }
}

/* This will insert after .text in the main linker script */
INSERT AFTER .text;

/* Ensure we don't exceed reasonable size (32KB limit) */
ASSERT(bootstrap_end - bootstrap_start <= 0x8000, "Bootstrap code too large")