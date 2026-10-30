#include "settings/settings.h"

#include <stddef.h>

#include "core/hash.h"

enum {
    VERSION = 6,
    FIFTH_VERSION = 5,
    FOURTH_VERSION = 4,
    THIRD_VERSION = 3,
    SECOND_VERSION = 2,
    FIRST_VERSION = 1,
    MAGIC_SIZE = 4,
    VERSION_FIELD = 4,
    BRIGHTNESS_FIELD = 6,
    TIMEOUT_FIELD = 7,
    GAIN_FIELD = 8,
    OUTPUT_FIELD = 9,
    START_FIELD = 10,
    SHUFFLE_FIELD = 11,
    REPEAT_FIELD = 12,
    VOLUME_FIELD = 13,
    AUTO_OFF_FIELD = 14,
    USB_MODE_FIELD = 15,
    DAC_VOLUME_FIELD = 16,
    EQ_ENABLED_FIELD = 17,
    EQ_PRESET_FIELD = 18,
    EQ_GAINS_FIELD = 19,
    EQ_USER_COUNT_FIELD = 24,
    REPLAY_GAIN_FIELD = 25,
    USER_PRESETS_BASE = 28,
    USER_PRESET_RECORD_SIZE = 24,
    LEGACY_CHECKSUM_FIELD = 20,
    CHECKSUM_FIELD = 252,
    DEFAULT_BRIGHTNESS = 7,
    DEFAULT_TIMEOUT_OPTION = 2,
    DEFAULT_VOLUME = 12,
    DEFAULT_AUTO_OFF_OPTION = 2,
    MILLISECONDS_PER_MINUTE = 60000,
    MILLISECONDS_PER_SECOND = 1000,
    BITS_PER_BYTE = 8
};

static const uint8_t MAGIC[MAGIC_SIZE] = {'E', 'M', 'S', 'T'};
static const uint16_t TIMEOUT_SECONDS[SCREEN_TIMEOUT_OPTION_COUNT] = {10, 20, 30, 60, 120, 300, 0};
static const uint8_t SLEEP_MINUTES[SLEEP_TIMER_OPTION_COUNT] = {0, 15, 30, 45, 60, 90, 120};
static const uint8_t AUTO_OFF_MINUTES[AUTO_OFF_OPTION_COUNT] = {0, 1, 5, 10};

Settings settings_defaults(void)
{
    return (Settings){
        .brightness = DEFAULT_BRIGHTNESS,
        .screen_timeout = DEFAULT_TIMEOUT_OPTION,
        .gain = GAIN_LOW,
        .output = OUTPUT_AUTO,
        .start_screen = START_MAIN_MENU,
        .shuffle = SHUFFLE_OFF,
        .repeat = REPEAT_OFF,
        .volume = DEFAULT_VOLUME,
        .auto_off = DEFAULT_AUTO_OFF_OPTION,
        .usb_mode = USB_MODE_STORAGE,
        .dac_volume = SETTINGS_DEFAULT_DAC_VOLUME,
        .eq_enabled = false,
        .eq_preset = 0,
        .eq_gains = {0, 0, 0, 0, 0},
        .eq_user_preset_count = 0,
        .replay_gain_mode = REPLAY_GAIN_OFF,
    };
}

int settings_find_user_preset_by_id(const Settings *settings, uint8_t id)
{
    for (int index = 0; index < settings->eq_user_preset_count; index++) {
        if (settings->eq_user_presets[index].id == id) {
            return index;
        }
    }
    return -1;
}

int settings_find_user_preset_by_name(const Settings *settings, const char *name)
{
    if (name == NULL) {
        return -1;
    }
    for (int index = 0; index < settings->eq_user_preset_count; index++) {
        const char *preset_name = settings->eq_user_presets[index].name;
        int char_idx = 0;
        bool match = true;
        while (preset_name[char_idx] != '\0' || name[char_idx] != '\0') {
            if (preset_name[char_idx] != name[char_idx]) {
                match = false;
                break;
            }
            char_idx++;
        }
        if (match) {
            return index;
        }
    }
    return -1;
}

