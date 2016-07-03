ENTRY(kernel_entry)

SECTIONS
{
	/* For now we must link and load the kernel at 16MB, this will eventually change */
     . = 16M;

    .boot :
    {
        *(.mbhdr)
        _loadStart = .;
        *(.boot)
        . = ALIGN(4096);

		/* our boot-time page tables */
        pml4 = .;
        . += 0x1000;

        pdpt = .;
        . += 0x1000;

        pd = .;
        . += 0x1000; 

		/* we have one for the SBOX region as well, won't fit within 1st gig */
		pd_sbox = .;
		. += 0x1000;

        . += 0x1000; /* buffer zone */

        boot_stack_start = .;

        . += 0x200000;  /* we start out with a 2MB stack */

        boot_stack_end = .;

        . += 0x1000; /* buffer zone */
    }


    .text : 
    {
        *(.text*)
        *(.gnu.linkonce.t*)
    }

    .data : AT(ADDR(.text) + SIZEOF(.text))
    {
        *(.data*)
        *(.gnu.linkonce.d*)
    }
    
    .rodata : AT(ADDR(.data) + SIZEOF(.data))
    {
        *(.rodata*)
        *(.gnu.linkonce.r*)
    }
    

    .got : AT(ADDR(.rodata)+SIZEOF(.rodata))
    {
        *(.got*)
        *(.gnu.linkconce.got*)
    }


    _loadEnd = .; 
    
    .bss : AT(ADDR(.got)+SIZEOF(.got))
    {
        *(COMMON)
        *(.bss*)
        *(.gnu.linkonce.b*)
    }
    
    _bssEnd = .; 
    
    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
    }
}
