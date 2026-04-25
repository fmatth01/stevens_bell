#include "display.h"
#include <stdio.h>

#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "intf/i2c/ssd1306_i2c.h"
#include "lcd/oled_ssd1306.h"

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
// Layout (6x8 font, 128x64, rows snap to multiples of 8):
//   y= 0  [LongModuleName]
//   y= 8  INP: LEFT  | <<
//   y=16  POS:*KNOB* | 50%    ← * flanking when row is selected
//   y=24  FRQ: FLAT  | 25%

static const char *SOURCE_NAMES[] = {"FLAT", "KNOB", "LEFT", "BOTM", "WAVE"};

static void draw_param_row(uint8_t y, const char *label,
                           uint8_t src, uint8_t val, int selected)
{
    char buf[24];
    const char *sname = (src < SRC_COUNT) ? SOURCE_NAMES[src] : "???";

    // Value field: << for LEFT, vv for BOTM, wav for WAVE, else percentage.
    char vbuf[6];
    if (src == SRC_LEFT)
    {
        vbuf[0] = '<';
        vbuf[1] = '<';
        vbuf[2] = '\0';
    }
    else if (src == SRC_BOTM)
    {
        vbuf[0] = 'v';
        vbuf[1] = 'v';
        vbuf[2] = '\0';
    }
    else if (src == SRC_WAVE)
    {
        vbuf[0] = 'w';
        vbuf[1] = 'a';
        vbuf[2] = 'v';
        vbuf[3] = '\0';
    }
    else
    {
        snprintf(vbuf, sizeof(vbuf), "%3d%%", (val * 100) / 127);
    }

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

    ssd1306_clearScreen();

    // Header: [LongModuleName]
    char hdr[24];
    const char *dname = (def->display_name && def->display_name[0])
                            ? def->display_name
                            : def->name;
    snprintf(hdr, sizeof(hdr), "[%.19s]", dname);
    ssd1306_printFixed(0, 0, hdr, STYLE_NORMAL);

    draw_param_row(8,
                   "INP",
                   m->input_source, 0,
                   s->selected_param == SEL_INPUT);

    draw_param_row(16,
                   (def->pr1_label && def->pr1_label[0]) ? def->pr1_label : "PR1",
                   m->param1_source, m->param1_value,
                   s->selected_param == SEL_PARAM1);

    draw_param_row(24,
                   (def->pr2_label && def->pr2_label[0]) ? def->pr2_label : "PR2",
                   m->param2_source, m->param2_value,
                   s->selected_param == SEL_PARAM2);
}

// ── Global view ───────────────────────────────────────────────────────────────
// 4x2 grid, each cell 32x32 px.
// Top row (slots 4-7) at y=0..31; bottom row (slots 0-3) at y=32..63.
// Selected module gets a rect drawn around its cell.

void display_global_view(Synth *s)
{
    ssd1306_clearScreen();

    // Draw horizontal line first so vertical lines overwrite it at intersections.
    ssd1306_drawHLine(0, 32, 127);
    ssd1306_drawVLine(32, 0, 63);
    ssd1306_drawVLine(64, 0, 63);
    ssd1306_drawVLine(96, 0, 63);

    for (uint8_t i = 0; i < NUM_MODULES; i++)
    {
        const ModuleDef *def = module_def(s->modules[i].type);

        // Top row = slots 4-7 (y=0..31), bottom row = slots 0-3 (y=32..63).
        uint8_t col = i % 4;
        uint8_t cx = col * 32;
        uint8_t cy = (i >= 4) ? 0 : 56;

        // Selection rect (2px inset).
        if (i == s->selected_module)
            ssd1306_drawRect(cx + 2, cy + 2, cx + 29, cy + 29);

        // Name centered in 32x32 cell. cy+16 = page-aligned midpoint of cell.
        ssd1306_printFixed(cx + 7, cy + 4, def->name, STYLE_BOLD);
    }
}
