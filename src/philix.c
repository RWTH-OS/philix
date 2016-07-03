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
 * Userspace Console for Nautilus on the Xeon Phi
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
#include <sys/stat.h>
#include <time.h>

#include <phi_linux.h>

#if DEBUG == 0
#define NODEBUG
#endif

#include <assert.h>

#include <phi_console.h>
#include <phi_mmap.h>
#include <phi_linux.h>
#include <phi_sbox.h>

static int dumponexit = 0;
static char * outfile = NULL;
static struct cons_struct console;

// default mic to use is mic0
static char* micnum = "0";

#if DEBUG == 1
#define DEBUG_PRINT(fmt, args...) fprintf(stderr, "DEBUG: " fmt, ##args);
#define DEBUG_SCREEN(fmt, args...) wprintw(console.windows.debug, fmt, ##args); wrefresh(console.windows.debug);
#else
#define DEBUG_PRINT(fmt, args...) 
#define DEBUG_SCREEN(fmt, args...)
#endif

#define POPUP_SHORT(fmt, args...)     \
({                                     \
	char _tmp[2048];                   \
	snprintf(_tmp, 2048, fmt, ##args); \
	show_notification_str(_tmp, 2);    \
})


static void
print_version (void)
{
	printf("philix " PACKAGE_VERSION "\n"
		   "Copyright (c) the V3VEE Project (v3vee.org) and the Hobbes Project (xstack.sandia.gov/hobbes).\n" 
		   "Released under the MIT License.\n\n" 
		   "Written by Kyle C. Hale (halek.co)\n\n");
}


static void
usage (char * prog)
{
	print_version();
    fprintf(stderr, "\nUsage: %s -b <bootloader> -k <kernel> [-m <micnum> ] [-o <file>] [-d] [-h] \n"
                    "\t-m    Which MIC device to use. Default is 0\n"
                    "\t-b    Path to the bootloader to boot with\n"
                    "\t-k    Path to the kernel to boot with\n"
                    "\t-o    Output to file (default is %s-datetime.log)\n"
					"\t-d    Dump output to specified output file on exit/interrupt\n"
					"\t-n    Run in no curses (batch) mode\n"
				    "\t-v    Print the version number\n"
                    "\t-h    Print this message\n\n", prog, CONSOLE_OUTPUT_FILENAME);
    exit(EXIT_SUCCESS);
}



static void
sbox_interrupt_enable (void)
{
	uint32_t sice0 = sbox_read(console.sbox, SBOX_SICE0);
	sice0 |= SBOX_SICE0_DBR_BITS(0xf) | SBOX_SICE0_DMA_BITS(0xff);
	sbox_write(console.sbox, SBOX_SICE0, sice0);
}


static void
sbox_send_int (uint8_t int_idx)
{
	uint32_t apicicr_low;
	uint64_t apic_icr_offset = SBOX_APICICR0 + int_idx * 8;

	apicicr_low = sbox_read(console.sbox, apic_icr_offset) | (1<<13);
	sbox_write(console.sbox, apic_icr_offset, apicicr_low);
}


static inline void 
sbox_send_wakeup (void)
{
	sbox_send_int(WAKEUP_INT_IDX);
}


static inline uint32_t 
console_read_reg (uint32_t reg)
{
    assert(console.ctrl_regs);
    return *(volatile uint32_t*)(console.ctrl_regs + reg);
}


static inline void
console_write_reg (uint32_t reg, uint32_t val)
{
    assert(console.ctrl_regs);
    *(volatile uint32_t*)(console.ctrl_regs + reg) = val;
    msync(console.gddr, console.gddr_size, MS_SYNC);
}



#define MAX_OUTFILE_NMSZ 128

static void 
output_fb_to_file (FILE * stream)
{
    int i,j;

	for (i = 0; i < PHI_FB_HEIGHT; i++) {
		for (j = 0; j < PHI_FB_WIDTH; j++) {

			fputc(*(char*)(console.fb + i*PHI_FB_WIDTH + j), stream);
		}
		fputc('\n', stream);
	}
}

static void
output_hist_to_file (FILE * stream)
{
	struct line_elm * cur = console.line_hist;
	int i;

	while (cur->next) {
		cur = cur->next;
	}
	
	while (cur != console.line_hist) {
		char * cursor = (char*)cur->line;
		
		for (i = 0; i < PHI_FB_WIDTH; i++) {
			fputc(*(cursor+2*i), stream);
		}
	
		fputc('\n', stream);
		
		cur = cur->prev;
	}
}




static update_type_t
wait_for_cons_update (void)
{
    update_type_t res;
    unsigned delay_cnt = 1000;

    while ((res = console_read_reg(OUTPUT_AVAIL_REG_OFFSET)) == TYPE_NO_UPDATE &&
           delay_cnt--) {
		usleep(100);
    }

    return res;
}


static void 
draw_done (void)
{
    console_write_reg(OUTPUT_DRAWN_REG_OFFSET, 1);
}


static int 
check_terminal_size (void)
{
    uint16_t ncols = 0;
    uint16_t nrows = 0;
    struct winsize winsz;

    ioctl(fileno(stdin), TIOCGWINSZ, &winsz);
    ncols = winsz.ws_col;
    nrows = winsz.ws_row;

    if (ncols < MIN_TTY_COLS || nrows < MIN_TTY_ROWS)
    {
        printf("Your window is not large enough.\n");
        printf("It must be at least %ux%u, but yours is %ux%u\n", 
				MIN_TTY_COLS, MIN_TTY_ROWS, ncols, nrows);
        return -1;
    }

    return 0;
}


/* NOTE: this console will support only 16 colors */
static void
init_colors (void)
{
    start_color();
    int i;
    for (i = 0; i < 0x100; i++) {
        init_pair(i, i & 0xf, (i >> 4) & 0xf);
    }
}


static inline uint16_t
get_cursor_x (void)
{
    return console.x;
}


static inline uint16_t
get_cursor_y (void)
{
    return console.y;
}


static int
handle_cursor_update (void)
{
    /* TODO */
    //wmove(console.win, y, x);
    return 0;
}


static int
handle_char_update (void)
{
    uint32_t cinfo = console_read_reg(CHAR_REG_OFFSET);
    uint16_t x     = cinfo & 0xff;
    uint16_t y     = cinfo >> 16;

    assert(console.fb);

    uint16_t entry = console.fb[y*PHI_FB_WIDTH+x];
    char c      = entry & 0xff;
    uint8_t color  = entry >> 8; 

    DEBUG_SCREEN("Handling char update '%c' @(%u, %u)\n", c, x, y);

    wattron(console.windows.win, COLOR_PAIR(color));
    /* draw the character */
    mvwaddch(console.windows.win, y, x, isprint(c) ? c : '@');
    wattroff(console.windows.win, COLOR_PAIR(color));

    /* display it */
    wrefresh(console.windows.win);

    return 0;
}

static void 
draw_line_buf (uint16_t * buf, unsigned row) 
{
    assert(row < PHI_FB_HEIGHT);
    int i;
    for (i = 0; i < PHI_FB_WIDTH; i++) {
        char c     = buf[i] & 0xff;
        char color = buf[i] >> 8;
        wattron(console.windows.win, COLOR_PAIR(color));
        mvwaddch(console.windows.win, 
                row, 
                i, 
                isprint(c) ? c : '@');
        wattroff(console.windows.win, COLOR_PAIR(color));
    }
}


static void 
draw_fb (unsigned offset, unsigned nlines)
{
    assert(nlines <= PHI_FB_HEIGHT);
    assert(offset < PHI_FB_HEIGHT);
    assert(nlines + offset <= PHI_FB_HEIGHT);
    int i;
    DEBUG_SCREEN("Draw  fb offset:%u nlines:%u\n", offset, nlines);

    for (i = 0; i < nlines; i++) {
        draw_line_buf((uint16_t*)(console.fb + i*PHI_FB_WIDTH), offset++);
    }
}


static void
draw_hist_buf (struct line_elm * head, 
               unsigned offset,
               unsigned nlines)
{
    assert(nlines <= PHI_FB_HEIGHT);
    assert(offset < PHI_FB_HEIGHT);
    assert(nlines + offset <= PHI_FB_HEIGHT);

    DEBUG_SCREEN("Draw hist buf offset:%u nlines:%u\n", offset, nlines);

    int i;
    for (i = 0; i < nlines && head != NULL && head != console.line_hist; i++) {
        /* copy this line bufer into the window at offset n */
        draw_line_buf(head->line, offset++);
        head = head->prev;
    }
}


/* 
 * move our window around in the history buffer.
 * an offset of 0 means that we're not including any
 * of the history buffer lines in the output window 
 */
static void
scroll_output_win (int nlines)
{
    struct line_elm * line = (console.win_ptr == NULL) ? console.line_hist : console.win_ptr;
    int scroll = 0;

    assert(line);

    /* move the window */
    if (nlines > 0) { 

        if (console.line_idx >= MAX_HIST_LINES) return;


        while (scroll != nlines && 
                console.line_idx < MAX_HIST_LINES && 
                console.line_idx < console.hist_cnt) {

            scroll++;
            console.line_idx++;

            if (line->next) {
                line = line->next;
            } else {
                break;
            }
        
        }


        console.win_ptr = line;
        
    } else if (nlines < 0) {

        if (console.line_idx == 0) return;
        
        while (scroll != nlines && line && console.line_idx > 0) {
            line = line->prev;
            scroll--;
            console.line_idx--;
        }

        console.win_ptr = line;
        
    } 

    DEBUG_SCREEN("Tried %d Scrolled %d lines, line idx is now %u\n", nlines, scroll, console.line_idx);

}

static void 
scroll_top (void) 
{
    if (console.line_hist && console.line_hist->prev) {
        console.win_ptr = console.line_hist->prev;
        console.line_idx = console.hist_cnt;
    } 
}

static void
scroll_bottom (void)
{
    console.win_ptr = NULL;
    console.line_idx = 0;
}


static void
draw_output_win (void)
{

    /* only need to draw FB */
    if (console.line_idx == 0) {
        DEBUG_SCREEN("drawing only fb\n");

        draw_fb(0, PHI_FB_HEIGHT);

    /* only need to draw history buf */
    } else if (console.line_idx >= PHI_FB_HEIGHT) {
        DEBUG_SCREEN("drawing only history\n");

        draw_hist_buf(console.win_ptr, 0, PHI_FB_HEIGHT);

    /* need to draw from both */
    } else {

        DEBUG_SCREEN("drawing mix\n");
        draw_hist_buf(console.win_ptr, 0, console.line_idx);
        draw_fb(console.line_idx, PHI_FB_HEIGHT - console.line_idx);

    }

    wrefresh(console.windows.win);
}

static void
show_notification_str (const char * str, unsigned secs)
{
    unsigned maxx, maxy, textlen, boxwidth, boxheight, textwidth;
    WINDOW * w = NULL;
    int col;
    int row;
#define MIN_POPUP_LINES 4

	// only do this if we're in curses mode
	if (!console.curses_enabled) {
		return;
	}

	getmaxyx(stdscr, maxy, maxx);
    
	// half of the window
    boxwidth = (maxx + 1) / 2;

    textwidth = boxwidth - 2; // one space on each side for padding

    textlen = strlen(str);
    boxheight = MIN_POPUP_LINES + (textlen / textwidth) + (textlen % textwidth ? 1 : 0);

	// center the new window 
	w = newwin(boxheight, boxwidth, (maxy / 2) , (maxx/2)-(boxwidth/2));
    if (!w) { 
        endwin();
        fprintf(stderr, "Could not display window (%s)\n", strerror(errno));
        exit(1);
    }

	//wclear(console.windows.win);
	wclear(w);
	box(w, 0, 0);

	row = 1;

	if (textlen < textwidth) {
		col = (boxwidth/2) - (textlen/2);
	} else {
		col = 2;
	}

    // draw the string
    while (*str) {

        if (col == textwidth-2) {
			if (strlen(str) < textwidth) {
				col = (boxwidth/2) - (strlen(str)/2);
			} else {
				col = 2;
			}
            row++;
        }
        
        mvwaddch(w, row, col, *str);

        ++str;
		++col;
    }

	wrefresh(w);
    sleep(secs);
	wclear(w);
	wrefresh(w);
    delwin(w);
	wrefresh(console.windows.win);
	wrefresh(console.windows.phi_info);
	wrefresh(console.windows.phi_info_border);
}


/* TODO: this will eventually include ability to output to a TTY */
static void
dump_console_to_file (void)
{
	FILE * fd = NULL;
	struct line_elm * cur = console.line_hist;
	char name[128];
	memset(name, 0, sizeof(name));
	char * nm = NULL;

	if (!outfile) {
		time_t ltime = time(NULL);
		struct tm * t;
		t = localtime(&ltime);
		sprintf(name, CONSOLE_OUTPUT_FILENAME "-%02d-%02u-%04u-%02u-%02u-%02u.log",  t->tm_mon+1,
				t->tm_mday,
				t->tm_year+1900,
				t->tm_hour, t->tm_min, t->tm_sec);
		nm = name;
		
		POPUP_SHORT("Dump using default filename (%s)", name);

	} else {
		nm = outfile;
	}

	if (!(fd = fopen(nm, "w+"))) {
		fprintf(stderr, "Could not open file %s\n", nm);
		exit(1);
	}
	
	/* seek to the end */
	while (cur->next) {
		cur = cur->next;
	}

	output_hist_to_file(fd);
	output_fb_to_file(fd);

	fclose(fd);
	DEBUG_SCREEN("File dump complete.\n");
}


static int
handle_line_update (void)
{
    uint32_t row = console_read_reg(LINE_REG_OFFSET);

    assert(row < PHI_FB_HEIGHT);
    assert(console.fb);

    DEBUG_SCREEN("Handling line update for line %u\n", row);
    
    draw_output_win();

	return 0;
}


/* dir = 1 => forward 
 * dir = 0 => backward
 */
static void 
scroll_page (uint8_t dir)
{
    if (dir == 1) {
        scroll_output_win(PHI_FB_HEIGHT);
    } else if (dir == 0) {
        scroll_output_win(-PHI_FB_HEIGHT);
    }
}


/* save the first line, it's about to go away */
static int
handle_scrollup (void)
{
    int i;
    assert(console.fb);

    /* make a new history line elm */
    struct line_elm * new = malloc(sizeof(struct line_elm));
    if (!new) {
        fprintf(stderr, "Could not allocate new line\n");
        return -1;
    }
    memset(new, 0, sizeof(struct line_elm));

    for (i = 0; i < PHI_FB_WIDTH; i++) {
        new->line[i] = console.fb[i];
    }
    
    /* kill the last one */
    if (console.hist_cnt == MAX_HIST_LINES) {

        assert(console.line_hist->prev);
        assert(console.line_hist->prev->prev);

        new->prev = console.line_hist->prev->prev;
        console.line_hist->prev->prev->next = NULL;

        free(console.line_hist->prev);

    } else {

        if (console.line_hist) {
            new->prev = console.line_hist->prev;
        } else {
            new->prev = new;
        }

        console.hist_cnt++;
    }

    if (console.line_hist) {
        console.line_hist->prev = new;
    } 

    /* if this is the first one, we're going to assign NULL anyhow */
    new->next = console.line_hist;

    /* add to the head of the list */
    console.line_hist = new;
    
    DEBUG_SCREEN("Handling scrollup: histcount=%u\n", console.hist_cnt);
	return 0;
}


static int
handle_screen_redraw (void)
{
    assert(console.fb);

    DEBUG_SCREEN("Handling screen redraw\n");

    draw_output_win();

	return 0;
}


static int
nocurses_main_loop (void)
{
    while (1) {

		update_type_t update = wait_for_cons_update();

		switch (update) {

			case TYPE_NO_UPDATE: 
			case TYPE_CHAR_DRAWN: 
			case TYPE_LINE_DRAWN:
			case TYPE_SCREEN_REDRAW:
			case TYPE_CURSOR_UPDATE:  /* TODO */
				break;
			case TYPE_CONSOLE_SHUTDOWN:
				printf("Received console shutdown signal\n");
				console_write_reg(OUTPUT_AVAIL_REG_OFFSET, 0);
				draw_done();
				dump_console_to_file();
				exit(0);
				break;
			case TYPE_SCROLLUP:
				if (handle_scrollup() != 0) {
					fprintf(stderr, "Error handling scrollup\n");
					return -1;
				}
				break;
			default: 
				fprintf(stderr, "Unknown update type (0x%x)\n", update);
				return -1;
		}

		console_write_reg(OUTPUT_AVAIL_REG_OFFSET, 0);
		draw_done();

	}


    printf("Console terminated\n");
    return 0;
}


static int
curses_main_loop (void)
{
    while (1) {

		update_type_t update = wait_for_cons_update();

		switch (update) {

			case TYPE_NO_UPDATE: 
				break;
			case TYPE_CHAR_DRAWN: 
				if (handle_char_update() != 0) {
					fprintf(stderr, "Error handling character update\n");
					return -1;
				}
				break;
			case TYPE_LINE_DRAWN:
				if (handle_line_update() != 0) {
					fprintf(stderr, "Error handling line update\n");
					return -1;
				}
				break;
			case TYPE_SCREEN_REDRAW:
				if (handle_screen_redraw() != 0) {
					fprintf(stderr, "Error handling screen redraw\n");
					return -1;
				}
				break;
			case TYPE_CURSOR_UPDATE:  /* TODO */
				if (handle_cursor_update() != 0) {
					fprintf(stderr, "Error handling cursor update\n");
					return -1;
				}
				break;
			case TYPE_CONSOLE_SHUTDOWN:
				DEBUG_SCREEN("Received console shutdown signal\n");
				console_write_reg(OUTPUT_AVAIL_REG_OFFSET, 0);
				draw_done();
				if (dumponexit) {
					dump_console_to_file();
				}
				exit(0);
				break;
			case TYPE_SCROLLUP:
				if (handle_scrollup() != 0) {
					fprintf(stderr, "Error handling scrollup\n");
					return -1;
				}
				break;
			default: 
				fprintf(stderr, "Unknown update type (0x%x)\n", update);
				return -1;
		}

		console_write_reg(OUTPUT_AVAIL_REG_OFFSET, 0);
		draw_done();

		int c = wgetch(console.windows.win);

		switch (c) {
			case KEY_LEFT:
				DEBUG_SCREEN("history count: %u\n", console.hist_cnt);
				break;
			case KEY_UP:
				DEBUG_SCREEN("Scrolling back one line\n");
				scroll_output_win(1);
				draw_output_win();
				break;
			case KEY_DOWN:
				DEBUG_SCREEN("Scrolling forward one line\n");
				scroll_output_win(-1);
				draw_output_win();
				break;
			case KEY_NPAGE:
				DEBUG_SCREEN("Scrolling forward one page\n");
				scroll_page(0);
				draw_output_win();
				break;
			case KEY_PPAGE:
				DEBUG_SCREEN("Scrolling back one page\n");
				scroll_page(1);
				draw_output_win();
				break;
			case 'g': 
				DEBUG_SCREEN("Scrolling to beginning\n");
				scroll_top();
				draw_output_win();
				break;
			case 'G': 
				DEBUG_SCREEN("Scrolling to end\n");
				scroll_bottom();
				draw_output_win();
				break;
			case 'p':
				DEBUG_SCREEN("Sending wakeup interrupt\n");
				sbox_interrupt_enable();
				sbox_send_wakeup();
				break;
			case 'b':
				DEBUG_SCREEN("Sending backtrace interrupt\n");
				sbox_interrupt_enable();
				sbox_send_int(BACKTRACE_INT_IDX);
				break;
			case 'o':
				DEBUG_SCREEN("Dumping output to file\n");	
				dump_console_to_file();
				break;
			case 'h':
				DEBUG_SCREEN("Concat_core_status: 0x%llx\n", sbox_read(console.sbox, 0xac0c));
				break;
			default:
				break;

		}
	}

	erase();

    printf("Console terminated\n");
    return 0;
}


static int
console_setup_window (WINDOW * w)
{
    if (scrollok(w, 1) != OK) {
        fprintf(stderr, "Problem setting scrolling\n");
        return -1;
    }

    if (erase() != OK) {
        fprintf(stderr, "Problem erasing screen\n");
        return -1;
    }

    if (cbreak() != OK) {
        fprintf(stderr, "Error putting terminal in raw mode\n");
        return -1;
    }

    if (noecho() != OK) {
        fprintf(stderr, "Error setting noecho\n");
        return -1;
    }

    if (keypad(w, TRUE) != OK) {
        fprintf(stderr, "Error setting keypad options\n");
        return -1;
    }

    /* set non-blocking read for keyboard with no timer */
    wtimeout(w, 0);

    return 0;
}


static void
reset_phi (void)
{
    char cmd_str[128];
    sprintf(cmd_str, "micctrl --reset mic%s\n", micnum);
    system(cmd_str);
}


/* 
 * this will use Intel's Xeon Phi driver sysfs interface
 * to initiate a boot on the card using a custom image
 *
 */
static int
boot_phi (const char * bpath, const char * kpath)
{
    char boot_str[128];
    char phi_path[128];
    char phi_state[64];
    struct stat s;
    FILE* fd = 0;

    /* first stat the kernel and bootloader to make sure they exist */
    if (stat(bpath, &s) != 0) {
        fprintf(stderr, "Invalid bootloader file %s (%s)\n", bpath, strerror(errno));
        return -1;
    }

    if (stat(kpath, &s) != 0) {
        fprintf(stderr, "Invalid kernel file %s (%s)\n", kpath, strerror(errno));
        return -1;
    }

    memset(boot_str, 0, sizeof(boot_str));
    strcat(boot_str, PHI_BOOT_STR);
    strcat(boot_str, bpath);
    strcat(boot_str, ":");
    strcat(boot_str, kpath);

    memset(phi_path, 0, sizeof(phi_path));

    strcat(phi_path, PHI_BASE_PATH);
    strcat(phi_path, micnum);
    strcat(phi_path, PHI_STATE_FILE);

    fd = fopen(phi_path, "r+");

    if (fd < 0) {
        fprintf(stderr, "Couldn't open file %s (%s) (Do you have Intel's MPSS toolchain installed?)\n", phi_path,
                strerror(errno));
        return -1;
    }

    fscanf(fd, "%s", phi_state);

    /* Does this Phi need to be reset? */
    if (strncmp(phi_state, "ready", 5) != 0) {

        reset_phi();

        while (strncmp(phi_state, "ready", 5) != 0) {
            rewind(fd);
            memset(phi_state, 0, sizeof(phi_state));
            fscanf(fd, "%s", phi_state);
            usleep(100);
        }
    }

    rewind(fd);

    if (fwrite(boot_str, 1, strlen(boot_str), fd) < 0) {
        fprintf(stderr, "Couldnt write file %s (%s)\n", phi_path, strerror(errno));
        return -1;
    }

    fclose(fd);
    return 0;
}


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

    void * buf = mmap(NULL, 
                      range_sz,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED, 
                      memfd,
                      0);

    DEBUG_PRINT("Mapped it into host process at %p\n", buf);

    if (buf == MAP_FAILED) {
        fprintf(stderr, "Couldn't map PHI MMIO range\n");
        return NULL;
    }


    return buf;
}


