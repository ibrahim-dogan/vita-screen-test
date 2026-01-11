/*
 * Vita Screen Test
 * by Ibrahim Dogan
 * 
 * This application displays various test patterns to help detect
 * OLED burn-in (image retention) on the PS Vita OLED screen.
 * 
 * Controls:
 * - Cross/Circle: Next pattern
 * - Square/Triangle: Previous pattern
 * - Start: Exit application
 * - L/R: Adjust animation speed
 */

#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <string.h>
#include <stdlib.h>

#define SCREEN_WIDTH    960
#define SCREEN_HEIGHT   544
#define SCREEN_FB_WIDTH 960
#define SCREEN_FB_SIZE  (2 * 1024 * 1024)

// Colors in BGR format (Vita framebuffer format)
#define COLOR_BLACK   0xFF000000
#define COLOR_WHITE   0xFFFFFFFF
#define COLOR_RED     0xFF0000FF
#define COLOR_GREEN   0xFF00FF00
#define COLOR_BLUE    0xFFFF0000
#define COLOR_CYAN    0xFFFFFF00
#define COLOR_MAGENTA 0xFFFF00FF
#define COLOR_YELLOW  0xFF00FFFF
#define COLOR_GRAY    0xFF808080
#define COLOR_DARK_GRAY 0xFF404040

typedef enum {
    PATTERN_SOLID_RED,
    PATTERN_SOLID_GREEN,
    PATTERN_SOLID_BLUE,
    PATTERN_SOLID_WHITE,
    PATTERN_SOLID_BLACK,
    PATTERN_SOLID_CYAN,
    PATTERN_SOLID_MAGENTA,
    PATTERN_SOLID_YELLOW,
    PATTERN_GRADIENT_H,
    PATTERN_GRADIENT_V,
    PATTERN_CHECKERBOARD_SMALL,
    PATTERN_CHECKERBOARD_LARGE,
    PATTERN_HORIZONTAL_BARS,
    PATTERN_VERTICAL_BARS,
    PATTERN_MOVING_BAR_H,
    PATTERN_MOVING_BAR_V,
    PATTERN_COLOR_CYCLE,
    PATTERN_INVERSION_TEST,
    PATTERN_GRAY_LEVELS,
    PATTERN_COUNT
} TestPattern;

// Double buffering
static void *framebuffers[2];
static SceUID fb_memblocks[2];
static int current_fb = 0;
static void *draw_buffer;

static int animation_frame = 0;
static int animation_speed = 2;

static uint32_t make_color_bgr(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000 | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
}

static void fill_solid(uint32_t color) {
    uint32_t *pixels = (uint32_t *)draw_buffer;
    for (int i = 0; i < SCREEN_FB_WIDTH * SCREEN_HEIGHT; i++) {
        pixels[i] = color;
    }
}

static void draw_gradient_horizontal(void) {
    uint32_t *pixels = (uint32_t *)draw_buffer;
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            uint8_t level = (x * 255) / SCREEN_WIDTH;
            pixels[y * SCREEN_FB_WIDTH + x] = make_color_bgr(level, level, level);
        }
    }
}

static void draw_gradient_vertical(void) {
    uint32_t *pixels = (uint32_t *)draw_buffer;
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        uint8_t level = (y * 255) / SCREEN_HEIGHT;
        uint32_t color = make_color_bgr(level, level, level);
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            pixels[y * SCREEN_FB_WIDTH + x] = color;
        }
    }
}

static void draw_checkerboard(int cell_size) {
    uint32_t *pixels = (uint32_t *)draw_buffer;
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            int checker = ((x / cell_size) + (y / cell_size)) % 2;
            pixels[y * SCREEN_FB_WIDTH + x] = checker ? COLOR_WHITE : COLOR_BLACK;
        }
    }
}

static void draw_horizontal_bars(void) {
    uint32_t *pixels = (uint32_t *)draw_buffer;
    uint32_t colors[] = {COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_CYAN, 
                         COLOR_MAGENTA, COLOR_YELLOW, COLOR_WHITE, COLOR_BLACK};
    int bar_height = SCREEN_HEIGHT / 8;
    
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        int color_idx = (y / bar_height) % 8;
        uint32_t color = colors[color_idx];
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            pixels[y * SCREEN_FB_WIDTH + x] = color;
        }
    }
}

