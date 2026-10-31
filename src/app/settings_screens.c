#include "settings_screens.h"

#include "hal/audio.h"
#include "hal/clock.h"
#include "default_theme.h"

enum {
    BRIGHTNESS_LEVEL_COUNT = SETTINGS_BRIGHTNESS_MAX - SETTINGS_BRIGHTNESS_MIN + 1,
    MILLISECONDS_PER_MINUTE = 60000,
    CHOICE_SHUFFLE = 0,
    CHOICE_REPEAT = 1,
    CHOICE_SLEEP = 2,
    CHOICE_AUTO_OFF = 3,
    CHOICE_TIMEOUT = 4,
    CHOICE_BRIGHTNESS = 5,
    CHOICE_GAIN = 6,
    CHOICE_OUTPUT = 7,
    CHOICE_START = 8,
    CHOICE_REPLAY_GAIN = 9,
    CHOICE_USB_MODE = 10,
    CHOICE_COLOR = 11,
    CHOICE_SIDE_BUTTONS = 12,
    CHOICE_THEME = 13
};

static const char *const SHUFFLE_OPTIONS[SHUFFLE_MODE_COUNT] = {"Off", "Songs", "Albums"};
static const char *const THEME_OPTIONS[THEME_COUNT] = {"NeoPod", "iPod Classic"};
static const char *const REPEAT_OPTIONS[REPEAT_MODE_COUNT] = {"Off", "One", "All"};
static const char *const COLOR_OPTIONS[BUTTON_COLOR_COUNT] = {
    "Blue", "Green", "Teal", "Purple", "Pink", "Red", "Orange", "Gold", "Silver"
};
static const char *const SIDE_BUTTON_OPTIONS[SIDE_BUTTONS_COUNT] = {
    "Volume up/down", "Previous/next track", "Hybrid"
};
static const char *const SLEEP_OPTIONS[SLEEP_TIMER_OPTION_COUNT] = {
    "Off", "15 min", "30 min", "45 min", "60 min", "90 min", "120 min"};
static const char *const SLEEP_MESSAGES[SLEEP_TIMER_OPTION_COUNT] = {
    "Sleep timer off",       "Turning off in 15 min", "Turning off in 30 min",
    "Turning off in 45 min", "Turning off in 60 min", "Turning off in 90 min",
    "Turning off in 120 min"};
static const char *const AUTO_OFF_OPTIONS[AUTO_OFF_OPTION_COUNT] = {"Off", "1 min", "5 min",
                                                                    "10 min"};
static const char *const TIMEOUT_OPTIONS[SCREEN_TIMEOUT_OPTION_COUNT] = {
    "10 s", "20 s", "30 s", "1 min", "2 min", "5 min", "Never"};
static const char *const BRIGHTNESS_OPTIONS[BRIGHTNESS_LEVEL_COUNT] = {"1", "2", "3", "4", "5",
                                                                       "6", "7", "8", "9", "10"};
static const char *const GAIN_OPTIONS[GAIN_LEVEL_COUNT] = {"Low", "High"};
static const char *const OUTPUT_OPTIONS[OUTPUT_JACK_COUNT] = {"Auto", "3.5 mm", "4.4 mm"};
static const char *const OUTPUT_MESSAGES[OUTPUT_JACK_COUNT] = {
    "Volume follows the plugged jack", "Using 3.5 mm volume levels", "Using 4.4 mm volume levels"};
static const char *const START_OPTIONS[START_SCREEN_COUNT] = {
    "Main menu", "Now Playing", "Albums", "Artists", "Favorites", "Playlists", "Cover Flow"};
static const char *const USB_MODE_OPTIONS[USB_MODE_COUNT] = {"Storage", "DAC"};
static const char *const USB_MODE_MESSAGES[USB_MODE_COUNT] = {"USB shows the player's drives",
                                                              "USB plays the computer's sound"};
static const char *const REPLAY_GAIN_OPTIONS[REPLAY_GAIN_MODE_COUNT] = {"Off", "Track", "Album"};

static const Settings *settings_of(const void *context)
{
    const SettingsContext *settings = context;
    return &settings->store->settings;
}

static Settings *editable_settings(void *context)
{
    SettingsContext *settings = context;
    return &settings->store->settings;
}

static void save(SettingsContext *context)
{
    settings_store_capture(context->store, player_status(context->player));
    if (!settings_store_save(context->store)) {
        toast_show(context->toast, "Could not save settings", hal_clock_milliseconds());
    }
}

static void apply_and_save(SettingsContext *context)
{
    settings_store_apply_hardware(&context->store->settings);
    save(context);
}