static void*
map_phi_sbox (void)
{
    int sboxfd;
    unsigned long long range_sz = SBOX_RANGE_SZ;
	char sboxpath[128];
    
	memset(sboxpath, 0, sizeof(sboxpath));
	strcat(sboxpath, MEM_BASE_PATH);
	strcat(sboxpath, micnum);
	strcat(sboxpath, SBOX_FILE);

    if (!(sboxfd = open(sboxpath, O_RDWR | O_SYNC))) {
        fprintf(stderr, "Error opening file %s\n", sboxpath);
        return NULL;
    }

    printf("Mapping MIC %s SBOX memory range (%llu bytes)\n",
            micnum, range_sz);

    void * buf = mmap(NULL, 
                      range_sz,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED, 
                      sboxfd,
                      SBOX_OFFSET);

    DEBUG_PRINT("Mapped it into host process at %p\n", buf);

    if (buf == MAP_FAILED) {
        fprintf(stderr, "Couldn't map PHI SBOX range\n");
        return NULL;
    }


    return buf;
}


static void
curses_sigint_handler (int sig)
{
    wclear(console.windows.win);
    mvwaddstr(console.windows.win, 0,0, "Caught ^C, exiting and resetting MIC\n");
    wrefresh(console.windows.win);
	exit(0);
}