static void draw_vertical_bars(void) {
    uint32_t *pixels = (uint32_t *)draw_buffer;
    uint32_t colors[] = {COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_CYAN,
                         COLOR_MAGENTA, COLOR_YELLOW, COLOR_WHITE, COLOR_BLACK};
    int bar_width = SCREEN_WIDTH / 8;
    
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            int color_idx = (x / bar_width) % 8;
            pixels[y * SCREEN_FB_WIDTH + x] = colors[color_idx];
        }
    }
}

static void draw_moving_bar_horizontal(int frame) {
    uint32_t *pixels = (uint32_t *)draw_buffer;
    int bar_width = 64;
    int bar_pos = (frame * animation_speed) % (SCREEN_WIDTH + bar_width);
    
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            int in_bar = (x >= bar_pos - bar_width && x < bar_pos);
            pixels[y * SCREEN_FB_WIDTH + x] = in_bar ? COLOR_WHITE : COLOR_BLACK;
        }
    }
}

static void draw_moving_bar_vertical(int frame) {
    uint32_t *pixels = (uint32_t *)draw_buffer;
    int bar_height = 64;
    int bar_pos = (frame * animation_speed) % (SCREEN_HEIGHT + bar_height);
    
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        int in_bar = (y >= bar_pos - bar_height && y < bar_pos);
        uint32_t color = in_bar ? COLOR_WHITE : COLOR_BLACK;
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            pixels[y * SCREEN_FB_WIDTH + x] = color;
        }
    }
}

static void draw_color_cycle(int frame) {
    int hue = (frame * animation_speed) % 360;
    float h = hue / 60.0f;
    int i = (int)h;
    float f = h - i;
    uint8_t v = 255;
    uint8_t p = 0;
    uint8_t q = (uint8_t)(255 * (1 - f));
    uint8_t t = (uint8_t)(255 * f);
    
    uint8_t r, g, b;
    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    
    fill_solid(make_color_bgr(r, g, b));
}

static void draw_inversion_test(int frame) {
    int phase = (frame / 60) % 2;
    fill_solid(phase ? COLOR_WHITE : COLOR_BLACK);
}

static void draw_gray_levels(void) {
    uint32_t *pixels = (uint32_t *)draw_buffer;
    int num_levels = 16;
    int bar_width = SCREEN_WIDTH / num_levels;
    
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            int level_idx = x / bar_width;
            if (level_idx >= num_levels) level_idx = num_levels - 1;
            uint8_t gray = (level_idx * 255) / (num_levels - 1);
            pixels[y * SCREEN_FB_WIDTH + x] = make_color_bgr(gray, gray, gray);
        }
    }
}

// ============================================
// Font rendering system (4x6 tiny font)
// ============================================

