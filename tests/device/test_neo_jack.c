#include "check.h"
#include "board.h"
#include "dac.h"
#include "gpio.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "device/jack_sense.h"

static bool plugged;
static bool pull_bit;
static unsigned mux_changes, input_changes, pull_changes;
static uint32_t now_ms;
uint32_t hal_clock_milliseconds(void) { return now_ms; }
void gpio_set_pull(GpioPin pin, bool enabled)
{
    CHECK_EQUAL(0, pin.port); CHECK_EQUAL(13, pin.pin);
    if (pull_changes == 0) CHECK(enabled);
    pull_bit = enabled;
    pull_changes++;
}

void gpio_mux(GpioPin pin, uint32_t function)
{
    CHECK_EQUAL(0, pin.port);
    CHECK_EQUAL(13, pin.pin);
    CHECK_EQUAL(GPIO_FUNCTION_GPIO, function);
    mux_changes++;
}
void gpio_input(GpioPin pin)
{
    CHECK_EQUAL(0, pin.port);
    CHECK_EQUAL(13, pin.pin);
    input_changes++;
}
bool gpio_read(GpioPin pin)
{
    CHECK_EQUAL(0, pin.port);
    CHECK_EQUAL(13, pin.pin);
    return plugged;
}
uint32_t gpio_function(GpioPin pin) { (void)pin; return GPIO_FUNCTION_GPIO; }
bool gpio_is_output(GpioPin pin) { (void)pin; return false; }
bool gpio_pull_enabled(GpioPin pin) { (void)pin; return pull_bit; }
uint32_t gpio_input_bank(uint8_t port)
{
    return port == 0 ? (plugged ? 1u << 13 : 0) : port;
}
HalDacHeadphoneProbe dac_path_headphone_probe(void)
{
    return (HalDacHeadphoneProbe){.powered = true, .valid = true, .control = 0xC4, .status = 0x40};
}

static void neo_wired_detection_follows_the_real_pin_without_touching_dac_mute(void)
{
    plugged = true;
    CHECK(board_jack_poll());
    CHECK_EQUAL(HAL_AUDIO_JACK_SINGLE_ENDED, hal_audio_jack());
    plugged = false;
    now_ms = 10;
    CHECK(!board_jack_poll());
    now_ms = 159;
    CHECK(!board_jack_poll());
    CHECK_EQUAL(HAL_AUDIO_JACK_SINGLE_ENDED, hal_audio_jack());
    now_ms = 160;
    CHECK(board_jack_poll());
    CHECK_EQUAL(HAL_AUDIO_JACK_NONE, hal_audio_jack());
    /* A ghost high longer than the old 70 ms debounce must not reconnect. */
    plugged = true;
    now_ms = 200;
    CHECK(!board_jack_poll());
    now_ms = 800;
    CHECK(!board_jack_poll());
    CHECK_EQUAL(HAL_AUDIO_JACK_NONE, hal_audio_jack());
    plugged = false;
    now_ms = 900;
    CHECK(!board_jack_poll());
    plugged = true;
    now_ms = 1000;
    CHECK(!board_jack_poll());
    now_ms = 2199;
    CHECK(!board_jack_poll());
    now_ms = 2200;
    CHECK(board_jack_poll());
    CHECK_EQUAL(HAL_AUDIO_JACK_SINGLE_ENDED, hal_audio_jack());
    HalJackPins pins = hal_audio_jack_pins();
    CHECK(pins.single_ended.high);
    CHECK(pins.balanced.high);
    CHECK_EQUAL(1, mux_changes);
    CHECK_EQUAL(1, input_changes);
    CHECK_EQUAL(1, pull_changes);
    CHECK(pull_bit);
#ifdef ECHO_JACK_DIAGNOSTICS
    CHECK_EQUAL(4, pins.raw_changes);
    CHECK_EQUAL(2, pins.stable_changes);
    CHECK_EQUAL(1u << 13, pins.input_banks[0]);
    CHECK_EQUAL(1, pins.input_banks[1]);
    CHECK_EQUAL(2, pins.input_banks[2]);
    CHECK(pins.dac_probe.valid);
    CHECK_EQUAL(0xC4, pins.dac_probe.control);
    CHECK_EQUAL(0x40, pins.dac_probe.status);
    hal_audio_jack_set_pull_bit(true);
    CHECK(hal_audio_jack_pins().pull_bit);
    hal_audio_jack_set_pull_bit(false);
    CHECK(!hal_audio_jack_pins().pull_bit);
    CHECK_EQUAL(3, pull_changes);
    CHECK_EQUAL(1, mux_changes);
    CHECK_EQUAL(1, input_changes);
#endif
}
int main(void)
{
    const TestCase cases[] = {TEST_CASE(neo_wired_detection_follows_the_real_pin_without_touching_dac_mute)};
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