static void
nocurses_sigint_handler (int sig)
{
	dump_console_to_file();
	reset_phi();
}


static void
curses_sigsegv_handler (int sig)
{
    wclear(console.windows.win);
    mvwaddstr(console.windows.win,0,0, "Caught SIGSEGV, exiting and resetting MIC\n");
    wrefresh(console.windows.win);
    exit(0);
}

static void
nocurses_sigsegv_handler (int sig)
{
	reset_phi();
}


static void
curses_handle_exit(void)
{
    endwin();
    printf("Phi Console Shutdown. Resetting device...\n");
    fflush(stdout);
    reset_phi();
}


static void
nocurses_handle_exit(void)
{
    printf("Phi Console Shutdown. Resetting device...\n");
    fflush(stdout);
    reset_phi();
}


static void 
console_init (void * mapped_phi_gddr, int nocurses)
{
    console.x         = 0;
    console.y         = 0;
    console.rows      = PHI_FB_HEIGHT;
    console.cols      = PHI_FB_WIDTH;
    console.gddr      = mapped_phi_gddr;
    console.gddr_size = PHI_GDDR_RANGE_SZ;
    console.fb        = (void*)( ((char*)mapped_phi_gddr) + PHI_FB_ADDR );
    console.ctrl_regs = (void*)( ((char*)mapped_phi_gddr) + PHI_FB_ADDR + PHI_FB_REGS_OFFSET );


    console.line_idx = 0;
    console.line_hist = NULL;
    console.win_ptr  = NULL;
    console.hist_cnt = 0;

	DEBUG_PRINT("Console framebuffer at %p\n", (void*)console.fb);
	DEBUG_PRINT("Console ctrl_regs at %p\n", (void*)console.ctrl_regs);

	if (nocurses) {
		console.cops.main_loop   = nocurses_main_loop;
		console.cops.sigsegv     = nocurses_sigsegv_handler;
		console.cops.sigint      = nocurses_sigint_handler;
		console.cops.handle_exit = nocurses_handle_exit;
	} else  {
		console.cops.main_loop   = curses_main_loop;
		console.cops.sigsegv     = curses_sigsegv_handler;
		console.cops.sigint      = curses_sigint_handler;
		console.cops.handle_exit = curses_handle_exit;
	}
	
    atexit(console.cops.handle_exit);
    /* reset the phi on exit */
    signal(SIGINT, console.cops.sigint);
    signal(SIGSEGV, console.cops.sigsegv);
}


