#include "device/jack_sense.h"

JackKind jack_kind_of(bool single_ended_high, bool balanced_high)
{
    if (!balanced_high) {
        return JACK_BALANCED;
    }
    return single_ended_high ? JACK_SINGLE_ENDED : JACK_NONE;
}

bool jack_sense_sample(JackSense *sense, JackKind reading)
{
    if (!sense->started) {
        *sense = (JackSense){.started = true, .stable = reading, .candidate = reading};
        return true;
    }
    if (reading == sense->stable) {
        sense->matches = 0;
        return false;
    }
    if (reading != sense->candidate) {
        sense->candidate = reading;
        sense->matches = 0;
    }
    sense->matches++;
    if (sense->matches < JACK_SENSE_SAMPLES) {
        return false;
    }
    sense->stable = reading;
    sense->matches = 0;
    return true;
}

bool jack_sense_sample_timed(JackSense *sense, JackKind reading, uint32_t now_ms,
                             uint32_t removal_ms, uint32_t insertion_ms)
{
    if (!sense->started) {
        *sense = (JackSense){.started = true, .stable = reading, .candidate = reading,
                            .candidate_since_ms = now_ms};
        return true;
    }
    if (reading == sense->stable || reading != sense->candidate) {
        sense->candidate = reading;
        sense->candidate_since_ms = now_ms;
        return false;
    }
    uint32_t required = reading == JACK_NONE ? removal_ms : insertion_ms;
    if (now_ms - sense->candidate_since_ms < required) return false;
    sense->stable = reading;
    return true;
}
