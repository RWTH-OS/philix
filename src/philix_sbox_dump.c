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
 * Tool to dump the SBOX registers on the Xeon Phi
 *
 * This utility has been tested with Intel's MPSS version 3.4.2
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <curses.h>
#include <ctype.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <linux/kd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <assert.h>



// We only need a few megs of it though
#define PHI_GDDR_RANGE_SZ (1024*64)

#define MEM_BASE_PATH "/sys/class/mic/mic"
#define MEM_FILE      "/device/resource4"


#define PHI_BASE_PATH "/sys/class/mic/mic"
#define PHI_STATE_FILE "/state"


static char* micnum = "1";


static void*
map_phi (void)
{
    int memfd;
    unsigned long long range_sz = PHI_GDDR_RANGE_SZ;
    char mempath[128];
    
    memset(mempath, 0, sizeof(mempath));

    strcat(mempath, MEM_BASE_PATH);
    strcat(mempath, micnum);
    strcat(mempath, MEM_FILE);

    if (!(memfd = open(mempath, O_RDWR | O_SYNC))) {
        fprintf(stderr, "Error opening file %s\n", mempath);
        return NULL;
    }

    printf("Mapping MIC %s GDDR memory range (%llu bytes)\n",
            micnum, range_sz);

#define SBOX_BASE 0x10000
#define SBOX_SIZE (1024*64)
    void * buf = mmap(NULL, 
                      range_sz,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED, 
                      memfd,
                      SBOX_BASE);

    if (buf == MAP_FAILED) {
        fprintf(stderr, "Couldn't map PHI MMIO range\n");
        return NULL;
    }

    return buf;
}


int 
main (int argc, char ** argv)
{
	volatile uint32_t * buf = NULL;
	int offset;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <micnum>\n", argv[0]);
		exit(0);
	}

	micnum = argv[1];

    buf = map_phi();

	for (offset = 0; offset < SBOX_SIZE; offset+=16, buf+=4) {
		printf("0x%08x: %08x %08x %08x %08x\n", offset,
		*buf,
		*(buf+1),
		*(buf+2),
		*(buf+3));
	}

    return 0;
}