static int current_shuffle(const void *context)
{
    const SettingsContext *settings = context;
    return (int)player_status(settings->player).shuffle;
}

static void choose_shuffle(void *context, int index)
{
    SettingsContext *settings = context;
    player_set_shuffle(settings->player, (ShuffleMode)index, hal_clock_microseconds());
    save(settings);
}

static int current_repeat(const void *context)
{
    const SettingsContext *settings = context;
    return (int)player_status(settings->player).repeat;
}

static void choose_repeat(void *context, int index)
{
    SettingsContext *settings = context;
    player_set_repeat(settings->player, (RepeatMode)index);
    save(settings);
}

static int current_sleep(const void *context)
{
    const SettingsContext *settings = context;
    return settings->power->sleep_timer_active ? settings->sleep_option : 0;
}

static void choose_sleep(void *context, int index)
{
    SettingsContext *settings = context;
    uint32_t now_ms = hal_clock_milliseconds();
    uint32_t minutes = settings_sleep_option_minutes(index);
    settings->sleep_option = index;
    if (minutes == 0) {
        power_policy_cancel_sleep_timer(settings->power);
    } else {
        power_policy_start_sleep_timer(settings->power, minutes * MILLISECONDS_PER_MINUTE, now_ms);
    }
    toast_show(settings->toast, SLEEP_MESSAGES[index], now_ms);
}

static int current_timeout(const void *context)
{
    return settings_of(context)->screen_timeout;
}

static void choose_timeout(void *context, int index)
{
    SettingsContext *settings = context;
    editable_settings(context)->screen_timeout = (uint8_t)index;
    power_policy_set_timeout(settings->power, settings_screen_timeout_ms(settings_of(context)));
    save(settings);
}

static int current_auto_off(const void *context)
{
    return settings_of(context)->auto_off;
}

static void choose_auto_off(void *context, int index)
{
    SettingsContext *settings = context;
    editable_settings(context)->auto_off = (uint8_t)index;
    power_policy_set_auto_off(settings->power, settings_auto_off_ms(settings_of(context)),
                              hal_clock_milliseconds());
    save(settings);
}

static int current_brightness(const void *context)
{
    return settings_of(context)->brightness - SETTINGS_BRIGHTNESS_MIN;
}

static void choose_brightness(void *context, int index)
{
    editable_settings(context)->brightness = (uint8_t)(index + SETTINGS_BRIGHTNESS_MIN);
    apply_and_save(context);
}

static int current_gain(const void *context)
{
    return (int)settings_of(context)->gain;
}

static void choose_gain(void *context, int index)
{
    editable_settings(context)->gain = (GainLevel)index;
    apply_and_save(context);
}

static int current_output(const void *context)
{
    return (int)settings_of(context)->output;
}

static void choose_output(void *context, int index)
{
    SettingsContext *settings = context;
    editable_settings(context)->output = (OutputJack)index;
    apply_and_save(context);
    toast_show(settings->toast, OUTPUT_MESSAGES[index], hal_clock_milliseconds());
}

static const char *describe_output(int current)
{
    HalAudioJack jack = hal_audio_jack();
    if (current != OUTPUT_AUTO || jack == HAL_AUDIO_JACK_NONE) {
        return OUTPUT_OPTIONS[current];
    }
    return jack == HAL_AUDIO_JACK_BALANCED ? "Auto (4.4 mm)" : "Auto (3.5 mm)";
}

static int current_start(const void *context)
{
    return (int)settings_of(context)->start_screen;
}

static void choose_start(void *context, int index)
{
    editable_settings(context)->start_screen = (StartScreen)index;
    save(context);
}

static int current_usb_mode(const void *context)
{
    return (int)settings_of(context)->usb_mode;
}

static void choose_usb_mode(void *context, int index)
{
    SettingsContext *settings = context;
    editable_settings(context)->usb_mode = (UsbMode)index;
    apply_and_save(context);
    toast_show(settings->toast, USB_MODE_MESSAGES[index], hal_clock_milliseconds());
}

static int current_replay_gain(const void *context)
{
    return (int)settings_of(context)->replay_gain_mode;
}

static void choose_replay_gain(void *context, int index)
{
    editable_settings(context)->replay_gain_mode = (ReplayGainMode)index;
    apply_and_save(context);
}

static ScreenAction open_equalizer(void *context)
{
    SettingsScreens *screens = context;
    return screen_action_push(equalizer_screen_init(&screens->equalizer, screens->context.store, screens->theme));
}

static const char *equalizer_value(const void *context)
{
    const SettingsScreens *screens = context;
    const Settings *settings = &screens->context.store->settings;
    return settings->eq_enabled ? settings_preset_name(settings, settings->eq_preset) : "Off";
}

