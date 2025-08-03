/* bootstrap.ld - Simple linker script to group bootstrap code */
SECTIONS
{
    .text.bootstrap : {
        bootstrap_start = .;
        *(.text.bootstrap_asm)    /* Assembly first */
        *(.text.bootstrap_c)      /* C function second */
        bootstrap_end = .;
    }
}
INSERT AFTER .text;