#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Stereo, signed 24-bit samples left justified in 32-bit DMA words. */
void pcm_transition(uint32_t *ring, uint32_t ring_frames, uint32_t start,
                    uint32_t frames, bool fade_in);
void pcm_quiet_tail(uint32_t *ring, uint32_t ring_frames, uint32_t start,
                    uint32_t ramp_frames, uint32_t silent_frames);
