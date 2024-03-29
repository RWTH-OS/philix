/**
 * \file
 * \brief Elver - Intermediary stage bootloader
 *
 * Elver is used to switch the system into 64-bit long-mode and load
 * the kernel, which is a relocatable ELF64 image. Unfortunately, GRUB
 * is not able to this without a patch. This is purely for
 * backwards-compatibility. As soon as bootloaders support loading
 * relocatable ELF64 images into 64-bit mode, this can be dropped.
 */

/*
 * Copyright (c) 2007, 2008, 2009, 2010, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <barrelfish_kpi/types.h>
#include <errors/errno.h>
#include <elf/elf.h>

#include <xeon_phi/xeon_phi.h>

#include "mbi.h"

#include <multiboot.h>

/* the boot magic */
#define K1OM_BOOT_MAGIC         0xB001B001

/* the address of the Xeon Phi SBOX registers used for status prints*/
#define SBOX_BASE           0x08007D0000ULL

/* reference to the end of bootloader */
extern char _end_bootloader;

/// Round up n to the next multiple of size
#define ROUND_UP(n, size)           ((((n) + (size) - 1)) & (~((size) - 1)))
#define MAX(x,y)  ((x)>(y) ? (x) : (y))
#define BASE_PAGE_SIZE 0x1000

/* Pointer to the multiboot struct we use */
struct multiboot_info *multiboot_info;

/* Address where we can safely allocate memory */
static lpaddr_t phys_alloc_start;

/* the entry address of the loaded kernel */
genvaddr_t kernel_entry;

/**
 * C level entry point for the boot loader
 *
 * \param magic this field must hold the value K1OM_BOOT_MAGIC
 * \param mb    pointer to the boot_params struct setup by the boot loader
 */
int
loader(uint64_t magic,
       struct xeon_phi_boot_params *bp);

/*
 * ----------------------------------------------------------------------------
 *  Basic Error Reporting Mechanism
 * ----------------------------------------------------------------------------
 */
union status
{
    uint32_t raw;
    char vals[4];
};


static unsigned char *status_port = (unsigned char*) 0x6a6a6a;

static void
print_status(char a,
             char b)
{
	*status_port = a;
}

static inline void
eabort(char a,
       char b)
{
    print_status(a, b);
    while (1)
        ;
}

static inline void notify_host(void)
{
    volatile uint32_t *p = (volatile uint32_t *) ((SBOX_BASE) + 0xAB28);
    *p = (*p) | 0x1;
}


/*
 * ----------------------------------------------------------------------------
 *  ELF Utility Functions
 * ----------------------------------------------------------------------------
 */

static errval_t
linear_alloc(void *s,
             genvaddr_t base,
             size_t size,
             uint32_t flags,
             void **ret)
{
    // round to base page size
    uint32_t npages = (size + BASE_PAGE_SIZE - 1) / BASE_PAGE_SIZE;

    *ret = (void *) phys_alloc_start;

    phys_alloc_start += npages * BASE_PAGE_SIZE;
    return SYS_ERR_OK;
}


static struct multiboot_modinfo *
multiboot_find_module(const char *basename)
{
    struct multiboot_modinfo *mod;
    mod = (struct multiboot_modinfo *) (uintptr_t) multiboot_info->mods_addr;

    for (size_t i = 0; i < multiboot_info->mods_count; i++) {
        const char *modname = strrchr((char *) (uintptr_t) mod[i].string, '/');

        if (modname == NULL) {
            modname = (char *) (uintptr_t) mod[i].string;
        } else {
            modname++;
        }

        if (!strncmp(modname, basename, strlen(basename))) {
            return &mod[i];
        }
    }

    return NULL;
}

static void
set_elf_headers(uintptr_t base)
{
    struct Elf64_Ehdr *head = (struct Elf64_Ehdr *) (base);

    multiboot_info->syms.elf.num = head->e_shnum;
    multiboot_info->syms.elf.size = head->e_shentsize;
    multiboot_info->syms.elf.addr = base + head->e_shoff;
    multiboot_info->syms.elf.shndx = head->e_shstrndx;
}

/*
 * ----------------------------------------------------------------------------
 *  Loader
 * ----------------------------------------------------------------------------
 */

static inline void 
ping(void)
{
	__asm__ __volatile__("movq $0x6a6a6a, %%rcx; movl $0x10AD10AD, (%%rcx)":::"rcx");
}

static inline void
bad(void)
{
	__asm__ __volatile__("movq $0x6a6a6a, %%rcx; movl $0xDEADDEAD, (%%rcx)":::"rcx");
}

/**
 * Entry point from boot.S
 * Long mode, paging and protected mode enabled
 *
 * \param magic         magic value
 * \param bootparam     pointer to struct boot param
 *
 */
int
loader(uint64_t magic,
       struct xeon_phi_boot_params *bp)
{
    errval_t err;

    print_status('S', '0');


    if (magic != K1OM_BOOT_MAGIC) {
        /* wrong value */
        eabort('E', '0');
    }

