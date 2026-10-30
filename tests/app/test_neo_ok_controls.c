#include "app/app.h"
#include "app_internal.h"
#include "card_files.h"
#include "check.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "music_check.h"
#include "lock_screen.h"
#include "default_theme.h"
#include "settings_store.h"
#include <string.h>
#include <stdio.h>

static uint32_t now;
static FixtureLibrary fixture;

static void advance(uint32_t milliseconds)
{
    now += milliseconds;
    fake_hal_set_milliseconds(now);
    app_step();
}

static void tap(Key key)
{
    fake_hal_queue_transition(key, true, now);
    fake_hal_queue_transition(key, false, now + 30);
    advance(40);
}

static void setup(void)
{
    fake_hal_reset();
    now = 1000;
    fake_hal_set_milliseconds(now);
    uint32_t size;
    const uint8_t *bytes = fixture_bytes(&size);
    fake_hal_add_file("/.echo/library.db", bytes, size);
    (void)card_files_prepare();
    music_check_note_firmware();
    Player saved;
    player_init(&saved, fixture_library_open(&fixture));
    CHECK(player_play_list(&saved, (BrowseQuery){.kind = BROWSE_ALBUM_TRACKS,
        .group = LIBRARY_ARTISTS, .subject = 0, .artist = LIBRARY_NONE}, 0, 7));
    CHECK(player_save(&saved));
    app_start();
    CHECK(app_player_status().has_track);
    CHECK_EQUAL(0, app_player_status().track);
}

static void dark_double_ok_skips_once_without_a_pause_or_wake(void)
{
    setup();
    tap(KEY_POWER);
    CHECK(!fake_hal_display_on());
    tap(KEY_PLAY);
    advance(350);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    int depth = app_screen_depth();
    tap(KEY_PLAY);
    advance(80);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    CHECK_EQUAL(0, app_player_status().track);
    tap(KEY_PLAY);
    CHECK_EQUAL(1, app_player_status().track);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    CHECK(!fake_hal_display_on());
    CHECK_EQUAL(depth, app_screen_depth());
    advance(350);
    CHECK_EQUAL(1, app_player_status().track);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
}

static void dark_single_ok_still_toggles_pause(void)
{
    setup();
    tap(KEY_POWER);
    tap(KEY_PLAY);
    advance(350);
    CHECK_EQUAL(PLAYER_PLAYING, app_player_status().state);
    tap(KEY_PLAY);
    advance(350);
    CHECK_EQUAL(PLAYER_PAUSED, app_player_status().state);
    CHECK_EQUAL(0, app_player_status().track);
    CHECK(!fake_hal_display_on());
}

static void lit_double_ok_still_goes_back_without_skipping(void)
{
    setup();
    tap(KEY_PLAY);
    advance(350);
    CHECK(app_screen_depth() > 1);
    int depth = app_screen_depth();
    tap(KEY_PLAY);
    advance(80);
    tap(KEY_PLAY);
    CHECK_EQUAL(depth - 1, app_screen_depth());
    CHECK_EQUAL(0, app_player_status().track);
    CHECK(fake_hal_display_on());
}

static bool locked(void)
{
    return strcmp(app_screen_title(), "Locked") == 0;
}

static void wheel_steps(int count)
{
    for (int i = 0; i < count; i++) tap(KEY_WHEEL_DOWN);
}

static void wake_requires_forty_wheel_steps_and_blocks_other_controls(void)
{
    setup();
    tap(KEY_POWER);
    tap(KEY_POWER);
    CHECK(fake_hal_display_on());
    CHECK(locked());
    uint8_t volume = app_player_status().volume;
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    advance(350);
    tap(KEY_VOLUME_UP);
    CHECK_EQUAL(volume, app_player_status().volume);
    CHECK_EQUAL(PLAYER_PAUSED, app_player_status().state);
    CHECK_EQUAL(0, app_player_status().track);
    wheel_steps(39);
    CHECK(locked());
    tap(KEY_WHEEL_UP);
    CHECK(!locked());
    CHECK_EQUAL(PLAYER_PAUSED, app_player_status().state);
    CHECK_EQUAL(0, app_player_status().track);
    CHECK_EQUAL(volume, app_player_status().volume);
}

static void inactivity_resets_visible_progress_and_requires_a_new_full_turn(void)
{
    setup();
    tap(KEY_POWER);
    tap(KEY_POWER);
    wheel_steps(20);
    CHECK_EQUAL(DEFAULT_THEME.colors.selection_background, fake_hal_pixel(31, 240));
    advance(1500);
    CHECK(locked());
    CHECK_EQUAL(DEFAULT_THEME.colors.divider, fake_hal_pixel(31, 240));
    wheel_steps(20);
    CHECK(locked());
    wheel_steps(20);
    CHECK(!locked());
}

