#include <string.h>
#include <stdio.h>

#include "check.h"
#include "core/hash.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "settings_screens.h"
#include "fixture_library.h"

static void put_checksum(uint8_t *bytes, int offset)
{
    uint32_t checksum = hash_update(HASH_START, bytes, (size_t)offset);
    for (int i = 0; i < 4; i++) bytes[offset + i] = (uint8_t)(checksum >> (8 * i));
}

static void presets_and_replay_gain_survive_reload(void)
{
    fake_hal_reset();
    SettingsStore original = {.settings = settings_defaults()};
    const int8_t gains[EQ_BAND_COUNT] = {4, 2, -1, 0, 3};
    memcpy(original.settings.eq_gains, gains, sizeof gains);
    CHECK(settings_save_user_preset(&original.settings, "My headphones", gains));
    original.settings.eq_enabled = true;
    original.settings.replay_gain_mode = REPLAY_GAIN_ALBUM;
    CHECK(settings_store_save(&original));
    SettingsStore restored;
    settings_store_load(&restored);
    CHECK_EQUAL(1, restored.settings.eq_user_preset_count);
    CHECK(strcmp("My headphones", settings_preset_name(&restored.settings, restored.settings.eq_preset)) == 0);
    settings_store_apply_hardware(&restored.settings);
    CHECK(fake_hal_audio_eq_enabled());
    CHECK_EQUAL(REPLAY_GAIN_ALBUM, hal_audio_replay_gain_mode());
    CHECK(memcmp(gains, fake_hal_audio_eq_gains(), sizeof gains) == 0);
}

static void legacy_settings_are_migrated(void)
{
    fake_hal_reset();
    Settings old = settings_defaults();
    old.volume = 23;
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&old, bytes);
    bytes[4] = 4;
    put_checksum(bytes, 20);
    fake_hal_add_file(SETTINGS_STORE_PATH, bytes, SETTINGS_LEGACY_FILE_SIZE);
    SettingsStore restored;
    settings_store_load(&restored);
    CHECK_EQUAL(23, restored.settings.volume);
    CHECK(!restored.settings.eq_enabled);
    CHECK_EQUAL(REPLAY_GAIN_OFF, restored.settings.replay_gain_mode);
}

static void invalid_preset_data_is_rejected(void)
{
    Settings settings = settings_defaults();
    uint8_t bytes[SETTINGS_FILE_SIZE];
    settings_encode(&settings, bytes);
    bytes[24] = EQ_USER_PRESET_CAPACITY + 1;
    put_checksum(bytes, SETTINGS_FILE_SIZE - 4);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
    settings_encode(&settings, bytes);
    bytes[25] = REPLAY_GAIN_MODE_COUNT;
    put_checksum(bytes, SETTINGS_FILE_SIZE - 4);
    CHECK(!settings_decode(bytes, sizeof bytes, &settings));
    CHECK(!settings_decode(bytes, 0, &settings));
}

static const MenuItem *find_item(const SettingsScreens *screens, const char *label)
{
    const MenuItem *arrays[] = {
        screens->items,
        screens->playback_items,
        screens->audio_items,
        screens->display_items,
        screens->power_items,
        screens->system_items
    };
    const int counts[] = {
        SETTINGS_CATEGORY_COUNT,
        SETTINGS_PLAYBACK_COUNT,
        SETTINGS_AUDIO_COUNT,
        SETTINGS_DISPLAY_COUNT,
        SETTINGS_POWER_COUNT,
        SETTINGS_SYSTEM_COUNT
    };
    for (int a = 0; a < 6; a++) {
        for (int i = 0; i < counts[a]; i++) {
            if (strcmp(arrays[a][i].label, label) == 0) {
                return &arrays[a][i];
            }
        }
    }
    return NULL;
}

static void audio_settings_are_reachable(void)
{
    fake_hal_reset();
    SettingsStore store = {.settings = settings_defaults()};
    SettingsScreens screens;
    (void)settings_screens_init(&screens, (SettingsContext){.store = &store}, &DEFAULT_THEME);
    CHECK_EQUAL(SETTINGS_CATEGORY_COUNT, screens.menu.config.item_count);
    CHECK(find_item(&screens, "Playback") != NULL);
    CHECK(find_item(&screens, "Audio") != NULL);
    CHECK(find_item(&screens, "Display & Controls") != NULL);
    CHECK(find_item(&screens, "Power & Sleep") != NULL);
    CHECK(find_item(&screens, "System") != NULL);

    const MenuItem *eq = find_item(&screens, "Equalizer");
    const MenuItem *rg = find_item(&screens, "ReplayGain");
    CHECK(eq != NULL);
    CHECK(rg != NULL);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, eq->select(eq->context).type);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, rg->select(rg->context).type);
}