// 4x6 font for characters (compact)
static const uint8_t font_4x6[96][6] = {
    {0x0,0x0,0x0,0x0,0x0,0x0}, // space
    {0x4,0x4,0x4,0x0,0x4,0x0}, // !
    {0xA,0xA,0x0,0x0,0x0,0x0}, // "
    {0xA,0xF,0xA,0xF,0xA,0x0}, // #
    {0x4,0xE,0xC,0x2,0xE,0x4}, // $
    {0x9,0x2,0x4,0x8,0x9,0x0}, // %
    {0x4,0xA,0x4,0xA,0x5,0x0}, // &
    {0x4,0x4,0x0,0x0,0x0,0x0}, // '
    {0x2,0x4,0x4,0x4,0x2,0x0}, // (
    {0x4,0x2,0x2,0x2,0x4,0x0}, // )
    {0x0,0xA,0x4,0xA,0x0,0x0}, // *
    {0x0,0x4,0xE,0x4,0x0,0x0}, // +
    {0x0,0x0,0x0,0x4,0x4,0x8}, // ,
    {0x0,0x0,0xE,0x0,0x0,0x0}, // -
    {0x0,0x0,0x0,0x0,0x4,0x0}, // .
    {0x1,0x2,0x4,0x8,0x0,0x0}, // /
    {0x6,0x9,0x9,0x9,0x6,0x0}, // 0
    {0x4,0xC,0x4,0x4,0xE,0x0}, // 1
    {0x6,0x9,0x2,0x4,0xF,0x0}, // 2
    {0xE,0x1,0x6,0x1,0xE,0x0}, // 3
    {0x2,0x6,0xA,0xF,0x2,0x0}, // 4
    {0xF,0x8,0xE,0x1,0xE,0x0}, // 5
    {0x6,0x8,0xE,0x9,0x6,0x0}, // 6
    {0xF,0x1,0x2,0x4,0x4,0x0}, // 7
    {0x6,0x9,0x6,0x9,0x6,0x0}, // 8
    {0x6,0x9,0x7,0x1,0x6,0x0}, // 9
    {0x0,0x4,0x0,0x4,0x0,0x0}, // :
    {0x0,0x4,0x0,0x4,0x4,0x8}, // ;
    {0x2,0x4,0x8,0x4,0x2,0x0}, // <
    {0x0,0xE,0x0,0xE,0x0,0x0}, // =
    {0x8,0x4,0x2,0x4,0x8,0x0}, // >
    {0x6,0x9,0x2,0x0,0x4,0x0}, // ?
    {0x6,0x9,0xB,0x8,0x6,0x0}, // @
    {0x6,0x9,0xF,0x9,0x9,0x0}, // A
    {0xE,0x9,0xE,0x9,0xE,0x0}, // B
    {0x6,0x9,0x8,0x9,0x6,0x0}, // C
    {0xE,0x9,0x9,0x9,0xE,0x0}, // D
    {0xF,0x8,0xE,0x8,0xF,0x0}, // E
    {0xF,0x8,0xE,0x8,0x8,0x0}, // F
    {0x6,0x8,0xB,0x9,0x6,0x0}, // G
    {0x9,0x9,0xF,0x9,0x9,0x0}, // H
    {0xE,0x4,0x4,0x4,0xE,0x0}, // I
    {0x7,0x1,0x1,0x9,0x6,0x0}, // J
    {0x9,0xA,0xC,0xA,0x9,0x0}, // K
    {0x8,0x8,0x8,0x8,0xF,0x0}, // L
    {0x9,0xF,0xF,0x9,0x9,0x0}, // M
    {0x9,0xD,0xB,0x9,0x9,0x0}, // N
    {0x6,0x9,0x9,0x9,0x6,0x0}, // O
    {0xE,0x9,0xE,0x8,0x8,0x0}, // P
    {0x6,0x9,0x9,0xA,0x5,0x0}, // Q
    {0xE,0x9,0xE,0xA,0x9,0x0}, // R
    {0x6,0x8,0x6,0x1,0xE,0x0}, // S
    {0xE,0x4,0x4,0x4,0x4,0x0}, // T
    {0x9,0x9,0x9,0x9,0x6,0x0}, // U
    {0x9,0x9,0x9,0x6,0x6,0x0}, // V
    {0x9,0x9,0xF,0xF,0x9,0x0}, // W
    {0x9,0x9,0x6,0x9,0x9,0x0}, // X
    {0x9,0x9,0x6,0x4,0x4,0x0}, // Y
    {0xF,0x1,0x6,0x8,0xF,0x0}, // Z
    {0x6,0x4,0x4,0x4,0x6,0x0}, // [
    {0x8,0x4,0x2,0x1,0x0,0x0}, // backslash
    {0x6,0x2,0x2,0x2,0x6,0x0}, // ]
    {0x4,0xA,0x0,0x0,0x0,0x0}, // ^
    {0x0,0x0,0x0,0x0,0xF,0x0}, // _
    {0x4,0x2,0x0,0x0,0x0,0x0}, // `
    {0x0,0x6,0x9,0xB,0x5,0x0}, // a
    {0x8,0xE,0x9,0x9,0xE,0x0}, // b
    {0x0,0x6,0x8,0x8,0x6,0x0}, // c
    {0x1,0x7,0x9,0x9,0x7,0x0}, // d
    {0x0,0x6,0xF,0x8,0x6,0x0}, // e
    {0x2,0x4,0xE,0x4,0x4,0x0}, // f
    {0x0,0x7,0x9,0x7,0x1,0x6}, // g
    {0x8,0xE,0x9,0x9,0x9,0x0}, // h
    {0x4,0x0,0x4,0x4,0x4,0x0}, // i
    {0x2,0x0,0x2,0x2,0xA,0x4}, // j
    {0x8,0x9,0xA,0xC,0x9,0x0}, // k
    {0x4,0x4,0x4,0x4,0x2,0x0}, // l
    {0x0,0xA,0xF,0x9,0x9,0x0}, // m
    {0x0,0xE,0x9,0x9,0x9,0x0}, // n
    {0x0,0x6,0x9,0x9,0x6,0x0}, // o
    {0x0,0xE,0x9,0xE,0x8,0x8}, // p
    {0x0,0x7,0x9,0x7,0x1,0x1}, // q
    {0x0,0x6,0x9,0x8,0x8,0x0}, // r
    {0x0,0x7,0xC,0x3,0xE,0x0}, // s
    {0x4,0xE,0x4,0x4,0x2,0x0}, // t
    {0x0,0x9,0x9,0x9,0x6,0x0}, // u
    {0x0,0x9,0x9,0x6,0x6,0x0}, // v
    {0x0,0x9,0x9,0xF,0x6,0x0}, // w
    {0x0,0x9,0x6,0x6,0x9,0x0}, // x
    {0x0,0x9,0x9,0x7,0x1,0x6}, // y
    {0x0,0xF,0x2,0x4,0xF,0x0}, // z
    {0x2,0x4,0xC,0x4,0x2,0x0}, // {
    {0x4,0x4,0x4,0x4,0x4,0x0}, // |
    {0x8,0x4,0x6,0x4,0x8,0x0}, // }
    {0x0,0x5,0xA,0x0,0x0,0x0}, // ~
    {0xF,0xF,0xF,0xF,0xF,0xF}, // DEL (filled block)
};

