/* aarch64.ld - Correct linker script to place bootstrap after text */
SECTIONS
{
    /* Place bootstrap section immediately after the text section ends */
    .text.bootstrap : {
        bootstrap_start = .;
        *bootstrap_s.o(.text.bootstrap)     /* Assembly file first */  
        *bootstrap_c.o(.text.bootstrap)     /* C file second */
        bootstrap_end = .;
    }
}
/* This inserts the section definition into the default linker script AFTER .text */
INSERT AFTER .text;