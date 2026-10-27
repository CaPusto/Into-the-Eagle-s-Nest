#ifndef EAGLE_MOD_H
#define EAGLE_MOD_H

#include <stddef.h>
#include <stdint.h>

// Minimal ProTracker MOD (.mod) decoder.
//
// Parses a 31-instrument "M.K."/4-channel module and renders one full loop of
// the song to a flat mono 16-bit PCM buffer using an Amiga-Paula style mixer
// (period-based pitch, 8-bit signed samples, per-channel 0..64 volume). Only
// the effects actually used by the game's module are implemented: arpeggio
// (0xy), position jump (Bxx), set volume (Cxx) and set speed/tempo (Fxx).
//
// Returns a malloc'd buffer the caller must free, or NULL on failure.
// *out_frames receives the number of mono samples written.
int16_t* mod_render_pcm(const void* data, size_t size, int sample_rate,
                        int* out_frames);

#endif  // EAGLE_MOD_H