    if (((uintptr_t)bp)>>32) {
        /*
         * sanity check that the boot params has a value which is less than 4G,
         * since we store the boot params in a 32bit value
         */
        eabort('E', 'a');
    }

    multiboot_info = get_multiboot();


    print_status('S', '1');

    /*
     * Copy the multiboot image closer to the kernel
     */
    lpaddr_t mb_img_start = ROUND_UP((lpaddr_t )&_end_bootloader, BASE_PAGE_SIZE);
    memcpy((void *) mb_img_start, (void *) (uintptr_t)bp->cmdline_ptr, bp->cmdline_size);
    bp->cmdline_ptr = mb_img_start;

    mb_img_start = ROUND_UP(mb_img_start+bp->cmdline_size, 1<<20);
    lpaddr_t mb_img_orig = bp->ramdisk_image;
    if (mb_img_start + bp->ramdisk_size > bp->ramdisk_image) {
        mb_img_orig = bp->ramdisk_image + bp->ramdisk_size + BASE_PAGE_SIZE;
        memcpy((void *) mb_img_orig, (void *)(uintptr_t) bp->ramdisk_image, bp->ramdisk_size);
        print_status('C', '1');
    }

    memcpy((void *) mb_img_start, (void *) mb_img_orig, bp->ramdisk_size);


    bp->ramdisk_image = mb_img_start;
    /*
     * the multiboot does only stores the offsets within the multiboot image
     * thus we have to adjust the addresses in the multiboot info struct
     */
    struct multiboot_modinfo *mod;
    mod = (struct multiboot_modinfo *) (uintptr_t) multiboot_info->mods_addr;

    for (size_t i = 0; i < multiboot_info->mods_count; i++) {
        mod->mod_start += mb_img_start;
        mod->mod_end += mb_img_start;
        mod++;
    }
    print_status('S', '2');

    /* look up the kernel module */
    struct multiboot_modinfo *kernel;
    kernel = multiboot_find_module("cpu");
    if (kernel == NULL) {
        kernel = multiboot_find_module("kernel");
    }
    if (kernel == NULL) {
        eabort('E', '3');
    }

    /* set the start address where we can allocate ram */
/*
    phys_alloc_start = ROUND_UP(mb_img_start + bp->ramdisk_size,
                                BASE_PAGE_SIZE)+BASE_PAGE_SIZE;
*/

	/* KCH: TODO: this should not be hardcoded */
    phys_alloc_start = 0x1000000;
    
    bp->ramdisk_image = (uint32_t) mb_img_start;

    lpaddr_t kernel_start = phys_alloc_start;

    /* overwrite the cmd line with the one supplied by the host */
    multiboot_info->cmdline = bp->cmdline_ptr;
    multiboot_info->flags |= MULTIBOOT_INFO_FLAG_HAS_CMDLINE;

    /* we use the mem_lower and mem_upper for the mulitboot image location */

    multiboot_info->mem_lower = bp->ramdisk_image;
    multiboot_info->mem_upper = bp->ramdisk_image+bp->ramdisk_size;
    multiboot_info->flags |= MULTIBOOT_INFO_FLAG_HAS_MEMINFO;


    /* we use the config table to store the pointer to struct boot param */
    multiboot_info->config_table = (uint32_t)(uintptr_t)bp;
    multiboot_info->flags |= MULTIBOOT_INFO_FLAG_HAS_CONFIG;

    print_status('Z','Z');
    print_status(*(char*)(&bp->xeon_phi_id), *(char*)((&bp->xeon_phi_id) + 1));

    multiboot_info->xeon_phi_id = (uint8_t)bp->xeon_phi_id;

    err = elf64_load(EM_K1OM, linear_alloc, NULL, kernel->mod_start,
                     MULTIBOOT_MODULE_SIZE(*kernel), &kernel_entry, NULL, NULL,
                     NULL);

    if (err_is_fail(err)) {
        eabort('E', '5');
    }

    struct Elf64_Ehdr *cpu_head = (struct Elf64_Ehdr *) (uint64_t) kernel->mod_start;
    struct Elf64_Shdr *rela, *symtab, *symhead;

    symhead = (struct Elf64_Shdr *) (kernel->mod_start
            + (uintptr_t) cpu_head->e_shoff);

    genvaddr_t elfbase = elf_virtual_base64(cpu_head);

    rela = elf64_find_section_header_type(symhead, cpu_head->e_shnum, SHT_RELA);

    symtab = elf64_find_section_header_type(symhead, cpu_head->e_shnum, SHT_DYNSYM);

    if (!rela) { 
      print_status('N','1');
    }
    if (!symtab) { 
      print_status('N','2');
    }

#if 0
    elf64_relocate(
            kernel_start, elfbase,
            (struct Elf64_Rela *) (uintptr_t) (kernel->mod_start + rela->sh_offset),
            rela->sh_size,
            (struct Elf64_Sym *) (uintptr_t) (kernel->mod_start + symtab->sh_offset),
            symtab->sh_size, elfbase, (void *) kernel_start);
#endif
    kernel_entry = kernel_entry - elfbase + kernel_start;

#if 0
    set_elf_headers(kernel->mod_start);
#endif

    return kernel_entry;
}