static void power_can_sleep_a_locked_screen_and_dark_media_controls_still_work(void)
{
    setup();
    tap(KEY_POWER);
    tap(KEY_POWER);
    wheel_steps(10);
    tap(KEY_POWER);
    CHECK(!fake_hal_display_on());
    tap(KEY_PLAY);
    tap(KEY_PLAY);
    CHECK_EQUAL(1, app_player_status().track);
    CHECK(!fake_hal_display_on());
    tap(KEY_POWER);
    CHECK(locked());
    wheel_steps(30);
    CHECK(locked());
    wheel_steps(10);
    CHECK(!locked());
}

static void idle_deadline_and_real_wheel_presses_control_the_progress(void)
{
    WheelLock lock;
    wheel_lock_begin(&lock, UINT32_MAX - 1000u);
    CHECK(!wheel_lock_key(&lock, (KeyEvent){KEY_WHEEL_UP, KEY_GESTURE_PRESS}, UINT32_MAX - 1000u));
    wheel_lock_update(&lock, 498);
    CHECK_EQUAL(1, lock.steps);
    wheel_lock_update(&lock, 499);
    CHECK_EQUAL(0, lock.steps);
    CHECK(lock.active);
    CHECK(!wheel_lock_key(&lock, (KeyEvent){KEY_WHEEL_DOWN, KEY_GESTURE_REPEAT}, 500));
    CHECK(!wheel_lock_key(&lock, (KeyEvent){KEY_VOLUME_UP, KEY_GESTURE_PRESS}, 500));
    CHECK_EQUAL(0, lock.steps);
}

static void now_playing_is_first_and_the_root_has_no_diagnostics(void)
{
    setup();
    tap(KEY_PLAY);
    advance(350);
    CHECK(strcmp("Now Playing", app_screen_title()) == 0);
    setup();
    wheel_steps(3);
    tap(KEY_PLAY);
    advance(350);
    CHECK(strcmp("Now Playing", app_screen_title()) == 0);
    setup();
    wheel_steps(2);
    tap(KEY_PLAY);
    advance(350);
    CHECK(strcmp("Settings", app_screen_title()) == 0);
}

static void side_button_preference_controls_tracks_in_light_and_dark_but_not_on_lock(void)
{
    setup();
    uint8_t volume = app_player_status().volume;
    SettingsStore store = {.settings = settings_defaults()};
    store.settings.side_buttons = SIDE_BUTTONS_TRACKS;
    CHECK(settings_store_save(&store));
    app_start();
    tap(KEY_VOLUME_UP);
    CHECK_EQUAL(1, app_player_status().track);
    CHECK_EQUAL(volume, app_player_status().volume);
    fake_hal_audio_set_position(0);
    tap(KEY_VOLUME_DOWN);
    CHECK_EQUAL(0, app_player_status().track);
    tap(KEY_POWER);
    tap(KEY_VOLUME_UP);
    CHECK_EQUAL(1, app_player_status().track);
    CHECK(!fake_hal_display_on());
    fake_hal_audio_set_position(0);
    tap(KEY_VOLUME_DOWN);
    CHECK_EQUAL(0, app_player_status().track);
    fake_hal_queue_transition(KEY_VOLUME_UP, true, now);
    advance(1100);
    CHECK_EQUAL(1, app_player_status().track);
    advance(2000);
    CHECK_EQUAL(1, app_player_status().track);
    fake_hal_queue_transition(KEY_VOLUME_UP, false, now);
    advance(40);
    CHECK_EQUAL(1, app_player_status().track);
    tap(KEY_POWER);
    CHECK(locked());
    fake_hal_audio_set_position(0);
    tap(KEY_VOLUME_DOWN);
    CHECK_EQUAL(1, app_player_status().track);
    CHECK_EQUAL(volume, app_player_status().volume);
}

static void default_side_buttons_still_adjust_volume(void)
{
    setup();
    uint8_t volume = app_player_status().volume;
    tap(KEY_VOLUME_UP);
    CHECK_EQUAL(volume + 1, app_player_status().volume);
    CHECK_EQUAL(0, app_player_status().track);
    tap(KEY_VOLUME_DOWN);
    CHECK_EQUAL(volume, app_player_status().volume);
    tap(KEY_POWER);
    tap(KEY_VOLUME_UP);
    CHECK_EQUAL(volume + 1, app_player_status().volume);
    CHECK_EQUAL(0, app_player_status().track);
}