static void
setup_screen (void)
{
	int phix, phiy, maxx, maxy;

	// create the screen
	tcgetattr(fileno(stdin), &console.termios_old);
	initscr();
	curs_set(FALSE);
	init_colors();
	getmaxyx(stdscr, maxy, maxx);

	// we're in curses mode now
	console.curses_enabled = 1;

#if DEBUG == 1
	console.windows.win = newwin(maxy - DEBUG_SCREEN_LINES - INFO_WIN_LINES, maxx, 0, 0);
#else 
	console.windows.win = newwin(maxy - INFO_WIN_LINES, maxx, 0, 0);
#endif

	if (!console.windows.win) {
		fprintf(stderr, "Error initializing curses screen\n");
		exit(EXIT_FAILURE);
	}

	if (console_setup_window(console.windows.win) != 0) {
		fprintf(stderr, "Error setting up console\n");
		exit(EXIT_FAILURE);
	}

#if DEBUG == 1
	console.windows.phi_info_border = newwin(INFO_WIN_LINES, maxx, maxy - DEBUG_SCREEN_LINES - INFO_WIN_LINES, 0);
#else 
	console.windows.phi_info_border = newwin(INFO_WIN_LINES, maxx, maxy  - INFO_WIN_LINES, 0);
#endif

	box(console.windows.phi_info_border, 0, 0);
	mvwprintw(console.windows.phi_info_border, 0, (maxx/2) - 9, " CONNECTED TO MIC %u ", atoi(micnum));
	wrefresh(console.windows.phi_info_border);

#if DEBUG == 1
	console.windows.phi_info = newwin(INFO_WIN_LINES-2, maxx-4, maxy - DEBUG_SCREEN_LINES - INFO_WIN_LINES+1, 2);
#else
	console.windows.phi_info = newwin(INFO_WIN_LINES-2, maxx-4, maxy - INFO_WIN_LINES+1, 2);
#endif

	mvwprintw(console.windows.phi_info, 0, 0, "Up Arrow:   scroll back\n");
	mvwprintw(console.windows.phi_info, 1, 0, "Down Arrow: scroll forward\n");
	mvwprintw(console.windows.phi_info, 2, 0, "Page Up:    scroll back one page\n");
	mvwprintw(console.windows.phi_info, 3, 0, "Page Down:  scroll forward one page\n");
	mvwprintw(console.windows.phi_info, 4, 0, "Ctrl^C:     quit\n");

	getmaxyx(console.windows.phi_info, phiy, phix);
	mvwprintw(console.windows.phi_info, 0, phix/2, "g: scroll to beginning\n");
	mvwprintw(console.windows.phi_info, 1, phix/2, "G: scroll to end\n");
	mvwprintw(console.windows.phi_info, 2, phix/2, "p: Send wakeup signal to all Phi cores\n");
	mvwprintw(console.windows.phi_info, 3, phix/2, "b: Send backtrace command to all Phi cores\n");
	wrefresh(console.windows.phi_info);


#if DEBUG == 1
	console.windows.debug_border = newwin(DEBUG_SCREEN_LINES, maxx, maxy - DEBUG_SCREEN_LINES, 0);
	box(console.windows.debug_border, 0, 0);
	mvwprintw(console.windows.debug_border, 0, maxx/2 - 12, "PHI CONSOLE DEBUG WINDOW");
	wrefresh(console.windows.debug_border);

	console.windows.debug = newwin(DEBUG_SCREEN_LINES-2, maxx-4, maxy - DEBUG_SCREEN_LINES+ 1, 2);
	if (!console.windows.debug) {
		fprintf(stderr, "Could not create output screen\n");
		exit(EXIT_FAILURE);
	}

	console_setup_window(console.windows.debug);
#endif
}