static int current_color(const void *context)
{
    return settings_of(context)->button_color;
}

static void choose_color(void *context, int index)
{
    SettingsContext *settings = context;
    editable_settings(context)->button_color = (uint8_t)index;
    app_theme_apply((uint8_t)index);
    save(settings);
}

static int current_side_buttons(const void *context)
{
    return (int)settings_of(context)->side_buttons;
}

static int current_theme(const void *context)
{
    return settings_of(context)->theme;
}

static void choose_theme(void *context, int index)
{
    SettingsContext *settings = context;
    editable_settings(context)->theme = (uint8_t)index;
    app_theme_select((uint8_t)index, settings_of(context)->button_color);
    save(settings);
}

static void choose_side_buttons(void *context, int index)
{
    editable_settings(context)->side_buttons = (SideButtons)index;
    save(context);
}

static const SettingChoice CHOICES[SETTINGS_CHOICE_COUNT] = {
    [CHOICE_THEME] = {"Theme", THEME_OPTIONS, THEME_COUNT, current_theme, choose_theme, NULL},
    [CHOICE_SHUFFLE] = {"Shuffle", SHUFFLE_OPTIONS, SHUFFLE_MODE_COUNT, current_shuffle, choose_shuffle, NULL},
    [CHOICE_REPEAT] = {"Repeat", REPEAT_OPTIONS, REPEAT_MODE_COUNT, current_repeat, choose_repeat, NULL},
    [CHOICE_SLEEP] = {"Sleep timer", SLEEP_OPTIONS, SLEEP_TIMER_OPTION_COUNT, current_sleep, choose_sleep, NULL},
    [CHOICE_AUTO_OFF] = {"Auto off", AUTO_OFF_OPTIONS, AUTO_OFF_OPTION_COUNT, current_auto_off, choose_auto_off, NULL},
    [CHOICE_TIMEOUT] = {"Screen timeout", TIMEOUT_OPTIONS, SCREEN_TIMEOUT_OPTION_COUNT, current_timeout,
                        choose_timeout, NULL},
    [CHOICE_BRIGHTNESS] = {"Brightness", BRIGHTNESS_OPTIONS, BRIGHTNESS_LEVEL_COUNT, current_brightness,
                           choose_brightness, NULL},
    [CHOICE_GAIN] = {"Gain", GAIN_OPTIONS, GAIN_LEVEL_COUNT, current_gain, choose_gain, NULL},
    [CHOICE_OUTPUT] = {"Output", OUTPUT_OPTIONS, OUTPUT_JACK_COUNT, current_output, choose_output, describe_output},
    [CHOICE_START] = {"Start screen", START_OPTIONS, START_SCREEN_COUNT, current_start, choose_start, NULL},
    [CHOICE_REPLAY_GAIN] = {"ReplayGain", REPLAY_GAIN_OPTIONS, REPLAY_GAIN_MODE_COUNT, current_replay_gain,
                            choose_replay_gain, NULL},
    [CHOICE_USB_MODE] = {"USB mode", USB_MODE_OPTIONS, USB_MODE_COUNT, current_usb_mode, choose_usb_mode, NULL},
    [CHOICE_COLOR] = {"Button colour", COLOR_OPTIONS, BUTTON_COLOR_COUNT, current_color, choose_color, NULL},
    [CHOICE_SIDE_BUTTONS] = {"Side buttons", SIDE_BUTTON_OPTIONS, SIDE_BUTTONS_COUNT, current_side_buttons,
                             choose_side_buttons, NULL},
};

static ScreenAction open_choice(void *context)
{
    SettingsEntry *entry = context;
    SettingsScreens *screens = entry->owner;
    const SettingChoice *choice = entry->choice;
    ChoiceConfig config = {
        .title = choice->options == COLOR_OPTIONS ? "Colours" : choice->title,
        .options = choice->options,
        .option_count = choice->option_count,
        .current = choice->current,
        .choose = choice->choose,
        .context = &screens->context,
        .theme = screens->theme,
    };
    return screen_action_push(choice_screen_init(&screens->choice, config));
}

static const char *choice_value(const void *context)
{
    const SettingsEntry *entry = context;
    int current = entry->choice->current(&entry->owner->context);
    if (current < 0 || current >= entry->choice->option_count) {
        return "";
    }
    if (entry->choice->describe != NULL) {
        return entry->choice->describe(current);
    }
    return entry->choice->options[current];
}

static ScreenAction request_rescan(void *context)
{
    SettingsContext *settings = context;
    if (settings->rescan_requested != NULL) {
        *settings->rescan_requested = true;
    }
    return screen_action_none();
}