static void draw_char(uint32_t *pixels, int x, int y, char c, int scale, uint32_t fg, uint32_t bg, int use_bg) {
    int idx = c - 32;
    if (idx < 0 || idx >= 96) idx = 0;
    
    for (int row = 0; row < 6; row++) {
        uint8_t line = font_4x6[idx][row];
        for (int col = 0; col < 4; col++) {
            int set = (line >> (3 - col)) & 1;
            if (set || use_bg) {
                uint32_t color = set ? fg : bg;
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + col * scale + sx;
                        int py = y + row * scale + sy;
                        if (px >= 0 && px < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT) {
                            pixels[py * SCREEN_FB_WIDTH + px] = color;
                        }
                    }
                }
            }
        }
    }
}

static void draw_string(uint32_t *pixels, int x, int y, const char *str, int scale, uint32_t fg, uint32_t bg, int use_bg) {
    int orig_x = x;
    while (*str) {
        if (*str == '\n') {
            y += 6 * scale + scale;
            x = orig_x;
        } else {
            draw_char(pixels, x, y, *str, scale, fg, bg, use_bg);
            x += 4 * scale + scale;
        }
        str++;
    }
}

static int get_string_width(const char *str, int scale) {
    int width = 0;
    int max_width = 0;
    while (*str) {
        if (*str == '\n') {
            if (width > max_width) max_width = width;
            width = 0;
        } else {
            width += 4 * scale + scale;
        }
        str++;
    }
    return (width > max_width) ? width : max_width;
}