static void 
teardown_screen (void)
{
#if DEBUG == 1
	delwin(console.windows.debug_border);
	delwin(console.windows.debug);
#endif

	delwin(console.windows.win);
	delwin(console.windows.phi_info);
	delwin(console.windows.phi_info_border);
	endwin();
}


int 
main (int argc, char ** argv)
{
    void * buf;
    int c;
	int nocurses = 0;
    char * bootloader = NULL;
    char * kernel = NULL;

    opterr = 0;

    while ((c = getopt(argc, argv, "vnhdm:b:k:o:")) != -1) {
        switch (c) {
            case 'h': 
                usage(argv[0]);
                break;
			case 'v':
				print_version();
				exit(0);
				break;
            case 'm': 
                micnum = optarg;
                break;
            case 'k':
                kernel = optarg;
                break;
            case 'b':
                bootloader = optarg;
                break;
			case 'o':
				outfile = optarg;
				break;
			case 'n':
				nocurses = 1;
				break;
			case 'd':
				dumponexit = 1;
				break;
            case '?':
                if (optopt == 'm' || optopt=='b' || optopt=='k' || optopt=='o') {
                    fprintf(stderr, "Option -%c requires argument\n", optopt);
                } else if (isprint(optopt)) {
                    fprintf(stderr, "Unknown option `-%c'\n", optopt);
                } else {
                    fprintf(stderr, "Unknown option character `\\x%x'\n", optopt);
                }
                exit(EXIT_FAILURE);
            default:
                abort();
        }
    }

    if (!bootloader || !kernel) { 
        usage(argv[0]);
    }

    buf = map_phi();

    printf("Philix booting PHI with custom kernel image...");

    if (!boot_phi(bootloader, kernel)) {
        printf("OK\n");
    } else {
        printf("FAIL\n");
        exit(EXIT_FAILURE);
    }

	// setup the console data structure
    console_init(buf, nocurses);

	// map in the SBOX unit in the Phi
	console.sbox = map_phi_sbox();

	if (!nocurses) {

		if (check_terminal_size() != 0) {
			exit(1);
		}

		setup_screen();

	}

    // won't come back from here until exit
    console.cops.main_loop();

	if (!nocurses) {
		teardown_screen();
	}

    return 0;
}
