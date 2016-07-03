#ifndef __CGA_H__
#define __CGA_H__

#define VGA_BASE_ADDR 0xb8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

enum vga_color
{
    COLOR_BLACK = 0,
    COLOR_BLUE = 1,
    COLOR_GREEN = 2,
    COLOR_CYAN = 3,
    COLOR_RED = 4,
    COLOR_MAGENTA = 5,
    COLOR_BROWN = 6,
    COLOR_LIGHT_GREY = 7,
    COLOR_DARK_GREY = 8,
    COLOR_LIGHT_BLUE = 9,
    COLOR_LIGHT_GREEN = 10,
    COLOR_LIGHT_CYAN = 11,
    COLOR_LIGHT_RED = 12,
    COLOR_LIGHT_MAGENTA = 13,
    COLOR_LIGHT_BROWN = 14,
    COLOR_WHITE = 15,
};

void term_init(void);
void putchar(char c);
void term_putc(char c, unsigned char color, unsigned x, unsigned y);
int puts(const char * s);
void term_print(const char * data);
void term_clear(void);

#endif