const char *settings_preset_name(const Settings *settings, uint8_t preset_id)
{
    if (preset_id < EQ_PRESET_COUNT) {
        return eq_preset(preset_id)->name;
    }
    if (preset_id == EQ_PRESET_CUSTOM) {
        return "Custom";
    }
    int user_idx = settings_find_user_preset_by_id(settings, preset_id);
    if (user_idx >= 0) {
        return settings->eq_user_presets[user_idx].name;
    }
    return "Custom";
}

bool settings_save_user_preset(Settings *settings, const char *name, const int8_t *gains)
{
    if (settings == NULL || name == NULL || gains == NULL || name[0] == '\0') {
        return false;
    }
    int existing_idx = settings_find_user_preset_by_name(settings, name);
    int target_idx = existing_idx;
    if (target_idx < 0) {
        if (settings->eq_user_preset_count >= EQ_USER_PRESET_CAPACITY) {
            return false;
        }
        target_idx = settings->eq_user_preset_count;
        uint8_t next_id = 100;
        while (settings_find_user_preset_by_id(settings, next_id) >= 0) {
            next_id = (uint8_t)(next_id + 1);
        }
        settings->eq_user_presets[target_idx].id = next_id;
        settings->eq_user_preset_count++;
    }
    int char_idx = 0;
    while (char_idx < EQ_USER_PRESET_NAME_MAX && name[char_idx] != '\0') {
        settings->eq_user_presets[target_idx].name[char_idx] = name[char_idx];
        char_idx++;
    }
    settings->eq_user_presets[target_idx].name[char_idx] = '\0';
    for (int band = 0; band < EQ_BAND_COUNT; band++) {
        settings->eq_user_presets[target_idx].gains[band] = gains[band];
    }
    settings->eq_preset = settings->eq_user_presets[target_idx].id;
    return true;
}

bool settings_delete_user_preset(Settings *settings, uint8_t id)
{
    if (settings == NULL) {
        return false;
    }
    int idx = settings_find_user_preset_by_id(settings, id);
    if (idx < 0) {
        return false;
    }
    for (int i = idx; i + 1 < settings->eq_user_preset_count; i++) {
        settings->eq_user_presets[i] = settings->eq_user_presets[i + 1];
    }
    settings->eq_user_preset_count--;
    if (settings->eq_preset == id) {
        settings->eq_preset = EQ_PRESET_CUSTOM;
    }
    return true;
}

uint32_t settings_timeout_option_seconds(int option)
{
    return option >= 0 && option < SCREEN_TIMEOUT_OPTION_COUNT ? TIMEOUT_SECONDS[option] : 0;
}

uint32_t settings_sleep_option_minutes(int option)
{
    return option >= 0 && option < SLEEP_TIMER_OPTION_COUNT ? SLEEP_MINUTES[option] : 0;
}

uint32_t settings_screen_timeout_ms(const Settings *settings)
{
    return settings_timeout_option_seconds(settings->screen_timeout) * MILLISECONDS_PER_SECOND;
}

uint32_t settings_auto_off_option_minutes(int option)
{
    return option >= 0 && option < AUTO_OFF_OPTION_COUNT ? AUTO_OFF_MINUTES[option] : 0;
}

uint32_t settings_auto_off_ms(const Settings *settings)
{
    return settings_auto_off_option_minutes(settings->auto_off) * MILLISECONDS_PER_MINUTE;
}

static uint32_t checksum_of(const uint8_t *bytes, int checksum_offset)
{
    return hash_update(HASH_START, bytes, (size_t)checksum_offset);
}

static void put_u32(uint8_t *bytes, int offset, uint32_t value)
{
    for (int index = 0; index < 4; index++) {
        bytes[offset + index] = (uint8_t)(value >> (BITS_PER_BYTE * index));
    }
}

static uint32_t get_u32(const uint8_t *bytes, int offset)
{
    uint32_t value = 0;
    for (int index = 3; index >= 0; index--) {
        value = value << BITS_PER_BYTE | bytes[offset + index];
    }
    return value;
}

