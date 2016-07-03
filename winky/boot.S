.code32
.section .mbhdr
.align 8

/*
 * This header makes us multi-boot compliant
 */
multiboot_hdr:
    .long 0xe85250d6
    .long 0
    .long hdr_end - multiboot_hdr
    .long -(0xe85250d6 + 0 + (hdr_end - multiboot_hdr))

    /* BEGIN TAGS */

    /* sections */
    .word 2, 0
    .long 24
    .long multiboot_hdr
    .long _loadStart
    .long _loadEnd
    .long _bssEnd

    /* entry point */
    .word 3, 0
    .long 16
    .long kernel_entry
    .long 0

    /* tags end */
    .word 0, 0
    .long 8
hdr_end:

.section .boot

/**
 * our entry point into the kernel, this is
 * where the bootloader will drop us
 */
 .globl kernel_entry;
kernel_entry:
    
.code64
    /* get GRUB multiboot info and stash it */
    movl %ebx, %edi
    movl %eax, %esi

    movq $boot_stack_end, %rsp

    call paging_setup

	// map the SBOX in
	// PDPT[31] -> PDT_SBOX
	movq $pd_sbox, %rax
	orq $0x3, %rax
	movq $pdpt, %rbx
	movq $31, %rcx
	movq %rax, (%rbx, %rcx, 8)

	// PDT_SBOX[2] -> MAPPED
	movq $pd_sbox, %rax
	movq $2, %rbx
	movq $0x8007D0000, %rcx
	orq $0x93, %rcx // PS, P, UC
	movq %rcx, (%rax, %rbx, 8)

    /* multiboot info is still in rdi and rsi */
    movq $main, %rax
    callq *%rax

    /* should never get here */
    cli
    jmp .

paging_setup:

    /* PML4[0] -> PDPT */
    movq $pdpt, %rax
    orq $0x3, %rax       /* entry is present, rw */
    movq $pml4, %rbx
    movq %rax, (%rbx)

    // PDPT[0] -> PDT 
    movq $pd, %rax
    orq $0x3, %rax
    movq $pdpt, %rbx
    movq %rax, (%rbx)

	// Identity map the first GB
    movl $512, %ecx
    movl $pd, %edx
    movl $0x83, %eax // set PS bit also (PDE -> 2MB page)
.write_pde:
    movl %eax, (%edx)
    addl $0x200000, %eax
    addl $0x8, %edx
    loop .write_pde

    /* enable lme bit in MSR */
    movq $0xc0000080, %rcx
    rdmsr
    orq $(1<<8), %rax
    wrmsr

    /* paging enable */
    movq %cr0, %rax
    orl $(1<<31), %eax
    movq %rax, %cr0

    ret
     
.align 8
gdt32:
    .quad 0x0000000000000000 /* null */
    .quad 0x00cf9a000000ffff /* code */
    .quad 0x00cf92000000ffff /* data */

.align 8
gdt64:
    .quad 0x0000000000000000 /* null */
    .quad 0x00a09a0000000000 /* code (note lme bit) */
    .quad 0x00a0920000000000 /* data (most entries don't matter) */


.align 8
gdtr32:
    .word 23
    .long gdt32

.align 8
.globl gdtr64; 
gdtr64:
    .word 23
    .long gdt64
    .long 0

