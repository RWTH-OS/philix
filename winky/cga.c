#include "xeon_phi.h"
#include "cga.h"


static struct term_info {
    unsigned    row;
    unsigned    col;
    unsigned char   color;
    volatile uint16_t * buf;
    int lock;
} term;


static uint16_t 
make_vgaentry (char c, unsigned char color)
{
    unsigned short c16 = c;
    unsigned short color16 = color;
    return c16 | color16 << 8;
}


static void 
phi_write_fb_and_notify (unsigned short x, unsigned short y, char c, unsigned char color)
{
    const unsigned index = y * VGA_WIDTH + x;
    term.buf[index] = make_vgaentry(c, color);
    phi_notify_char_write(x, y);
}


static unsigned char 
make_color (enum vga_color fg, enum vga_color bg) 
{
    return fg | bg << 4;
}


void 
term_init (void)
{
    term.row = 0;
    term.col = 0;
    term.color = make_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    term.buf = (unsigned short*) VGA_BASE_ADDR;
    unsigned y;
    unsigned x;
    for ( y = 0; y < VGA_HEIGHT; y++ )
    {
        for ( x = 0; x < VGA_WIDTH; x++ )
        {
            const unsigned index = y * VGA_WIDTH + x;
            term.buf[index] = make_vgaentry(' ', term.color);
        }
    }

    phi_notify_redraw();
}
 

void 
term_putc (char c, unsigned char color, unsigned x, unsigned y)
{
    const unsigned index = y * VGA_WIDTH + x;
    term.buf[index] = make_vgaentry(c, color);
}
 

inline void
term_clear (void) 
{
    unsigned i;

    for (i = 0; i < VGA_HEIGHT*VGA_WIDTH; i++) {
        term.buf[i] = make_vgaentry(' ', term.color);
    }

    phi_notify_redraw();

}


static void 
term_scrollup (void) 
{
    int i;
    int n = (((VGA_HEIGHT-1)*VGA_WIDTH*2)/sizeof(long));
    int lpl = (VGA_WIDTH*2)/sizeof(long);
    long * pos = (long*)term.buf;
    
    phi_notify_scrollup();

    for (i = 0; i < n; i++) {
        *pos = *(pos + lpl);
        ++pos;
    }

    unsigned index = (VGA_HEIGHT-1) * VGA_WIDTH;
    for (i = 0; i < VGA_WIDTH; i++) {
        term.buf[index++] = make_vgaentry(' ', term.color);
    }

    phi_notify_redraw();
}


void 
putchar (char c)
{
    if (c == '\n') {
        term.col = 0;

        phi_notify_line_draw(term.row);

        if (++term.row == VGA_HEIGHT) {
            term_scrollup();
            term.row--;
        }

        return;
    }

    term_putc(c, term.color, term.col, term.row);

    if (++term.col == VGA_WIDTH) {
        term.col = 0;

        phi_notify_line_draw(term.row);

        if (++term.row == VGA_HEIGHT) {
            term_scrollup();
            term.row--;
        }
    }
}


int puts
(const char *s)
{
    while (*s)
    {
        putchar(*s);
        s++;
    }
    putchar('\n');
    return 0;
}
 

static inline unsigned 
strlen (const char * str)
{
    size_t ret = 0;
    while (str[ret] != 0) {
        ret++;
    }

    return ret;
}

void 
term_print (const char* data)
{
    int i;
    unsigned datalen = strlen(data);
    for (i = 0; i < datalen; i++) {
        putchar(data[i]);
    }
}