static MenuItem make_choice_item(SettingsScreens *screens, int choice_index)
{
    screens->entries[choice_index] = (SettingsEntry){.choice = &CHOICES[choice_index], .owner = screens};
    return (MenuItem){
        .label = CHOICES[choice_index].title,
        .select = open_choice,
        .context = &screens->entries[choice_index],
        .value = choice_value,
        .opens = true
    };
}

Screen *settings_screens_init(SettingsScreens *screens, SettingsContext context, const Theme *theme)
{
    screens->context = context;
    screens->theme = theme;

    /* 1. Playback Category */
    screens->playback_items[0] = make_choice_item(screens, CHOICE_SHUFFLE);
    screens->playback_items[1] = make_choice_item(screens, CHOICE_REPEAT);
    screens->playback_items[2] = make_choice_item(screens, CHOICE_REPLAY_GAIN);
    menu_screen_init(&screens->playback_menu, (MenuConfig){
        .title = "Playback",
        .items = screens->playback_items,
        .item_count = SETTINGS_PLAYBACK_COUNT,
        .theme = theme
    });

    /* 2. Audio Category */
    screens->audio_items[0] = (MenuItem){
        .label = "Equalizer",
        .select = open_equalizer,
        .context = screens,
        .value = equalizer_value,
        .opens = true
    };
    screens->audio_items[1] = make_choice_item(screens, CHOICE_GAIN);
    screens->audio_items[2] = make_choice_item(screens, CHOICE_OUTPUT);
    menu_screen_init(&screens->audio_menu, (MenuConfig){
        .title = "Audio",
        .items = screens->audio_items,
        .item_count = SETTINGS_AUDIO_COUNT,
        .theme = theme
    });

    /* 3. Display & Controls Category */
    screens->display_items[0] = make_choice_item(screens, CHOICE_BRIGHTNESS);
    screens->display_items[1] = make_choice_item(screens, CHOICE_TIMEOUT);
    screens->display_items[2] = make_choice_item(screens, CHOICE_COLOR);
    screens->display_items[3] = make_choice_item(screens, CHOICE_SIDE_BUTTONS);
    screens->display_items[4] = make_choice_item(screens, CHOICE_START);
    screens->display_items[5] = make_choice_item(screens, CHOICE_THEME);
    menu_screen_init(&screens->display_menu, (MenuConfig){
        .title = "Display & Controls",
        .items = screens->display_items,
        .item_count = SETTINGS_DISPLAY_COUNT,
        .theme = theme
    });

    /* 4. Power & Sleep Category */
    screens->power_items[0] = make_choice_item(screens, CHOICE_SLEEP);
    screens->power_items[1] = make_choice_item(screens, CHOICE_AUTO_OFF);
    menu_screen_init(&screens->power_menu, (MenuConfig){
        .title = "Power & Sleep",
        .items = screens->power_items,
        .item_count = SETTINGS_POWER_COUNT,
        .theme = theme
    });

    /* 5. System Category */
    screens->system_items[0] = make_choice_item(screens, CHOICE_USB_MODE);
    screens->system_items[1] = (MenuItem){
        .label = "Rescan library",
        .select = request_rescan,
        .context = &screens->context,
        .opens = false
    };
    menu_screen_init(&screens->system_menu, (MenuConfig){
        .title = "System",
        .items = screens->system_items,
        .item_count = SETTINGS_SYSTEM_COUNT,
        .theme = theme
    });

    /* Top-Level Settings Menu */
    screens->items[0] = (MenuItem){
        .label = "Playback",
        .select = menu_open_screen,
        .context = &screens->playback_menu.screen,
        .opens = true
    };
    screens->items[1] = (MenuItem){
        .label = "Audio",
        .select = menu_open_screen,
        .context = &screens->audio_menu.screen,
        .opens = true
    };
    screens->items[2] = (MenuItem){
        .label = "Display & Controls",
        .select = menu_open_screen,
        .context = &screens->display_menu.screen,
        .opens = true
    };
    screens->items[3] = (MenuItem){
        .label = "Power & Sleep",
        .select = menu_open_screen,
        .context = &screens->power_menu.screen,
        .opens = true
    };
    screens->items[4] = (MenuItem){
        .label = "System",
        .select = menu_open_screen,
        .context = &screens->system_menu.screen,
        .opens = true
    };

    MenuConfig menu = {.title = "Settings",
                       .items = screens->items,
                       .item_count = SETTINGS_CATEGORY_COUNT,
                       .theme = theme};
    return menu_screen_init(&screens->menu, menu);
}
