/* Render production UI components with sample metadata and supplied RGB565 cover art. */
#include <stdio.h>
#include <string.h>

#include "app_screens.h"
#include "bluetooth_manager.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "fixture_library.h"
#include "lock_screen.h"
#include "ui/status_bar.h"

static Color pixels[240 * 320];
static FixtureLibrary fixture;
static LibraryStore library;
static Player player;
static CoverCache covers;
static AppScreens screens;
static SettingsStore settings;
static FavoritesStore favorites;
static SpokenStore spoken;
static PowerPolicy power;
static Toast toast;
static AppOptions options;
static bool rescan;

static bool save(const char *directory, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.ppm", directory, name);
    FILE *out = fopen(path, "wb");
    if (!out) return false;
    fprintf(out, "P6\n240 320\n255\n");
    for (unsigned i = 0; i < 240 * 320; i++) {
        Color c = pixels[i];
        unsigned char rgb[3] = {(unsigned char)((c >> 11) * 255 / 31),
            (unsigned char)(((c >> 5) & 63) * 255 / 63),
            (unsigned char)((c & 31) * 255 / 31)};
        if (fwrite(rgb, 1, 3, out) != 3) { fclose(out); return false; }
    }
    return fclose(out) == 0;
}

static void status(Canvas *canvas)
{
    status_bar_draw(canvas, &APP_THEME, (StatusBarState){
        .playback = STATUS_PLAYBACK_PLAYING, .battery_known = true, .battery_percent = 83});
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "Usage: neo_render_ui OUTPUT_DIRECTORY COVER.rgb565\n"); return 2; }
    fake_hal_reset();
    library.library = *fixture_library_open(&fixture);
    library.available = true;
    favorites_store_load(&favorites, &library.library);
    player_init(&player, &library.library);
    if (!player_play_list(&player, (BrowseQuery){.kind = BROWSE_ALBUM_TRACKS,
        .group = LIBRARY_ARTISTS, .subject = 0, .artist = LIBRARY_NONE}, 0, 7)) return 1;
    FILE *art = fopen(argv[2], "rb");
    if (!art) return 1;
    size_t count = fread(covers.full, sizeof(Color), COVER_FULL_PIXELS, art);
    fclose(art);
    if (count != COVER_FULL_PIXELS) return 1;
    covers.library = &library.library;
    covers.full_album = 0;
    covers.full_state = COVER_LOADED;
    settings.settings = settings_defaults();
    app_theme_apply(3);
    AppScreensContext context = {.theme = &APP_THEME, .store = &library, .player = &player,
        .settings = &settings, .favorites = &favorites, .spoken = &spoken, .power = &power,
        .toast = &toast, .options = &options, .covers = &covers, .rescan_requested = &rescan};
    Screen *home = app_screens_init(&screens, context);
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, 240, 320});
    home->enter(home->context, 1000);
    home->update(home->context, 1000);
    canvas_clear(&canvas, APP_THEME.colors.background);
    home->draw(home->context, &canvas);
    status(&canvas);
    if (!save(argv[1], "home-purple")) return 1;
#ifdef ECHO_JACK_DIAGNOSTICS
    Screen *diagnostics = &screens.diagnostics.screen;
    diagnostics->enter(diagnostics->context, 1000);
    diagnostics->update(diagnostics->context, 1000);
    canvas_clear(&canvas, APP_THEME.colors.background);
    diagnostics->draw(diagnostics->context, &canvas);
    status(&canvas);
    if (!save(argv[1], "jack-diagnostics")) return 1;
