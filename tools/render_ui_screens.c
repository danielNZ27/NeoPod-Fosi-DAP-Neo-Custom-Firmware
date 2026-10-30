/* Render production UI components with sample metadata and supplied RGB565 cover art. */
#include <stdio.h>
#include <string.h>

#include "app_screens.h"
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

    app_theme_apply(2);
    WheelLock lock;
    wheel_lock_begin(&lock, 0);
    for (uint32_t i = 0; i < 25; i++)
        wheel_lock_key(&lock, (KeyEvent){KEY_WHEEL_DOWN, KEY_GESTURE_PRESS}, i);
    wheel_lock_draw(&lock, &canvas, &APP_THEME);
    if (!save(argv[1], "lock-teal")) return 1;
    puts("Rendered Home / Purple, Now Playing / Orange and Lock / Teal.");
    return 0;
}