void settings_encode(const Settings *settings, uint8_t *bytes)
{
    for (int index = 0; index < SETTINGS_FILE_SIZE; index++) {
        bytes[index] = index < MAGIC_SIZE ? MAGIC[index] : 0;
    }
    bytes[VERSION_FIELD] = VERSION;
    bytes[BRIGHTNESS_FIELD] = settings->brightness;
    bytes[TIMEOUT_FIELD] = settings->screen_timeout;
    bytes[GAIN_FIELD] = (uint8_t)settings->gain;
    bytes[OUTPUT_FIELD] = (uint8_t)settings->output;
    bytes[START_FIELD] = (uint8_t)settings->start_screen;
    bytes[SHUFFLE_FIELD] = (uint8_t)settings->shuffle;
    bytes[REPEAT_FIELD] = (uint8_t)settings->repeat;
    bytes[VOLUME_FIELD] = settings->volume;
    bytes[AUTO_OFF_FIELD] = settings->auto_off;
    bytes[USB_MODE_FIELD] = (uint8_t)settings->usb_mode;
    bytes[DAC_VOLUME_FIELD] = settings->dac_volume;
    bytes[EQ_ENABLED_FIELD] = settings->eq_enabled ? 1 : 0;
    bytes[EQ_PRESET_FIELD] = settings->eq_preset;
    for (int band = 0; band < EQ_BAND_COUNT; band++) {
        bytes[EQ_GAINS_FIELD + band] = (uint8_t)settings->eq_gains[band];
    }
    bytes[EQ_USER_COUNT_FIELD] = settings->eq_user_preset_count;
    bytes[REPLAY_GAIN_FIELD] = (uint8_t)settings->replay_gain_mode;
    for (int index = 0; index < settings->eq_user_preset_count; index++) {
        int offset = USER_PRESETS_BASE + index * USER_PRESET_RECORD_SIZE;
        bytes[offset] = settings->eq_user_presets[index].id;
        for (int char_idx = 0; char_idx <= EQ_USER_PRESET_NAME_MAX; char_idx++) {
            bytes[offset + 1 + char_idx] = (uint8_t)settings->eq_user_presets[index].name[char_idx];
        }
        for (int band = 0; band < EQ_BAND_COUNT; band++) {
            bytes[offset + 17 + band] = (uint8_t)settings->eq_user_presets[index].gains[band];
        }
    }
    put_u32(bytes, CHECKSUM_FIELD, checksum_of(bytes, CHECKSUM_FIELD));
}

static bool header_valid(const uint8_t *bytes, uint32_t length)
{
    if (length < SETTINGS_LEGACY_FILE_SIZE) return false;
    for (int index = 0; index < MAGIC_SIZE; index++) {
        if (bytes[index] != MAGIC[index]) {
            return false;
        }
    }
    uint8_t version = bytes[VERSION_FIELD];
    if (bytes[VERSION_FIELD + 1] != 0) {
        return false;
    }
    if (version < FIRST_VERSION || version > VERSION) {
        return false;
    }
    if (version <= 4) {
        return length >= SETTINGS_LEGACY_FILE_SIZE &&
               get_u32(bytes, LEGACY_CHECKSUM_FIELD) == checksum_of(bytes, LEGACY_CHECKSUM_FIELD);
    }
    return length >= SETTINGS_FILE_SIZE &&
           get_u32(bytes, CHECKSUM_FIELD) == checksum_of(bytes, CHECKSUM_FIELD);
}

