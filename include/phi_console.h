#ifndef __PHI_CONSOLE_H__
#define __PHI_CONSOLE_H__

#include <curses.h>
#include <stdint.h>

#define CONSOLE_OUTPUT_FILENAME "philix"
#define DEBUG_SCREEN_LINES 10
#define INFO_WIN_LINES 8
#define MAX_HIST_LINES 100000

#define MIN_TTY_COLS 80
#define MIN_TTY_ROWS 25

#define PHI_FB_ADDR   0xb8000
#define PHI_FB_WIDTH  80
#define PHI_FB_HEIGHT 25
// in bytes
#define PHI_FB_LEN    (PHI_FB_WIDTH*PHI_FB_HEIGHT*2)

#define PHI_FB_REGS_OFFSET PHI_FB_LEN
// is there something to draw?
#define OUTPUT_AVAIL_REG_OFFSET 0x0

// ready for next update
#define OUTPUT_DRAWN_REG_OFFSET 0x1



// character was drawn
// upper = x
// lower = y
#define CHAR_REG_OFFSET 0x2

// cursor update
// upper = x
// lower = y
#define CURSOR_REG_OFFSET 0x3

#define LINE_REG_OFFSET 0x4


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

typedef unsigned char uchar_t;

struct line_elm {
    uint16_t          line[PHI_FB_WIDTH];
    struct line_elm * next;
    struct line_elm * prev;
};


struct cons_ops {
	// main window handling
	// flush
	void (*sigint)(int);
	void (*sigsegv)(int);
	void (*handle_exit)(void);
	int (*main_loop)(void);
};

struct curses_windows {
	WINDOW * win;
	WINDOW * debug;
	WINDOW * debug_border;
	WINDOW * phi_info;
	WINDOW * phi_info_border;
};

struct cons_struct {
	unsigned char curses_enabled;
	struct curses_windows windows;
	struct cons_ops cops;
    uint16_t x;
    uint16_t y;
    uint16_t rows;
    uint16_t cols;
    void * gddr;
	void * sbox;
    unsigned long gddr_size;
    volatile uint16_t * fb;
    volatile uint32_t * ctrl_regs;
    struct termios termios_old;
    struct line_elm * line_hist;
    struct line_elm * win_ptr; 
    int line_idx; // 0 is pointing at top of framebuffer, > 0 is pointing at hist buff
    unsigned hist_cnt;
};

#endif
