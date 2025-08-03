/* bootstrap.ld - Simple linker script to group bootstrap code */

SECTIONS
{
    .text.bootstrap : {
        bootstrap_start = .;
        *bootstrap.o(.text.bootstrap)     /* Assembly file first */
        *(.text.bootstrap)                /* Then everything else */
        bootstrap_end = .;
    }
}
INSERT AFTER .text;