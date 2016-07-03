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
#ifndef __PHI_SBOX_H__
#define __PHI_SBOX_H__

#define SBOX_APICICR0 0x0000A9D0
#define SBOX_APICICR7 0x0000AA08
#define SBOX_SICE0 0x0000900C
#define WAKEUP_INT_IDX 0
#define BACKTRACE_INT_IDX 1


#define SBOX_SICE0_DBR(x) ((x) & 0xf)
#define SBOX_SICE0_DBR_BITS(x) ((x) & 0xf)
#define SBOX_SICE0_DMA(x) (((x) >> 8) & 0xff)
#define SBOX_SICE0_DMA_BITS(x) (((x) & 0xff) << 8)

static inline void 
sbox_write (void * sbox, uint32_t offset, uint32_t value)
{
	*(volatile uint32_t*)(sbox + offset) = value;
}

static inline uint32_t 
sbox_read (void * sbox, uint32_t offset)
{
	return *(volatile uint32_t*)(sbox + offset);
}

#endif
