#include "display.h"
#include <stdio.h>
#include <stdint.h>

#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "intf/i2c/ssd1306_i2c.h"
#include "lcd/oled_ssd1306.h"

// ── Scope constants ───────────────────────────────────────────────────────────
// Blue region of the two-tone OLED: rows 16-63 (48 px tall).
// Yellow region (rows 0-15) is reserved for labels.
#define SCOPE_TOP 16
#define SCOPE_BOT 63
#define SCOPE_MID ((SCOPE_TOP + SCOPE_BOT) / 2)  // 39
#define SCOPE_HALF ((SCOPE_BOT - SCOPE_TOP) / 2) // 23

// Frames to hold before re-drawing: index maps into this table.
static const uint16_t SKIP_SEQ[10] = {0, 1, 2, 4, 8, 16, 32, 64, 128, 256};

// CC18 (0..127) → stride: maps directly to SKIP_SEQ values {0,1,2,4,...,256}.
// Index 0 (stride=0) is treated as 1 to avoid divide-by-zero.
static uint16_t cc18_to_stride(uint8_t cc18)
{
    uint16_t s = SKIP_SEQ[cc18 * 10 / 128];
    return s ? s : 1;
}

// int16_t sample → pixel row in the blue region, scaled by CC17 (top_slider).
static uint8_t sample_to_y(int16_t sample, uint8_t top_slider)
{
    int zoom = top_slider * 6 / 128; // steps 0..5
    int divisor = 32768 >> zoom;     // 32768 (zoomed out) → 1024 (zoomed in)
    int y = SCOPE_MID - (int32_t)sample * SCOPE_HALF / divisor;
    if (y < SCOPE_TOP)
        y = SCOPE_TOP;
    if (y > SCOPE_BOT)
        y = SCOPE_BOT;
    return (uint8_t)y;
}

static void draw_scope_header(Synth *s)
{
    char buf[12];
    if (s->global_view)
    {
        ssd1306_printFixed(0, 0, "GLOBAL", STYLE_NORMAL);
    }
    else
    {
        const ModuleDef *def = module_def(s->modules[s->selected_module].type);
        snprintf(buf, sizeof(buf), "%d: %.3s", s->selected_module,
                 def->name ? def->name : "???");
        ssd1306_printFixed(0, 0, buf, STYLE_NORMAL);
    }
    ssd1306_printFixed(98, 0, s->scope_macro ? "MACRO" : "MICRO", STYLE_NORMAL);
}

// Draw all 128 macro bars (min-to-max vertical lines) in the blue region.
static void draw_scope_macro(Synth *s)
{
    draw_scope_header(s);
    ssd1306_clearBlock(0, SCOPE_TOP / 8, 128, 48); // clear blue region
    for (uint8_t x = 0; x < 128; x++)
    {
        uint8_t idx = (s->scope_bar_idx + x) % 128; // oldest bar first
        uint8_t y_top = sample_to_y(s->scope_bar_max[idx], s->top_slider);
        uint8_t y_bot = sample_to_y(s->scope_bar_min[idx], s->top_slider);
        ssd1306_drawVLine(x, y_top, y_bot);
    }
}

// Draw 128 micro points (one pixel per collected sample) in the blue region.
static void draw_scope_micro(Synth *s)
{
    draw_scope_header(s);
    ssd1306_clearBlock(0, SCOPE_TOP / 8, 128, 48); // clear blue region
    for (uint8_t x = 0; x < BUFFER_SIZE; x++)
    {
        uint8_t y = sample_to_y(s->scope_micro_buf[x], s->top_slider);
        ssd1306_putPixel(x, y);
    }
}

// ── Init ──────────────────────────────────────────────────────────────────────

void display_init(void)
{
    // Triggers ssd1306_platform_i2cInit() in platform.c (I2C1, PB6/PB7).
    ssd1306_i2cInitEx(-1, -1, 0x3C);
    ssd1306_128x64_init();
    ssd1306_setFixedFont(ssd1306xled_font6x8);
    ssd1306_clearScreen();
}

// ── Module view ───────────────────────────────────────────────────────────────
// Layout (8x16 font, 128x64):
//   y= 0  display_name centered          ← yellow bar (rows 0-15)
//   y=16  INP:*FLAT* | 50%               ← blue
//   y=32  POS: KNOB  | 75%               ← blue
//   y=48  FRQ: FLAT  |100%               ← blue

static const char *SOURCE_NAMES[] = {"FLAT", "KNOB", "LEFT", "BOTM", "WAVE"};

