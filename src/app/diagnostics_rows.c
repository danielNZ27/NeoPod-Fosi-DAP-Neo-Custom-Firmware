#include "diagnostics_rows.h"

#include "diagnostics_values.h"
#include "hal/audio.h"
#include "hal/power.h"
#include "hal/system.h"
#include "core/string_builder.h"

const char *diagnostics_row_firmware(const void *context)
{
    (void)context;
    return hal_system_version();
}

static char battery_text[DIAGNOSTICS_VALUE_CAPACITY];

const char *diagnostics_row_battery(const void *context)
{
    (void)context;
    diagnostics_battery_text(hal_power_battery(), battery_text, sizeof battery_text);
    return battery_text;
}

static char jack_single_ended_text[DIAGNOSTICS_VALUE_CAPACITY];
static char jack_balanced_text[DIAGNOSTICS_VALUE_CAPACITY];

const char *diagnostics_row_jack_single_ended(const void *context)
{
    (void)context;
    diagnostics_jack_pin_text(hal_audio_jack_pins().single_ended, jack_single_ended_text,
                              sizeof jack_single_ended_text);
    return jack_single_ended_text;
}

const char *diagnostics_row_jack_balanced(const void *context)
{
    (void)context;
    diagnostics_jack_pin_text(hal_audio_jack_pins().balanced, jack_balanced_text,
                              sizeof jack_balanced_text);
    return jack_balanced_text;
}

static char start_text[DIAGNOSTICS_VALUE_CAPACITY];

const char *diagnostics_row_start(const void *context)
{
    (void)context;
    diagnostics_start_text(hal_system_start_info(), start_text, sizeof start_text);
    return start_text;
}

static char last_step_text[DIAGNOSTICS_VALUE_CAPACITY];

const char *diagnostics_row_last_step(const void *context)
{
    (void)context;
    diagnostics_last_step_text(hal_system_start_info(), last_step_text, sizeof last_step_text);
    return last_step_text;
}

static char last_battery_text[DIAGNOSTICS_VALUE_CAPACITY];

const char *diagnostics_row_last_battery(const void *context)
{
    (void)context;
    diagnostics_last_battery_text(hal_system_start_info(), last_battery_text,
                                  sizeof last_battery_text);
    return last_battery_text;
}

static char usb_last_text[DIAGNOSTICS_VALUE_CAPACITY];

const char *diagnostics_row_usb_last(const void *context)
{
    (void)context;
    diagnostics_usb_last_text(hal_system_usb_last(), usb_last_text, sizeof usb_last_text);
    return usb_last_text;
}

const char *diagnostics_row_headphones(const void *context)
{
    (void)context;
    return hal_audio_jack() == HAL_AUDIO_JACK_NONE ? "Unplugged" : "Plugged in";
}

const char *diagnostics_row_jack_raw_changes(const void *context)
{
    (void)context;
    static char text[12];
    StringBuilder builder = string_builder_make(text, sizeof text);
    string_builder_append_unsigned(&builder, hal_audio_jack_pins().raw_changes);
    return text;
}

const char *diagnostics_row_jack_stable_changes(const void *context)
{
    (void)context;
    static char text[12];
    StringBuilder builder = string_builder_make(text, sizeof text);
    string_builder_append_unsigned(&builder, hal_audio_jack_pins().stable_changes);
    return text;
}

static const char *gpio_input_text(unsigned port)
{
    static char text[3][9];
    static const char HEX[] = "0123456789ABCDEF";
    uint32_t value = hal_audio_jack_pins().input_banks[port];
    for (unsigned digit = 0; digit < 8; digit++) {
        text[port][digit] = HEX[value >> (28 - 4 * digit) & 15u];
    }
    text[port][8] = '\0';
    return text[port];
}

const char *diagnostics_row_gpio0_inputs(const void *context)
{
    (void)context;
    return gpio_input_text(0);
}

const char *diagnostics_row_gpio1_inputs(const void *context)
{
    (void)context;
    return gpio_input_text(1);
}

const char *diagnostics_row_gpio2_inputs(const void *context)
{
    (void)context;
    return gpio_input_text(2);
}

const char *diagnostics_row_dac_hp_link(const void *context)
{
    (void)context;
    HalDacHeadphoneProbe probe = hal_audio_jack_pins().dac_probe;
    return !probe.powered ? "Start music" : probe.valid ? "Ready" : "Read failed";
}

static const char *dac_hp_byte(bool control)
{
    static const char HEX[] = "0123456789ABCDEF";
    static char text[2][5];
    HalDacHeadphoneProbe probe = hal_audio_jack_pins().dac_probe;
    if (!probe.powered || !probe.valid) return "--";
    unsigned index = control ? 0 : 1;
    uint8_t value = control ? probe.control : probe.status;
    text[index][0] = '0'; text[index][1] = 'x';
    text[index][2] = HEX[value >> 4]; text[index][3] = HEX[value & 15u];
    text[index][4] = '\0';
    return text[index];
}

const char *diagnostics_row_dac_hp_control(const void *context)
{
    (void)context;
    return dac_hp_byte(true);
}

const char *diagnostics_row_dac_hp_status(const void *context)
{
    (void)context;
    return dac_hp_byte(false);
}

const char *diagnostics_row_jack_pull_bit(const void *context)
{
    (void)context;
    return hal_audio_jack_pins().pull_bit ? "1" : "0";
}
