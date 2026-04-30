#ifndef DISPLAY_H
#define DISPLAY_H

#include "synth_types.h"

// SSD1306 0.96" 128x64 OLED driver + synth UI views.
// Hardware: I2C3, PA7/A6 = SCL, PB4/D12 = SDA (AF4, open-drain).
// To change pins or address, edit platform.c in lib/ssd1306/src/ssd1306_hal/stm32/.

void display_init(void);

// Render the parameter inspector for one module slot (0..7).
void display_module_view(Synth *s, uint8_t slot);

// Render the 4x2 module grid overview.
void display_global_view(Synth *s);

// Call once per main-loop display iteration when scope_on is true.
void scope_update(Synth *s);

// Public Function to update display
void update_display(Synth *s);


#endif // DISPLAY_H
