#pragma once

#include <stdint.h>

#include "core/power_policy.h"
#include "player.h"
#include "screens/equalizer_screen.h"
#include "screens/bluetooth_screen.h"
#include "screens/stats_screen.h"
#include "settings_store.h"
#include "ui/choice_screen.h"
#include "ui/menu_screen.h"
#include "ui/theme.h"
#include "ui/toast.h"

enum {
    SETTINGS_CHOICE_COUNT = 14,
    SETTINGS_CATEGORY_COUNT = 5,
    SETTINGS_PLAYBACK_COUNT = 3,
    SETTINGS_AUDIO_COUNT = 4,
    SETTINGS_DISPLAY_COUNT = 6,
    SETTINGS_POWER_COUNT = 2,
    SETTINGS_SYSTEM_COUNT = 3,
    SETTINGS_ITEM_COUNT = SETTINGS_CATEGORY_COUNT
};

typedef struct SettingsContext {
    SettingsStore *store;
    Player *player;
    PowerPolicy *power;
    Toast *toast;
    bool *rescan_requested;
    int sleep_option;
} SettingsContext;

typedef struct SettingChoice {
    const char *title;
    const char *const *options;
    int option_count;
    int (*current)(const void *context);
    void (*choose)(void *context, int index);
    const char *(*describe)(int current);
} SettingChoice;

typedef struct SettingsScreens SettingsScreens;

typedef struct SettingsEntry {
    const SettingChoice *choice;
    SettingsScreens *owner;
} SettingsEntry;

struct SettingsScreens {
    SettingsContext context;
    const Theme *theme;
    MenuScreen menu;
    MenuScreen playback_menu;
    MenuScreen audio_menu;
    MenuScreen display_menu;
    MenuScreen power_menu;
    MenuScreen system_menu;
    ChoiceScreen choice;
    EqualizerScreen equalizer;
    BluetoothScreen bluetooth;
    StatsScreen stats;
    SettingsEntry entries[SETTINGS_CHOICE_COUNT];
    MenuItem items[SETTINGS_CATEGORY_COUNT];
    MenuItem playback_items[SETTINGS_PLAYBACK_COUNT];
    MenuItem audio_items[SETTINGS_AUDIO_COUNT];
    MenuItem display_items[SETTINGS_DISPLAY_COUNT];
    MenuItem power_items[SETTINGS_POWER_COUNT];
    MenuItem system_items[SETTINGS_SYSTEM_COUNT];
};

Screen *settings_screens_init(SettingsScreens *screens, SettingsContext context,
                              const Theme *theme);