// Draw one parameter row using the current font (8x16).
// Format: "LBL:*SRC* |VAL"  (* flanking when selected, space otherwise)
// Fits comfortably in 128px at 8px/char (up to 16 chars).
static void draw_param_row(uint8_t y, const char *label,
                           uint8_t src, uint8_t val, int selected)
{
    char buf[20];
    const char *sname = (src < SRC_COUNT) ? SOURCE_NAMES[src] : "???";

    char vbuf[6];
    if (src == SRC_LEFT)
    {
        vbuf[0] = ' ';
        vbuf[1] = '<';
        vbuf[2] = '<';
        vbuf[3] = '\0';
    }
    else if (src == SRC_BOTM)
    {
        vbuf[0] = ' ';
        vbuf[1] = 'v';
        vbuf[2] = 'v';
        vbuf[3] = '\0';
    }
    else if (src == SRC_WAVE)
    {
        vbuf[0] = 'w';
        vbuf[1] = 'a';
        vbuf[2] = 'v';
        vbuf[3] = '\0';
    }
    else
        snprintf(vbuf, sizeof(vbuf), "%3d%%", (val * 100) / 127);

    snprintf(buf, sizeof(buf), "%.3s:%c%.4s%c|%s",
             label,
             selected ? '*' : ' ',
             sname,
             selected ? '*' : ' ',
             vbuf);

    ssd1306_printFixed(0, y, buf, STYLE_NORMAL);
}

void display_module_view(Synth *s, uint8_t slot)
{
    if (slot >= NUM_MODULES)
        return;

    const Module *m = &s->modules[slot];
    const ModuleDef *def = module_def(m->type);

    ssd1306_setFixedFont(ssd1306xled_font8x16);
    ssd1306_clearScreen();

    // Yellow bar (y=0..15): display_name centered at 8px/char.
    const char *dname = (def->display_name && def->display_name[0])
                            ? def->display_name
                            : def->name;
    uint8_t len = 0;
    while (dname[len] && len < 16)
        len++;
    uint8_t hdr_x = (len < 16) ? (128 - len * 8) / 2 : 0;
    ssd1306_printFixed(hdr_x, 0, dname, STYLE_NORMAL);

    // Blue rows at y=16, 32, 48.
    draw_param_row(16, "INP",
                   m->input_source, 0,
                   s->selected_param == SEL_INPUT);

    draw_param_row(32,
                   (def->pr1_label && def->pr1_label[0]) ? def->pr1_label : "PR1",
                   m->param1_source, m->param1_value,
                   s->selected_param == SEL_PARAM1);

    draw_param_row(48,
                   (def->pr2_label && def->pr2_label[0]) ? def->pr2_label : "PR2",
                   m->param2_source, m->param2_value,
                   s->selected_param == SEL_PARAM2);

    ssd1306_setFixedFont(ssd1306xled_font6x8); // restore for other views
}

// ── Routing arrows ───────────────────────────────────────────────────────────
// 8×8 bitmaps, row-major. bit7 of each byte = leftmost column (lowest x).
//
// ARROW_RIGHT: 90° CW rotation of ARROW_UP. Tip at col 7 (rightmost).
//   . . . . . . . .   0x00
//   . . . . . X . .   0x04
//   . . . . . X X .   0x06
//   X X X X X . X X   0xFB
//   X X X X X . X X   0xFB
//   . . . . . X X .   0x06
//   . . . . . X . .   0x04
//   . . . . . . . .   0x00
static const uint8_t ARROW_RIGHT[8] = {
    0x00,
    0x04,
    0x06,
    0xFB,
    0xFB,
    0x06,
    0x04,
    0x00,
};

// ARROW_UP: tip at row 0 (topmost). Used for SRC_BOTM input routing.
//   . . . X X . . .   0x18
//   . . X X X X . .   0x3C
//   . X X . . X X .   0x66
//   . . . X X . . .   0x18
//   . . . X X . . .   0x18
//   . . . X X . . .   0x18
//   . . . X X . . .   0x18
//   . . . X X . . .   0x18
static const uint8_t ARROW_UP[8] = {
    0x18,
    0x3C,
    0x66,
    0x18,
    0x18,
    0x18,
    0x18,
    0x18,
};

// Draw an 8x8 bitmap stored row-major (bit7 = leftmost column).
// Uses ssd1306_putPixels to write each column in one shot, avoiding the
// single-pixel overwrite bug in ssd1306_putPixel. Handles sprites that
// straddle a page boundary (non-page-aligned y).
static void draw_bitmap_8x8(uint8_t x, uint8_t y, const uint8_t *bmp)
{
    uint8_t offset = y & 7u;  // pixel offset within the starting page
    uint8_t page_y = y & ~7u; // y rounded down to page boundary

    for (uint8_t col = 0; col < 8; col++)
    {
        // Transpose: build the column byte.
        // Bit r of col_byte = 1 if the bitmap has a pixel at (row=r, col=col).
        uint8_t col_byte = 0;
        for (uint8_t row = 0; row < 8; row++)
        {
            if (bmp[row] & (0x80 >> col))
                col_byte |= (uint8_t)(1u << row);
        }

        if (offset == 0)
        {
            // Page-aligned: one write covers all 8 rows.
            ssd1306_putPixels(x + col, page_y, col_byte);
        }
        else
        {
            // Straddles two pages: split col_byte across page_a and page_b.
            // Lower (8-offset) sprite rows go into the upper bits of page_a.
            // Upper offset sprite rows go into the lower bits of page_b.
            ssd1306_putPixels(x + col, page_y, (uint8_t)(col_byte << offset));
            ssd1306_putPixels(x + col, page_y + 8, (uint8_t)(col_byte >> (8u - offset)));
        }
    }
}