static void button_colour_picker_applies_immediately_and_saves(void)
{
    fake_hal_reset();
    SettingsStore store = {.settings = settings_defaults()};
    static FixtureLibrary fixture;
    Player player;
    player_init(&player, fixture_library_open(&fixture));
    SettingsScreens screens;
    app_theme_apply(0);
    Color blue = APP_THEME.colors.selection_background;
    settings_screens_init(&screens, (SettingsContext){.store = &store, .player = &player}, &APP_THEME);
    const MenuItem *item = find_item(&screens, "Button colour");
    CHECK(item != NULL);
    if (!item) return;
    Screen *choice = item->select(item->context).target;
    CHECK(choice != NULL);
    if (!choice) return;
    CHECK_EQUAL(BUTTON_COLOR_COUNT, screens.choice.config.option_count);
    CHECK(strcmp("Blue", screens.choice.config.options[0]) == 0);
    choice->enter(choice->context, 0);
    choice->handle_key(choice->context, (KeyEvent){KEY_WHEEL_DOWN, KEY_GESTURE_PRESS}, 10);
    ScreenAction action = choice->handle_key(choice->context, (KeyEvent){KEY_PLAY, KEY_GESTURE_PRESS}, 20);
    CHECK_EQUAL(SCREEN_ACTION_POP, action.type);
    CHECK_EQUAL(1, store.settings.button_color);
    CHECK(APP_THEME.colors.selection_background != blue);
    CHECK_EQUAL(blue, DEFAULT_THEME.colors.selection_background);
    SettingsStore restored;
    settings_store_load(&restored);
    CHECK_EQUAL(1, restored.settings.button_color);
    Color colors[BUTTON_COLOR_COUNT];
    for (uint8_t index = 0; index < BUTTON_COLOR_COUNT; index++) {
        app_theme_apply(index);
        colors[index] = APP_THEME.colors.selection_background;
        for (uint8_t earlier = 0; earlier < index; earlier++)
            CHECK(colors[earlier] != colors[index]);
    }
    app_theme_apply(BUTTON_COLOR_COUNT);
    CHECK_EQUAL(blue, APP_THEME.colors.selection_background);
}

static void side_buttons_setting_offers_three_modes_and_saves_hybrid(void)
{
    fake_hal_reset();
    SettingsStore store = {.settings = settings_defaults()};
    static FixtureLibrary fixture;
    Player player;
    player_init(&player, fixture_library_open(&fixture));
    SettingsScreens screens;
    settings_screens_init(&screens, (SettingsContext){.store = &store, .player = &player}, &DEFAULT_THEME);
    const MenuItem *item = find_item(&screens, "Side buttons");
    CHECK(item != NULL);
    if (!item) return;
    Screen *choice = item->select(item->context).target;
    CHECK(choice != NULL);
    if (!choice) return;
    CHECK_EQUAL(3, screens.choice.config.option_count);
    CHECK(strcmp("Volume up/down", screens.choice.config.options[0]) == 0);
    CHECK(strcmp("Previous/next track", screens.choice.config.options[1]) == 0);
    CHECK(strcmp("Hybrid", screens.choice.config.options[2]) == 0);
    choice->enter(choice->context, 0);
    choice->handle_key(choice->context, (KeyEvent){KEY_WHEEL_DOWN, KEY_GESTURE_PRESS}, 10);
    choice->handle_key(choice->context, (KeyEvent){KEY_WHEEL_DOWN, KEY_GESTURE_PRESS}, 15);
    choice->handle_key(choice->context, (KeyEvent){KEY_PLAY, KEY_GESTURE_PRESS}, 20);
    CHECK_EQUAL(SIDE_BUTTONS_HYBRID, store.settings.side_buttons);
    SettingsStore restored;
    settings_store_load(&restored);
    CHECK_EQUAL(SIDE_BUTTONS_HYBRID, restored.settings.side_buttons);
}

static void render_settings_choice(const char *path, const char *label)
{
    fake_hal_reset();
    SettingsStore store = {.settings = settings_defaults()};
    static FixtureLibrary fixture;
    Player player;
    player_init(&player, fixture_library_open(&fixture));
    store.settings.button_color = 1;
    app_theme_apply(1);
    SettingsScreens screens;
    settings_screens_init(&screens, (SettingsContext){.store = &store, .player = &player}, &APP_THEME);
    const MenuItem *item = find_item(&screens, label);
    if (!item) return;
    Screen *choice = item->select(item->context).target;
    if (!choice) return;
if (!choice) return;
    choice->enter(choice->context, 0);
    screens.choice.list.selected = 1;
    static Color pixels[240 * 320];
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, 240, 320});
    choice->draw(choice->context, &canvas);
    FILE *out = fopen(path, "wb");
    CHECK(out != NULL);
    if (!out) return;
    fprintf(out, "P6\n240 320\n255\n");
    for (unsigned i = 0; i < 240 * 320; i++) {
        Color c = pixels[i];
        unsigned char rgb[3] = {(unsigned char)((c >> 11) * 255 / 31),
            (unsigned char)(((c >> 5) & 63) * 255 / 63),
            (unsigned char)((c & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, out);
    }
    fclose(out);
    app_theme_apply(0);
}

int main(int argc, char **argv)
{
    const TestCase cases[] = {
        TEST_CASE(side_buttons_setting_offers_three_modes_and_saves_hybrid),
        TEST_CASE(button_colour_picker_applies_immediately_and_saves),
        TEST_CASE(presets_and_replay_gain_survive_reload),
        TEST_CASE(legacy_settings_are_migrated),
        TEST_CASE(invalid_preset_data_is_rejected),
        TEST_CASE(audio_settings_are_reachable),
    };
    int result = test_run_all(cases, TEST_CASE_COUNT(cases));
    if (argc == 3) {
        render_settings_choice(argv[1], "Button colour");
        render_settings_choice(argv[2], "Side buttons");
    }
    return result;
}
