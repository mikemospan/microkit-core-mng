/* aarch64.ld - Correct linker script to place bootstrap after text */

SECTIONS
{
    .text.bootstrap : {
        bootstrap_text_start = .;
        *bootstrap_s.o(.text*)  /* Assembly file first */  
        *bootstrap_c.o(.text*)  /* C file second */
        bootstrap_text_end = .;
    }

    .rodata.bootstrap : {
        bootstrap_rodata_start = .;
        *bootstrap_c.o(.rodata*)
        bootstrap_rodata_end = .;
    }
}

/* This inserts the section definition into the default linker script AFTER .text */
INSERT AFTER .text;
