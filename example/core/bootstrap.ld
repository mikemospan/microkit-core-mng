/* bootstrap.ld - Custom linker script for bootstrap sections */

SECTIONS
{
    .bootstrap : {
        bootstrap_start = .;
        
        /* Text section */
        bootstrap_text_start = .;
        *bootstrap_s.o(.text*)  /* Assembly code goes first */
        *bootstrap_c.o(.text*)  /* C code goes second */
        bootstrap_text_end = .;
        
        /* Rodata section */
        bootstrap_rodata_start = .;
        *bootstrap_c.o(.rodata*)
        bootstrap_rodata_end = .;
        
        /* Data section */
        bootstrap_data_start = .;
        *bootstrap_c.o(.data*)
        bootstrap_data_end = .;
        
        /* BSS section */
        bootstrap_bss_start = .;
        *bootstrap_c.o(.bss*)
        bootstrap_bss_end = .;
        
        bootstrap_end = .;
    }
}

/* This will insert after .text in the main linker script */
INSERT AFTER .text;