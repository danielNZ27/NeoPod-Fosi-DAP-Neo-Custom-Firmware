#include "board.h"
#include "device/jack_sense.h"
#include "gpio.h"
#include "hal/audio.h"

static const GpioPin SINGLE_ENDED_SENSE = {0, 13};
static const GpioPin BALANCED_SENSE = {2, 8};

static bool pins_ready;
static HalJackPins boot_state;
static JackSense sense;

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
    pins.balanced.high = false; /* GPIO2.8 is Neo mute, never a jack input. */
#else
    pins.balanced.high = gpio_read(BALANCED_SENSE);
#endif
    return pins;
}

bool board_jack_poll(void)
{
    prepare_pins();
    return jack_sense_sample(
        &sense, jack_kind_of(gpio_read(SINGLE_ENDED_SENSE),
#ifdef ECHO_BOARD_NEO
                             false
#else
                             gpio_read(BALANCED_SENSE)
#endif
                             ));
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
