#ifndef WAVETABLE_H
#define WAVETABLE_H

#include <stdint.h>
#include <stddef.h>

// ── Wavetable enum ────────────────────────────────────────────────────────────
// Add a new entry here AND a matching Wavetable instance in wavetable.c.
typedef enum
{
    TABLE_SINE = 0,
    TABLE_COUNT
} Table;

// ── Wavetable struct ──────────────────────────────────────────────────────────
typedef struct Wavetable
{
    const int16_t *samples; // pointer to raw PCM data (one full cycle)
    uint16_t       length;  // number of samples in one cycle
    const char    *name;    // short display name shown in UI, e.g. "SIN"
} Wavetable;

// Registry — one entry per Table enum value, defined in wavetable.c.
extern const Wavetable WAVETABLES[TABLE_COUNT];

// Convenience accessor — bounds-safe, returns pointer into WAVETABLES[].
static inline const Wavetable *get_wavetable(Table t)
{
    return &WAVETABLES[t];
}

#endif // WAVETABLE_H