// Draw routing arrows on grid lines based solely on each module's input_source.
static void draw_routing_arrows(Synth *s)
{
    for (uint8_t i = 0; i < NUM_MODULES; i++)
    {
        uint8_t col = i % 4;
        uint8_t top_row = (i >= 4);

        // Horizontal: slot has SRC_LEFT input and is not the leftmost column.
        // Arrow centered on the vertical grid line, vertically centered in its row cell.
        if (s->modules[i].input_source == SRC_LEFT && col != 0)
        {
            uint8_t line_x = col * 32;
            uint8_t cell_top = top_row ? 16 : 40;
            draw_bitmap_8x8(line_x - 4, cell_top + 8, ARROW_RIGHT);
        }

        // Vertical: top-row slot has SRC_BOTM input.
        // Arrow centered on the horizontal grid line (y=40), horizontally centered in column.
        if (top_row && s->modules[i].input_source == SRC_BOTM)
        {
            draw_bitmap_8x8(col * 32 + 12, 36, ARROW_UP);
        }
    }
}

// ── Global view ───────────────────────────────────────────────────────────────
// Yellow bar (rows 0-15): title.
// Blue region (rows 16-63, 48px): 4x2 grid, each cell 32x24 px.
// Top row (slots 4-7) at y=16..39; bottom row (slots 0-3) at y=40..63.
// Selected module gets a rect drawn around its cell.

void display_global_view(Synth *s)
{
    ssd1306_clearScreen();

    // Yellow bar: centered title. "STEVEN'S BELL" = 13 chars * 6px = 78px; x=(128-78)/2=25.
    ssd1306_printFixed(25, 4, "STEVEN'S BELL", STYLE_NORMAL);

    // Grid lines in blue region only.
    ssd1306_drawHLine(0, 40, 127);
    ssd1306_drawVLine(32, 16, 63);
    ssd1306_drawVLine(64, 16, 63);
    ssd1306_drawVLine(96, 16, 63);

    for (uint8_t i = 0; i < NUM_MODULES; i++)
    {
        const ModuleDef *def = module_def(s->modules[i].type);

        // Top row (slots 4-7) at cy=16, bottom row (slots 0-3) at cy=40.
        uint8_t col = i % 4;
        uint8_t cx = col * 32;
        uint8_t cy = (i >= 4) ? 16 : 40;

        // Selection rect: 2px inset inside 32x24 cell.
        if (i == s->selected_module)
            ssd1306_drawRect(cx + 2, cy + 2, cx + 29, cy + 21);

        // Name centered in 32x24 cell.
        // All names are 3 chars (18px wide) → cx+7. Vertical center: cy+8 (page-aligned).
        ssd1306_printFixed(cx + 7, cy + 8, def->name, STYLE_BOLD);
    }

    draw_routing_arrows(s);
}

// ── Scope update ──────────────────────────────────────────────────────────────
// Call once per main-loop display iteration.
void scope_update(Synth *s)
{
    if (!s->scope_on)
        return;

    int16_t *src = s->global_view
                       ? synth_get_output()
                       : s->modules[s->selected_module].buffer_out;

    uint16_t stride = cc18_to_stride(s->bottom_slider);

    if (s->scope_macro)
    {
        // Compute min/max of this buffer snapshot.
        int16_t mn = src[0], mx = src[0];
        for (int i = 1; i < BUFFER_SIZE; i++)
        {
            if (src[i] < mn)
                mn = src[i];
            if (src[i] > mx)
                mx = src[i];
        }

        // Store a new bar every `stride` buffers.
        s->scope_buf_counter++;
        if (s->scope_buf_counter >= stride)
        {
            s->scope_bar_min[s->scope_bar_idx] = mn;
            s->scope_bar_max[s->scope_bar_idx] = mx;
            s->scope_bar_idx = (s->scope_bar_idx + 1) % 128;
            s->scope_buf_counter = 0;

            // Push to display after holding for skipped_frames collections.
            if (s->scope_frame_counter >= SKIP_SEQ[s->scope_skip_idx])
            {
                draw_scope_macro(s);
                s->scope_frame_counter = 0;
            }
            else
            {
                s->scope_frame_counter++;
            }
        }
    }
    else
    {
        // Collect strided samples across buffers until we have 128 points.
        for (uint16_t i = 0; i < BUFFER_SIZE && s->scope_micro_count < BUFFER_SIZE; i += stride)
            s->scope_micro_buf[s->scope_micro_count++] = src[i];

        if (s->scope_micro_count >= BUFFER_SIZE)
        {
            if (s->scope_frame_counter >= SKIP_SEQ[s->scope_skip_idx])
            {
                draw_scope_micro(s);
                s->scope_frame_counter = 0;
            }
            else
            {
                s->scope_frame_counter++;
            }
            s->scope_micro_count = 0;
        }
    }
}

void update_display(Synth *s)
{
    if (s->scope_on)
    {
        scope_update(s);
    }
    else if (s->global_view)
    {
        display_global_view(s);
    }
    else
    {
        display_module_view(s, s->selected_module);
    }
}