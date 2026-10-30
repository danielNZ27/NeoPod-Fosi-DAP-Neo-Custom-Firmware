#include <string.h>

#include "check.h"
#include "core/hash.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "settings_screens.h"

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

static void audio_settings_are_reachable(void)
{
    fake_hal_reset();
    SettingsStore store = {.settings = settings_defaults()};
    SettingsScreens screens;
    (void)settings_screens_init(&screens, (SettingsContext){.store = &store}, &DEFAULT_THEME);
    int found = 0;
    for (int i = 0; i < screens.menu.config.item_count; i++) {
        const MenuItem *item = &screens.items[i];
        CHECK(item->label != NULL);
        if (strcmp(item->label, "Equalizer") == 0 || strcmp(item->label, "ReplayGain") == 0) {
            CHECK_EQUAL(SCREEN_ACTION_PUSH, item->select(item->context).type);
            found++;
        }
    }
    CHECK_EQUAL(2, found);
}

int main(void)
{
    const TestCase cases[] = {
        TEST_CASE(presets_and_replay_gain_survive_reload),
        TEST_CASE(legacy_settings_are_migrated),
        TEST_CASE(invalid_preset_data_is_rejected),
        TEST_CASE(audio_settings_are_reachable),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}
