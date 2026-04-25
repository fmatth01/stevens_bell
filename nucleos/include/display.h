#ifndef DISPLAY_H
#define DISPLAY_H

#include "synth_types.h"

// SSD1306 0.96" 128x64 OLED driver + synth UI views.
// Hardware: I2C1, PB6 = SCL, PB7 = SDA (AF4, open-drain).
// To change pins or address, edit the #defines at the top of display.c.

void display_init(void);

// Render the parameter inspector for one module slot (0..7).
void display_module_view(Synth *s, uint8_t slot);

// Render the 4x2 module grid overview.
void display_global_view(Synth *s);

#endif // DISPLAY_H
