#include "device/pcm_transition.h"
void pcm_transition(uint32_t *ring, uint32_t ring_frames, uint32_t start,
                    uint32_t frames, bool fade_in)
{
    if (!ring_frames || !frames) return;
    if (frames > ring_frames) frames = ring_frames;
    uint32_t denominator = frames > 1 ? frames - 1 : 1;
    for (uint32_t i = 0; i < frames; ++i) {
        uint32_t gain = frames == 1 ? 0 : fade_in ? i : denominator - i;
        uint32_t at = (start + i) % ring_frames * 2;
        for (uint32_t channel = 0; channel < 2; ++channel) {
            int64_t sample = (int32_t)ring[at + channel];
            ring[at + channel] = (uint32_t)(sample * gain / denominator) & 0xFFFFFF00u;
        }
    }
}

void pcm_quiet_tail(uint32_t *ring, uint32_t ring_frames, uint32_t start,
                    uint32_t ramp_frames, uint32_t silent_frames)
{
    if (!ring_frames) return;
    if (ramp_frames > ring_frames) ramp_frames = ring_frames;
    if (silent_frames > ring_frames - ramp_frames) silent_frames = ring_frames - ramp_frames;
    pcm_transition(ring, ring_frames, start, ramp_frames, false);
    for (uint32_t i = 0; i < silent_frames; ++i) {
        uint32_t at = (start + ramp_frames + i) % ring_frames * 2;
        ring[at] = ring[at + 1] = 0;
    }
}
