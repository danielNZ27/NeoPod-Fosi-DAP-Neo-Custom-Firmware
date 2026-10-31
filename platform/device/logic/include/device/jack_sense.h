#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum JackKind {
    JACK_NONE,
    JACK_SINGLE_ENDED,
    JACK_BALANCED
} JackKind;

enum {
    JACK_SENSE_SAMPLES = 7
};

typedef struct JackSense {
    bool started;
    JackKind stable;
    JackKind candidate;
    uint8_t matches;
    uint32_t candidate_since_ms;
} JackSense;

JackKind jack_kind_of(bool single_ended_high, bool balanced_high);
bool jack_sense_sample(JackSense *sense, JackKind reading);

/* Asymmetric, wall-clock debounce: a reconnect must remain quiet longer. */
bool jack_sense_sample_timed(JackSense *sense, JackKind reading, uint32_t now_ms,
                             uint32_t removal_ms, uint32_t insertion_ms);