static void hybrid_taps_skip_and_holds_change_volume_without_a_release_skip(void)
{
    setup();
    SettingsStore store = {.settings = settings_defaults()};
    store.settings.side_buttons = SIDE_BUTTONS_HYBRID;
    CHECK(settings_store_save(&store));
    app_start();
    uint8_t volume = app_player_status().volume;
    tap(KEY_VOLUME_UP);
    CHECK_EQUAL(1, app_player_status().track);
    CHECK_EQUAL(volume, app_player_status().volume);
    fake_hal_audio_set_position(0);
    tap(KEY_VOLUME_DOWN);
    CHECK_EQUAL(0, app_player_status().track);
    CHECK_EQUAL(volume, app_player_status().volume);

    fake_hal_queue_transition(KEY_VOLUME_UP, true, now);
    advance(900);
    CHECK_EQUAL(volume, app_player_status().volume);
    CHECK_EQUAL(0, app_player_status().track);
    advance(100);
    CHECK_EQUAL(volume + 1, app_player_status().volume);
    advance(150);
    CHECK_EQUAL(volume + 2, app_player_status().volume);
    fake_hal_queue_transition(KEY_VOLUME_UP, false, now);
    advance(40);
    CHECK_EQUAL(0, app_player_status().track);
    CHECK_EQUAL(volume + 2, app_player_status().volume);

    tap(KEY_POWER);
    fake_hal_queue_transition(KEY_VOLUME_DOWN, true, now);
    advance(1000);
    advance(150);
    fake_hal_queue_transition(KEY_VOLUME_DOWN, false, now);
    advance(40);
    CHECK_EQUAL(volume, app_player_status().volume);
    CHECK_EQUAL(0, app_player_status().track);
    CHECK(!fake_hal_display_on());
    tap(KEY_VOLUME_UP);
    CHECK_EQUAL(1, app_player_status().track);
    CHECK_EQUAL(volume, app_player_status().volume);
    CHECK(!fake_hal_display_on());

    tap(KEY_POWER);
    CHECK(locked());
    fake_hal_audio_set_position(0);
    tap(KEY_VOLUME_DOWN);
    fake_hal_queue_transition(KEY_VOLUME_DOWN, true, now);
    advance(1000);
    advance(150);
    fake_hal_queue_transition(KEY_VOLUME_DOWN, false, now);
    advance(40);
    CHECK_EQUAL(1, app_player_status().track);
    CHECK_EQUAL(volume, app_player_status().volume);
}

static void render_lock(const char *path, int steps)
{
    WheelLock lock;
    wheel_lock_begin(&lock, 0);
    for (int i = 0; i < steps; i++)
        wheel_lock_key(&lock, (KeyEvent){KEY_WHEEL_DOWN, KEY_GESTURE_PRESS}, (uint32_t)i);
    Theme theme = DEFAULT_THEME;
    theme.metrics.screen_width = 240;
    theme.metrics.screen_height = 320;
    static Color pixels[240 * 320];
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, 240, 320});
    wheel_lock_draw(&lock, &canvas, &theme);
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
}

static void restart_restores_the_saved_button_colour(void)
{
    setup();
    Color blue = APP_THEME.colors.selection_background;
    SettingsStore store = {.settings = settings_defaults()};
    store.settings.button_color = 3;
    CHECK(settings_store_save(&store));
    app_start();
    CHECK(APP_THEME.colors.selection_background != blue);
    app_theme_apply(0);
}

int main(int argc, char **argv)
{
    const TestCase cases[] = {
        TEST_CASE(hybrid_taps_skip_and_holds_change_volume_without_a_release_skip),
        TEST_CASE(side_button_preference_controls_tracks_in_light_and_dark_but_not_on_lock),
        TEST_CASE(default_side_buttons_still_adjust_volume),
        TEST_CASE(now_playing_is_first_and_the_root_has_no_diagnostics),
        TEST_CASE(restart_restores_the_saved_button_colour),
        TEST_CASE(wake_requires_forty_wheel_steps_and_blocks_other_controls),
        TEST_CASE(inactivity_resets_visible_progress_and_requires_a_new_full_turn),
        TEST_CASE(power_can_sleep_a_locked_screen_and_dark_media_controls_still_work),
        TEST_CASE(idle_deadline_and_real_wheel_presses_control_the_progress),
        TEST_CASE(dark_double_ok_skips_once_without_a_pause_or_wake),
        TEST_CASE(dark_single_ok_still_toggles_pause),
        TEST_CASE(lit_double_ok_still_goes_back_without_skipping),
    };
    int result = test_run_all(cases, TEST_CASE_COUNT(cases));
    if (argc == 3) {
        render_lock(argv[1], 0);
        render_lock(argv[2], 20);
    }
    return result;
}
