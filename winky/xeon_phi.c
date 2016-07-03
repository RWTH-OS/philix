#include "xeon_phi.h"


/* FILL THE BELOW IN */

/* put any include files you need here */
#include "cga.h"

/* The OS mapping funciton 
 * you should set this as whatever function your OS uses to 
 * map IO memory (i.e. uncachable memory)
 */
#define OS_MAP_IOMEM NULL

/* 
 * put your OS's appropriate printing functions here, e.g.
 * printk(fmt, ##args)
 *
 */
#define PHI_PRINT(fmt, args...) term_print(fmt)
#define PHI_DEBUG(fmt, args...) term_print(fmt)
#define PHI_ERROR(fmt, args...) term_print(fmt)


static int knc_stepping = 0;

int
phi_map_mmio_range (void* (*os_map_iomem)(void* paddr, unsigned long size))
{
	if (!os_map_iomem) {
		return 0;
	}

	if (!os_map_iomem((void*)PHI_SBOX_BASE, PHI_SBOX_SIZE)) {
		PHI_ERROR("Could not map SBOX MMIO\n");
		return -1;
	}

	return 0;
}


int 
phi_get_vendor (void)
{
	sboxPcieVendorIdDeviceIdReg reg;
	int vendor = 0;

	if ((reg.value = phi_sbox_read(SBOX_PCIE_VENDOR_ID_DEVICE_ID))) {
		vendor = reg.bits.vendor_id;
		return vendor;
	}

	return -1;
}


int 
phi_get_sku (void)
{
	sboxPcieVendorIdDeviceIdReg reg;
	int skuid = 0;

	if ((reg.value = phi_sbox_read(SBOX_PCIE_VENDOR_ID_DEVICE_ID))) {
		skuid = reg.bits.device_id & 0xf;
		return skuid;
	}

	return -1;
}


void 
phi_init (void)
{
	// map the IO space
	phi_map_mmio_range(OS_MAP_IOMEM);

	PHI_PRINT("Xeon Phi Initializing\n");
}


void
phi_notify_boot_ok (void)
{
	phi_sbox_write(PHI_BOOT_OK_REG, 1);
}


/* BEGIN CONSOLE SPECIFIC FUNCTIONS */

static inline uint32_t
phi_cons_read_reg(uint32_t off)
{
   uint32_t* addr = (uint32_t*)PHI_FB_CTRL_REG_ADDR;
   return *(volatile uint32_t*)(addr+off);
}


static inline void 
phi_cons_write_reg (uint32_t off, uint32_t val)
{
    uint32_t* addr = (uint32_t*)PHI_FB_CTRL_REG_ADDR;
    *(volatile uint32_t*)(addr+off) = val;
}


static void
wait_for_out_cmpl (void)
{
	while (phi_cons_read_reg(OUTPUT_DRAWN_REG_OFFSET) == 0);
	phi_cons_write_reg(OUTPUT_DRAWN_REG_OFFSET, 0);
}


void 
phi_send_cons_shutdown (void)
{
	phi_cons_write_reg(OUTPUT_AVAIL_REG_OFFSET, TYPE_CONSOLE_SHUTDOWN);
	wait_for_out_cmpl();
}


void
phi_notify_redraw (void)
{
    phi_cons_write_reg(OUTPUT_AVAIL_REG_OFFSET, TYPE_SCREEN_REDRAW);

    wait_for_out_cmpl();
}


/* tell host to save the first line in its buffer if it needs to */
void
phi_notify_scrollup (void)
{
    phi_cons_write_reg(OUTPUT_AVAIL_REG_OFFSET, TYPE_SCROLLUP);

    wait_for_out_cmpl();
}


void 
phi_notify_char_write (uint16_t x, uint16_t y)
{
    uint32_t coords = (uint32_t)(x | y << 16);

    /* where did we draw the char? */
    phi_cons_write_reg(CHAR_REG_OFFSET, coords);

    /* we have output ready to be drawn */
    phi_cons_write_reg(OUTPUT_AVAIL_REG_OFFSET, TYPE_CHAR_DRAWN);

    wait_for_out_cmpl();
}


void 
phi_notify_line_draw (unsigned row)
{
    /* which row did we write to? */
    phi_cons_write_reg(LINE_REG_OFFSET, row);

    /* we have output ready to be drawn */
    phi_cons_write_reg(OUTPUT_AVAIL_REG_OFFSET, TYPE_LINE_DRAWN);

    wait_for_out_cmpl();
}

