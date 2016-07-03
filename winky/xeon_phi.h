#ifndef __XEON_PHI_H__
#define __XEON_PHI_H__

/* 
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Kyle C. Hale <kh@u.northwestern.edu>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */




#define PHI_FB_CTRL_REG_ADDR 0xb8fa0

#define OUTPUT_AVAIL_REG_OFFSET 0x0
#define OUTPUT_DRAWN_REG_OFFSET 0x1
#define CHAR_REG_OFFSET         0x2
#define CURSOR_REG_OFFSET       0x3
#define LINE_REG_OFFSET         0x4

#define PHI_IORED_LO(x) (SBOX_APICRT0 + 8*(x))
#define PHI_IORED_HI(x) (SBOX_APICRT0 + 8*(x) + 4)

#define MIC_VENDOR_INTEL 0x8086
#define K1OM_MODEL 0x1
#define MIC_FAMILY 0xb

#define PHI_SBOX_BASE   0x8007D0000ULL
#define PHI_DBOX_BASE   0x8007C0000ULL
#define PHI_SBOX_SIZE (64*1024)
#define PHI_DBOX_SIZE PHI_SBOX_SIZE
#define PHI_BOOT_OK_REG 0xAB28

#ifndef __ASSEMBLER__

#ifndef NULL
#define NULL (void*)0
#endif

#include "phi_sbox_regs.h"

typedef enum {
    TYPE_NO_UPDATE = 0,
    TYPE_CHAR_DRAWN,
    TYPE_LINE_DRAWN,
    TYPE_CURSOR_UPDATE,
    TYPE_SCREEN_REDRAW,
    TYPE_CONSOLE_SHUTDOWN,
    TYPE_SCROLLUP,
    TYPE_INVAL
} update_type_t;

#define PHI_ETC_TICK_RATE 15625000
#define PHI_NANOS_PER_TICK (1000000000/PHI_ETC_TICK_RATE)

#ifndef __packed
#define __packed __attribute__((packed))
#endif


typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int size_t;


union sboxScratch4RegDef
{
	uint32_t m_value;
	struct {
		uint32_t thread_mask : 4;
		uint32_t cache_size : 2;
		uint32_t gbox_channel_count : 4;
		uint32_t :15;
		uint32_t icc_divider : 5;
		uint32_t soft_reset : 1;
		uint32_t internal_flash : 1;
	} __packed;
} __packed;


typedef union _sboxElapsedTimeLowReg {
	uint32_t value;
	struct {
		uint32_t elapsed_time_low : 32;
	} __packed bits;
} __packed sboxElapsedTimeLowReg;

typedef union _sboxElapsedTimeHighReg {
	uint32_t value;
	struct {
		uint32_t elapsed_time_high : 32;
	} __packed bits;
} __packed sboxElapsedTimeHighReg;

typedef union _sboxPcieVendorIdDeviceIdReg
{
	uint32_t value;
	struct {
		uint32_t vendor_id : 16;
		uint32_t device_id : 16;
	} __packed bits;
} __packed sboxPcieVendorIdDeviceIdReg;



/* register read and write from SBOX MMIO space */
static inline uint32_t
phi_sbox_read (uint32_t offset)
{
    return *(volatile uint32_t*)((volatile uint32_t*) (PHI_SBOX_BASE + offset));
}

static inline void
phi_sbox_write (uint32_t offset, uint32_t val)
{
    *(volatile uint32_t*)((volatile uint32_t*)(PHI_SBOX_BASE + offset)) = val;
}


static inline uint64_t phi_etc_read (void) 
{
	sboxElapsedTimeLowReg low1Reg, low2Reg;
	sboxElapsedTimeHighReg hi1Reg, hi2Reg;

	do {
		hi1Reg.value = phi_sbox_read(SBOX_ELAPSED_TIME_HIGH);
		low1Reg.value = phi_sbox_read(SBOX_ELAPSED_TIME_LOW);
		hi2Reg.value = phi_sbox_read(SBOX_ELAPSED_TIME_HIGH);
	} while (hi1Reg.value != hi2Reg.value);
	return ((uint64_t)(((uint64_t)hi1Reg.value << 32) | low1Reg.value));
}

static inline uint64_t phi_etc_get_cycles (void)
{
	return phi_etc_read() >> 5;
}



int phi_map_mmio_range(void* (*os_map_iomem)(void* paddr, unsigned long size));
int phi_get_sku(void);
int phi_get_vendor(void);
void phi_init(void);
void phi_notify_boot_ok(void);

/* CONSOLE FUNCTIONS */
void phi_notify_redraw(void);
void phi_notify_scrollup(void);
void phi_send_cons_shutdown(void);
void phi_notify_char_write(uint16_t x, uint16_t y);
void phi_notify_line_draw (unsigned row);
/* ! CONSOLE FUNCTIONS */

/* UTILITY FUNCTIONS */
static inline void 
udelay (unsigned n) 
{
	unsigned cycles = n * 1100;
	asm volatile ("movl %0, %%eax; delay %%eax;"
					: /* no output */
					: "r" (cycles)
					: "eax");
}


#endif /* !__ASSEMBLER__! */


#endif
