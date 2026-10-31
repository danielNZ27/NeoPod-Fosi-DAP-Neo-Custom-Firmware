#include "app_screens.h"

#include <stddef.h>

#include "diagnostics_rows.h"
#include "hal/audio.h"

enum {
    RELEASE_DIAGNOSTICS_COUNT =
#ifdef ECHO_JACK_DIAGNOSTICS
        10
#else
        9
#endif
};

static MenuItem value_item(const char *label, const char *(*value)(const void *context))
{
    return (MenuItem){.label = label, .select = NULL, .context = NULL, .value = value};
}

#ifdef ECHO_JACK_DIAGNOSTICS
static ScreenAction toggle_jack_pull_bit(void *context)
{
    (void)context;
    hal_audio_jack_set_pull_bit(!hal_audio_jack_pins().pull_bit);
    return screen_action_none();
}
#endif

void diagnostics_menu_build(AppScreens *screens, AppOptions *options, const Theme *theme)
{
    (void)options;
    MenuItem *items = screens->diagnostics_items;
#ifdef ECHO_JACK_DIAGNOSTICS
    items[0] = value_item("DAC HP link", diagnostics_row_dac_hp_link);
    items[1] = value_item("DAC HP ctl", diagnostics_row_dac_hp_control);
    items[2] = value_item("DAC HP bits", diagnostics_row_dac_hp_status);
    items[3] = value_item("Jack raw", diagnostics_row_jack_single_ended);
    items[4] = (MenuItem){.label = "Jack pull bit", .select = toggle_jack_pull_bit,
                          .value = diagnostics_row_jack_pull_bit};
    items[5] = value_item("GPIO estimate", diagnostics_row_headphones);
    items[6] = value_item("Raw changes", diagnostics_row_jack_raw_changes);
    items[7] = value_item("Jack changes", diagnostics_row_jack_stable_changes);
    items[8] = value_item("Firmware", diagnostics_row_firmware);
    items[9] = value_item("Battery", diagnostics_row_battery);
#else
    items[0] = app_screens_open_item("Key test", test_pattern_screen_init(&screens->test_pattern));
    items[1] = value_item("Firmware", diagnostics_row_firmware);
    items[2] = value_item("Battery", diagnostics_row_battery);
    items[3] = value_item("Last start", diagnostics_row_start);
    items[4] = value_item("Last step", diagnostics_row_last_step);
    items[5] = value_item("Last battery", diagnostics_row_last_battery);
    items[6] = value_item("Last USB", diagnostics_row_usb_last);
    items[7] = value_item("Jack 3.5", diagnostics_row_jack_single_ended);
    items[8] = value_item("Headphones", diagnostics_row_headphones);
#endif
    app_screens_build_menu(&screens->diagnostics, "Diagnostics", items, RELEASE_DIAGNOSTICS_COUNT,
                           theme);
}