#endif

    app_theme_apply(6);
    player_set_shuffle(&player, SHUFFLE_SONGS, 7);
    NowPlayingScreen *playing = &screens.now_playing;
    playing->screen.enter(playing, 1000);
    snprintf(playing->title, sizeof playing->title, "I'm the One");
    snprintf(playing->artist, sizeof playing->artist, "DJ Khaled");
    snprintf(playing->album, sizeof playing->album, "Grateful");
    playing->status.duration_ms = 288493;
    playing->status.position_ms = 95100;
    playing->status.format = (HalAudioFormat){.codec = HAL_AUDIO_CODEC_FLAC,
        .sample_rate = 44100, .bits_per_sample = 24, .channels = 2, .bitrate_kbps = 2062};
    playing->controls_active = true;
    playing->controls_active_until_ms = 5000;
    playing->selected_button = 4;
    playing->screen.draw(playing, &canvas);
    status(&canvas);
    if (!save(argv[1], "now-playing-orange")) return 1;

    app_theme_apply(0);
    CoverFlowScreen *flow = &screens.cover_flow;
    flow->screen.enter(flow, 1000);
    flow->screen.update(flow, 1000);
    canvas_clear(&canvas, APP_THEME.colors.background);
    flow->screen.draw(flow, &canvas);
    if (!save(argv[1], "cover-flow-blue")) return 1;

    app_theme_apply(1);
    Screen *settings_screen = &screens.settings.menu.screen;
    settings_screen->enter(settings_screen->context, 1000);
    settings_screen->update(settings_screen->context, 1000);
    canvas_clear(&canvas, APP_THEME.colors.background);
    settings_screen->draw(settings_screen->context, &canvas);
    status(&canvas);
    if (!save(argv[1], "settings-green")) return 1;

    app_theme_apply(4);
    custom_eq_screen_init(&screens.settings.equalizer.custom_eq, &settings, &APP_THEME);
    Screen *eq_screen = &screens.settings.equalizer.custom_eq.screen;
    eq_screen->enter(eq_screen->context, 1000);
    eq_screen->update(eq_screen->context, 1000);
    canvas_clear(&canvas, APP_THEME.colors.background);
    eq_screen->draw(eq_screen->context, &canvas);
    status(&canvas);
    if (!save(argv[1], "custom-eq-magenta")) return 1;

    app_theme_apply(2);
    WheelLock lock;
    wheel_lock_begin(&lock, 0);
    for (uint32_t i = 0; i < 25; i++)
        wheel_lock_key(&lock, (KeyEvent){KEY_WHEEL_DOWN, KEY_GESTURE_PRESS}, i);
    wheel_lock_draw(&lock, &canvas, &APP_THEME);
    if (!save(argv[1], "lock-teal")) return 1;
    app_theme_select(1, 3);
    settings.settings.theme = 1;
    home->update(home->context, 1000);
    canvas_clear(&canvas, APP_THEME.colors.background);
    home->draw(home->context, &canvas);
    status(&canvas);
    if (!save(argv[1], "ipod-home")) return 1;
    canvas_clear(&canvas, APP_THEME.colors.background);
    screens.settings.display_menu.screen.draw(&screens.settings.display_menu, &canvas);
    status(&canvas);
    if (!save(argv[1], "ipod-settings")) return 1;
    playing->screen.draw(playing, &canvas);
    status(&canvas);
    if (!save(argv[1], "ipod-now-playing-controls")) return 1;
    playing->selected_button = 5;
    playing->scrubbing = true;
    playing->screen.draw(playing, &canvas);
    status(&canvas);
    if (!save(argv[1], "ipod-now-playing-seek")) return 1;
    playing->scrubbing = false;
    playing->controls_active = false;
    playing->screen.draw(playing, &canvas);
    status(&canvas);
    if (!save(argv[1], "ipod-now-playing")) return 1;
    app_theme_select(0, 0);
    bluetooth_manager_init(1000);
    BluetoothScreen *bt = &screens.settings.bluetooth;
    bt->screen.enter(bt, 1000);
    canvas_clear(&canvas, APP_THEME.colors.background);
    bt->screen.draw(bt, &canvas);
    status(&canvas);
    if (!save(argv[1], "bluetooth-off")) return 1;
    bt->screen.handle_key(bt, (KeyEvent){KEY_WHEEL_DOWN, KEY_GESTURE_PRESS}, 1100);
    bt->screen.handle_key(bt, (KeyEvent){KEY_PLAY, KEY_GESTURE_PRESS}, 1200);
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_READY;
    bt->screen.update(bt, 1500);
    fake_hal_bluetooth()->devices[0] = (HalBluetoothDevice){{1,2,3,4,5,6}, "WH-1000XM5"};
    fake_hal_bluetooth()->devices[1] = (HalBluetoothDevice){{7,8,9,10,11,12}, "Living Room Speaker"};
    fake_hal_bluetooth()->device_count = 2;
    list_view_select(&bt->list, 2, 1600);
    canvas_clear(&canvas, APP_THEME.colors.background);
    bt->screen.draw(bt, &canvas);
    status(&canvas);
    if (!save(argv[1], "bluetooth-pair")) return 1;
    bt->screen.handle_key(bt, (KeyEvent){KEY_PLAY, KEY_GESTURE_PRESS}, 1700);
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_CONNECTED;
    bluetooth_manager_update(1800);
    bt->screen.handle_key(bt, (KeyEvent){KEY_BACK, KEY_GESTURE_PRESS}, 1900);
    list_view_select(&bt->list, 2, 2000);
    bt->screen.handle_key(bt, (KeyEvent){KEY_PLAY, KEY_GESTURE_PRESS}, 2100);
    canvas_clear(&canvas, APP_THEME.colors.background);
    bt->screen.draw(bt, &canvas);
    status(&canvas);
    if (!save(argv[1], "bluetooth-saved")) return 1;
    bt->screen.handle_key(bt, (KeyEvent){KEY_PLAY, KEY_GESTURE_PRESS}, 2200);
    canvas_clear(&canvas, APP_THEME.colors.background);
    bt->screen.draw(bt, &canvas);
    status(&canvas);
    if (!save(argv[1], "bluetooth-device")) return 1;
    StatsScreen *stats = &screens.settings.stats;
    for (int theme = 0; theme < 2; theme++) {
        app_theme_select((uint8_t)theme, 0);
        stats->screen.enter(stats, 3000);
        stats->screen.update(stats, 3001);
        canvas_clear(&canvas, APP_THEME.colors.background);
        stats->screen.draw(stats, &canvas);
        status_bar_draw(&canvas, &APP_THEME, (StatusBarState){
            .playback = STATUS_PLAYBACK_PLAYING, .battery_known = true,
            .battery_percent = 83, .bluetooth_on = true, .bluetooth_connected = true});
        if (!save(argv[1], theme ? "stats-ipod" : "stats-neopod")) return 1;
    }
    puts("Rendered all UI screens in multiple color themes successfully.");
    return 0;
}