// Draw a box with outline
static void draw_box(uint32_t *pixels, int x, int y, int w, int h, uint32_t fill, uint32_t outline) {
    for (int py = y; py < y + h && py < SCREEN_HEIGHT; py++) {
        for (int px = x; px < x + w && px < SCREEN_WIDTH; px++) {
            if (px >= 0 && py >= 0) {
                int is_border = (px == x || px == x + w - 1 || py == y || py == y + h - 1);
                pixels[py * SCREEN_FB_WIDTH + px] = is_border ? outline : fill;
            }
        }
    }
}

// Draw pattern indicator with good contrast (outlined text)
static void draw_pattern_indicator(int pattern_num, int total) {
    uint32_t *pixels = (uint32_t *)draw_buffer;
    
    char buf[16];
    // Simple integer to string
    if (pattern_num >= 10) {
        buf[0] = '0' + (pattern_num / 10);
        buf[1] = '0' + (pattern_num % 10);
        buf[2] = '/';
        buf[3] = '0' + (total / 10);
        buf[4] = '0' + (total % 10);
        buf[5] = '\0';
    } else {
        buf[0] = '0' + pattern_num;
        buf[1] = '/';
        buf[2] = '0' + (total / 10);
        buf[3] = '0' + (total % 10);
        buf[4] = '\0';
    }
    
    int scale = 3;
    int text_w = get_string_width(buf, scale);
    int text_h = 6 * scale;
    int box_x = 8;
    int box_y = 8;
    int box_w = text_w + 16;
    int box_h = text_h + 12;
    
    // Draw box with semi-transparent background
    draw_box(pixels, box_x, box_y, box_w, box_h, 0xD0000000, 0xFFFFFFFF);
    
    // Draw text with outline for visibility
    int tx = box_x + 8;
    int ty = box_y + 6;
    
    // Draw outline (black)
    draw_string(pixels, tx - 1, ty, buf, scale, COLOR_BLACK, 0, 0);
    draw_string(pixels, tx + 1, ty, buf, scale, COLOR_BLACK, 0, 0);
    draw_string(pixels, tx, ty - 1, buf, scale, COLOR_BLACK, 0, 0);
    draw_string(pixels, tx, ty + 1, buf, scale, COLOR_BLACK, 0, 0);
    // Draw main text (white)
    draw_string(pixels, tx, ty, buf, scale, COLOR_WHITE, 0, 0);
}

// Draw welcome screen
static void draw_welcome_screen(void) {
    uint32_t *pixels = (uint32_t *)draw_buffer;
    
    // Dark blue gradient background
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        uint8_t b = 40 + (y * 30) / SCREEN_HEIGHT;
        uint32_t color = make_color_bgr(10, 15, b);
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            pixels[y * SCREEN_FB_WIDTH + x] = color;
        }
    }
    
    // Title
    const char *title = "Vita Screen Test";
    int title_scale = 5;
    int title_w = get_string_width(title, title_scale);
    int title_x = (SCREEN_WIDTH - title_w) / 2;
    int title_y = 80;
    draw_string(pixels, title_x + 2, title_y + 2, title, title_scale, COLOR_BLACK, 0, 0);
    draw_string(pixels, title_x, title_y, title, title_scale, COLOR_CYAN, 0, 0);
    
    // Welcome message
    const char *welcome = "Welcome, PS Vita Lover!";
    int welcome_scale = 3;
    int welcome_w = get_string_width(welcome, welcome_scale);
    int welcome_x = (SCREEN_WIDTH - welcome_w) / 2;
    int welcome_y = 160;
    draw_string(pixels, welcome_x + 1, welcome_y + 1, welcome, welcome_scale, COLOR_BLACK, 0, 0);
    draw_string(pixels, welcome_x, welcome_y, welcome, welcome_scale, COLOR_WHITE, 0, 0);
    
    // Controls box
    int box_x = (SCREEN_WIDTH - 400) / 2;
    int box_y = 220;
    int box_w = 400;
    int box_h = 180;
    draw_box(pixels, box_x, box_y, box_w, box_h, 0xC0000000, COLOR_WHITE);
    
    // Controls title
    const char *ctrl_title = "CONTROLS";
    int ctrl_scale = 2;
    int ctrl_w = get_string_width(ctrl_title, ctrl_scale);
    draw_string(pixels, (SCREEN_WIDTH - ctrl_w) / 2, box_y + 12, ctrl_title, ctrl_scale, COLOR_YELLOW, 0, 0);
    
    // Control instructions
    const char *controls[] = {
        "X / O          Next Pattern",
        "[] / /\\       Previous Pattern",
        "L / R          Adjust Speed",
        "SELECT         Toggle Info",
        "START          Exit"
    };
    
    int line_y = box_y + 45;
    for (int i = 0; i < 5; i++) {
        draw_string(pixels, box_x + 30, line_y, controls[i], 2, COLOR_WHITE, 0, 0);
        line_y += 25;
    }
    
    // Press any button
    const char *press = "Press X to start...";
    int press_scale = 2;
    int press_w = get_string_width(press, press_scale);
    draw_string(pixels, (SCREEN_WIDTH - press_w) / 2 + 1, 440 + 1, press, press_scale, COLOR_BLACK, 0, 0);
    draw_string(pixels, (SCREEN_WIDTH - press_w) / 2, 440, press, press_scale, COLOR_GREEN, 0, 0);
    
    // Credits
    const char *credits = "by Ibrahim Dogan";
    int cred_scale = 1;
    int cred_w = get_string_width(credits, cred_scale);
    draw_string(pixels, (SCREEN_WIDTH - cred_w) / 2, 500, credits, cred_scale, COLOR_GRAY, 0, 0);
}