static bool values_valid(const uint8_t *bytes)
{
    if (bytes[BRIGHTNESS_FIELD] < SETTINGS_BRIGHTNESS_MIN ||
        bytes[BRIGHTNESS_FIELD] > SETTINGS_BRIGHTNESS_MAX ||
        bytes[TIMEOUT_FIELD] >= SCREEN_TIMEOUT_OPTION_COUNT ||
        bytes[GAIN_FIELD] >= GAIN_LEVEL_COUNT || bytes[OUTPUT_FIELD] >= OUTPUT_JACK_COUNT ||
        bytes[START_FIELD] >= START_SCREEN_COUNT || bytes[SHUFFLE_FIELD] >= SHUFFLE_MODE_COUNT ||
        bytes[REPEAT_FIELD] >= REPEAT_MODE_COUNT || bytes[VOLUME_FIELD] > SETTINGS_VOLUME_MAX ||
        bytes[AUTO_OFF_FIELD] >= AUTO_OFF_OPTION_COUNT) {
        return false;
    }
    uint8_t version = bytes[VERSION_FIELD];
    if (version >= 4) {
        if (bytes[USB_MODE_FIELD] >= USB_MODE_COUNT ||
            bytes[DAC_VOLUME_FIELD] > SETTINGS_VOLUME_MAX) {
            return false;
        }
    }
    if (version >= 6) {
        if (bytes[EQ_USER_COUNT_FIELD] > EQ_USER_PRESET_CAPACITY ||
            bytes[REPLAY_GAIN_FIELD] >= REPLAY_GAIN_MODE_COUNT) return false;
        for (int index = 0; index < bytes[EQ_USER_COUNT_FIELD]; index++) {
            int offset = USER_PRESETS_BASE + index * USER_PRESET_RECORD_SIZE;
            if (bytes[offset + 1 + EQ_USER_PRESET_NAME_MAX] != 0) return false;
            for (int band = 0; band < EQ_BAND_COUNT; band++) {
                int8_t gain = (int8_t)bytes[offset + 17 + band];
                if (gain < EQ_GAIN_MIN || gain > EQ_GAIN_MAX) return false;
            }
        }
    }
    if (version >= 5) {
        for (int band = 0; band < EQ_BAND_COUNT; band++) {
            int8_t gain = (int8_t)bytes[EQ_GAINS_FIELD + band];
            if (gain < EQ_GAIN_MIN || gain > EQ_GAIN_MAX) {
                return false;
            }
        }
    }
    return true;
}

bool settings_decode(const uint8_t *bytes, uint32_t length, Settings *settings)
{
    if (!header_valid(bytes, length) || !values_valid(bytes)) {
        return false;
    }
    uint8_t version = bytes[VERSION_FIELD];
    *settings = (Settings){
        .brightness = bytes[BRIGHTNESS_FIELD],
        .screen_timeout = bytes[TIMEOUT_FIELD],
        .gain = (GainLevel)bytes[GAIN_FIELD],
        .output = (OutputJack)bytes[OUTPUT_FIELD],
        .start_screen = (StartScreen)bytes[START_FIELD],
        .shuffle = (ShuffleMode)bytes[SHUFFLE_FIELD],
        .repeat = (RepeatMode)bytes[REPEAT_FIELD],
        .volume = bytes[VOLUME_FIELD],
        .auto_off = version < 2 ? (uint8_t)DEFAULT_AUTO_OFF_OPTION : bytes[AUTO_OFF_FIELD],
        .usb_mode = version < 3 ? USB_MODE_STORAGE : (UsbMode)bytes[USB_MODE_FIELD],
        .dac_volume = version < 4 ? (uint8_t)SETTINGS_DEFAULT_DAC_VOLUME : bytes[DAC_VOLUME_FIELD],
        .eq_enabled = version >= 5 ? (bytes[EQ_ENABLED_FIELD] != 0) : false,
        .eq_preset = version >= 5 ? bytes[EQ_PRESET_FIELD] : 0,
        .eq_user_preset_count = version >= 6 ? bytes[EQ_USER_COUNT_FIELD] : 0,
        .replay_gain_mode = version >= 6 ? (ReplayGainMode)bytes[REPLAY_GAIN_FIELD] : REPLAY_GAIN_OFF,
    };
    if (version >= 5) {
        for (int band = 0; band < EQ_BAND_COUNT; band++) {
            settings->eq_gains[band] = (int8_t)bytes[EQ_GAINS_FIELD + band];
        }
    }
    if (version >= 6) {
        uint8_t count = settings->eq_user_preset_count;
        if (count > EQ_USER_PRESET_CAPACITY) count = EQ_USER_PRESET_CAPACITY;
        for (int index = 0; index < count; index++) {
            int offset = USER_PRESETS_BASE + index * USER_PRESET_RECORD_SIZE;
            settings->eq_user_presets[index].id = bytes[offset];
            for (int char_idx = 0; char_idx <= EQ_USER_PRESET_NAME_MAX; char_idx++) {
                settings->eq_user_presets[index].name[char_idx] = (char)bytes[offset + 1 + char_idx];
            }
            for (int band = 0; band < EQ_BAND_COUNT; band++) {
                settings->eq_user_presets[index].gains[band] = (int8_t)bytes[offset + 17 + band];
            }
        }
    }
    return true;
}
