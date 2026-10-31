#include "board.h"
#include "device/jack_sense.h"
#include "gpio.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "dac.h"

static const GpioPin SINGLE_ENDED_SENSE = {0, 13};
static const GpioPin BALANCED_SENSE = {2, 8};

static bool pins_ready;
static HalJackPins boot_state;
static JackSense sense;
#ifdef ECHO_JACK_DIAGNOSTICS
static JackKind last_raw;
static uint32_t raw_changes, stable_changes;
#endif

static HalJackPin boot_pin(GpioPin pin)
{
    return (HalJackPin){.high = false,
                        .boot_function = (uint8_t)gpio_function(pin),
                        .boot_output = gpio_is_output(pin),
                        .pull = gpio_pull_enabled(pin)};
}

static void prepare_pins(void)
{
    if (pins_ready) {
        return;
    }
    boot_state = (HalJackPins){.single_ended = boot_pin(SINGLE_ENDED_SENSE),
                               .balanced = boot_pin(BALANCED_SENSE)};
    gpio_mux(SINGLE_ENDED_SENSE, GPIO_FUNCTION_GPIO);
    gpio_input(SINGLE_ENDED_SENSE);
#ifdef ECHO_BOARD_NEO
    /* Owner-confirmed on Neo: pull bit 1 gives stable unplug/replug detection.
     * Bit 0 floats/chatter when unplugged. Keep GPIO2.8 reserved for DAC mute. */
    gpio_set_pull(SINGLE_ENDED_SENSE, true);
#endif
#ifndef ECHO_BOARD_NEO
    gpio_mux(BALANCED_SENSE, GPIO_FUNCTION_GPIO);
    gpio_input(BALANCED_SENSE);
#endif
    pins_ready = true;
}

HalJackPins hal_audio_jack_pins(void)
{
    prepare_pins();
    HalJackPins pins = boot_state;
    pins.single_ended.high = gpio_read(SINGLE_ENDED_SENSE);
#ifdef ECHO_BOARD_NEO
    pins.balanced.high = true; /* Unused active-low input: inactive. GPIO2.8 is Neo mute. */
#else
    pins.balanced.high = gpio_read(BALANCED_SENSE);
#endif
#ifdef ECHO_JACK_DIAGNOSTICS
    pins.raw_changes = raw_changes;
    pins.pull_bit = gpio_pull_enabled(SINGLE_ENDED_SENSE);
    pins.stable_changes = stable_changes;
    for (uint8_t port = 0; port < 3; port++) {
        pins.input_banks[port] = gpio_input_bank(port);
    }
#ifdef ECHO_BOARD_NEO
    pins.dac_probe = dac_path_headphone_probe();
#endif
#endif
    return pins;
}

void hal_audio_jack_set_pull_bit(bool bit)
{
#if defined(ECHO_BOARD_NEO) && defined(ECHO_JACK_DIAGNOSTICS)
    prepare_pins();
    gpio_set_pull(SINGLE_ENDED_SENSE, bit);
#else
    (void)bit;
#endif
}

bool board_jack_poll(void)
{
    prepare_pins();
#ifdef ECHO_BOARD_NEO
    JackKind reading = gpio_read(SINGLE_ENDED_SENSE) ? JACK_SINGLE_ENDED : JACK_NONE;
#ifdef ECHO_JACK_DIAGNOSTICS
    bool started = sense.started;
    if (started && reading != last_raw) raw_changes++;
    last_raw = reading;
    bool changed = jack_sense_sample_timed(&sense, reading, hal_clock_milliseconds(), 150, 1200);
    if (started && changed) stable_changes++;
    return changed;
#else
    return jack_sense_sample_timed(&sense, reading, hal_clock_milliseconds(), 150, 1200);
#endif
#else
    return jack_sense_sample(&sense, jack_kind_of(gpio_read(SINGLE_ENDED_SENSE),
                                                gpio_read(BALANCED_SENSE)));
#endif
}

HalAudioJack hal_audio_jack(void)
{
    if (!sense.started) {
        board_jack_poll();
    }
    switch (sense.stable) {
    case JACK_SINGLE_ENDED:
        return HAL_AUDIO_JACK_SINGLE_ENDED;
    case JACK_BALANCED:
        return HAL_AUDIO_JACK_BALANCED;
    case JACK_NONE:
        break;
    }
    return HAL_AUDIO_JACK_NONE;
}