static void draw_pattern(TestPattern pattern, int show_info) {
    switch (pattern) {
        case PATTERN_SOLID_RED:
            fill_solid(COLOR_RED);
            break;
        case PATTERN_SOLID_GREEN:
            fill_solid(COLOR_GREEN);
            break;
        case PATTERN_SOLID_BLUE:
            fill_solid(COLOR_BLUE);
            break;
        case PATTERN_SOLID_WHITE:
            fill_solid(COLOR_WHITE);
            break;
        case PATTERN_SOLID_BLACK:
            fill_solid(COLOR_BLACK);
            break;
        case PATTERN_SOLID_CYAN:
            fill_solid(COLOR_CYAN);
            break;
        case PATTERN_SOLID_MAGENTA:
            fill_solid(COLOR_MAGENTA);
            break;
        case PATTERN_SOLID_YELLOW:
            fill_solid(COLOR_YELLOW);
            break;
        case PATTERN_GRADIENT_H:
            draw_gradient_horizontal();
            break;
        case PATTERN_GRADIENT_V:
            draw_gradient_vertical();
            break;
        case PATTERN_CHECKERBOARD_SMALL:
            draw_checkerboard(8);
            break;
        case PATTERN_CHECKERBOARD_LARGE:
            draw_checkerboard(64);
            break;
        case PATTERN_HORIZONTAL_BARS:
            draw_horizontal_bars();
            break;
        case PATTERN_VERTICAL_BARS:
            draw_vertical_bars();
            break;
        case PATTERN_MOVING_BAR_H:
            draw_moving_bar_horizontal(animation_frame);
            break;
        case PATTERN_MOVING_BAR_V:
            draw_moving_bar_vertical(animation_frame);
            break;
        case PATTERN_COLOR_CYCLE:
            draw_color_cycle(animation_frame);
            break;
        case PATTERN_INVERSION_TEST:
            draw_inversion_test(animation_frame);
            break;
        case PATTERN_GRAY_LEVELS:
            draw_gray_levels();
            break;
        default:
            fill_solid(COLOR_BLACK);
            break;
    }
    
    if (show_info) {
        draw_pattern_indicator(pattern + 1, PATTERN_COUNT);
    }
}

// Swap buffers (double buffering to prevent tearing)
static void swap_buffers(void) {
    SceDisplayFrameBuf fb = {
        .size = sizeof(SceDisplayFrameBuf),
        .base = draw_buffer,
        .pitch = SCREEN_FB_WIDTH,
        .pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8,
        .width = SCREEN_WIDTH,
        .height = SCREEN_HEIGHT
    };
    
    sceDisplayWaitVblankStart();
    sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_IMMEDIATE);
    
    // Switch to other buffer for next frame
    current_fb = 1 - current_fb;
    draw_buffer = framebuffers[current_fb];
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    // Initialize controller
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    
    // Allocate double framebuffer memory
    for (int i = 0; i < 2; i++) {
        fb_memblocks[i] = sceKernelAllocMemBlock("display", 
                                              SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 
                                              SCREEN_FB_SIZE, NULL);
        if (fb_memblocks[i] < 0) {
            sceKernelExitProcess(0);
            return -1;
        }
        sceKernelGetMemBlockBase(fb_memblocks[i], &framebuffers[i]);
        memset(framebuffers[i], 0, SCREEN_FB_SIZE);
    }
    
    draw_buffer = framebuffers[0];
    current_fb = 0;
    
    // Set up initial display
    SceDisplayFrameBuf fb = {
        .size = sizeof(SceDisplayFrameBuf),
        .base = framebuffers[0],
        .pitch = SCREEN_FB_WIDTH,
        .pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8,
        .width = SCREEN_WIDTH,
        .height = SCREEN_HEIGHT
    };
    sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
    
    SceCtrlData ctrl, ctrl_old;
    memset(&ctrl_old, 0, sizeof(ctrl_old));
    
    // ==================
    // Welcome Screen
    // ==================
    int welcome_done = 0;
    while (!welcome_done) {
        sceCtrlPeekBufferPositive(0, &ctrl, 1);
        uint32_t pressed = ctrl.buttons & ~ctrl_old.buttons;
        
        if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START)) {
            welcome_done = 1;
        }
        
        ctrl_old = ctrl;
        
        draw_welcome_screen();
        swap_buffers();
    }
    
    // ==================
    // Main Test Loop
    // ==================
    TestPattern current_pattern = PATTERN_SOLID_RED;
    int show_info = 1;
    int info_timeout = 180;
    
    while (1) {
        sceCtrlPeekBufferPositive(0, &ctrl, 1);
        uint32_t pressed = ctrl.buttons & ~ctrl_old.buttons;
        
        // Next pattern
        if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE)) {
            current_pattern = (current_pattern + 1) % PATTERN_COUNT;
            animation_frame = 0;
            info_timeout = 180;
            show_info = 1;
        }
        
        // Previous pattern
        if (pressed & (SCE_CTRL_SQUARE | SCE_CTRL_TRIANGLE)) {
            current_pattern = (current_pattern + PATTERN_COUNT - 1) % PATTERN_COUNT;
            animation_frame = 0;
            info_timeout = 180;
            show_info = 1;
        }
        
        // Toggle info display
        if (pressed & SCE_CTRL_SELECT) {
            show_info = !show_info;
            info_timeout = show_info ? 180 : 0;
        }
        
        // Adjust speed
        if (pressed & SCE_CTRL_RTRIGGER) {
            animation_speed = (animation_speed < 10) ? animation_speed + 1 : 10;
            info_timeout = 180;
            show_info = 1;
        }
        if (pressed & SCE_CTRL_LTRIGGER) {
            animation_speed = (animation_speed > 1) ? animation_speed - 1 : 1;
            info_timeout = 180;
            show_info = 1;
        }
        
        // Exit
        if (pressed & SCE_CTRL_START) {
            break;
        }
        
        ctrl_old = ctrl;
        
        // Update animation
        animation_frame++;
        
        // Auto-hide info after timeout
        if (info_timeout > 0) {
            info_timeout--;
            if (info_timeout == 0) {
                show_info = 0;
            }
        }
        
        // Draw current pattern
        draw_pattern(current_pattern, show_info);
        
        // Swap buffers (vsync + flip)
        swap_buffers();
    }
    
    // Cleanup
    sceDisplaySetFrameBuf(NULL, SCE_DISPLAY_SETBUF_IMMEDIATE);
    for (int i = 0; i < 2; i++) {
        sceKernelFreeMemBlock(fb_memblocks[i]);
    }
    
    sceKernelExitProcess(0);
    return 0;
}
