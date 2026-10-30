#include "screens/custom_eq_screen.h"

#include <stddef.h>

#include "core/string_builder.h"
#include "hal/audio.h"
#include "ui/header.h"
#include "ui/label.h"

enum {
    TRACK_WIDTH = 2,
    FILL_WIDTH = 12,
    THUMB_WIDTH = 28,
    THUMB_HEIGHT = 6,
    LABEL_HEIGHT = 14
};

typedef struct EqLayout {
    int left, right, middle, range, labels_y;
} EqLayout;

static inline bool portrait_theme(const Theme *theme)
{
#if defined(ECHO_BOARD_NEO) || defined(ECHO_NEO_SIM)
    (void)theme;
    return true;
#else
    return theme->metrics.screen_height > theme->metrics.screen_width;
#endif
}

static EqLayout layout_of(const Theme *theme)
{
    bool portrait = portrait_theme(theme);
    int top = theme->metrics.header_height + (portrait ? 50 : 22);
    int labels = theme->metrics.screen_height - (portrait ? 84 : 36);
    int bottom = labels - (portrait ? 12 : 4);
    return (EqLayout){32, theme->metrics.screen_width - 10,
                      (top + bottom) / 2, (bottom - top) / 2, labels};
}

static int band_center(EqLayout layout, int band)
{
    return layout.left + (2 * band + 1) * (layout.right - layout.left) / (2 * EQ_BAND_COUNT);
}
static const char *const SHORT_NAMES[EQ_BAND_COUNT] = {"100", "300", "1k", "3k", "10k"};

static void apply_eq(const Settings *settings)
{
    hal_audio_set_eq(settings->eq_enabled, settings->eq_gains);
}

static void format_band_title(int band, int8_t gain, bool adjusting, char *buffer,
                              size_t capacity)
{
    StringBuilder b = string_builder_make(buffer, capacity);
    if (adjusting) {
        string_builder_append(&b, "< ");
    }
    string_builder_append(&b, eq_band_name(band));
    string_builder_append(&b, ": ");
    if (gain > 0) {
        string_builder_append_char(&b, '+');
        string_builder_append_unsigned(&b, (uint32_t)gain);
    } else if (gain < 0) {
        string_builder_append_char(&b, '-');
        string_builder_append_unsigned(&b, (uint32_t)(-gain));
    } else {
        string_builder_append_char(&b, '0');
    }
    string_builder_append(&b, " dB");
    if (adjusting) {
        string_builder_append(&b, " >");
    }
}

static ScreenAction handle_adjusting_key(CustomEqScreen *screen, KeyEvent event)
{
    Settings *settings = &screen->store->settings;
    int band = screen->selected_band;
    bool is_step = event.gesture == KEY_GESTURE_PRESS || event.gesture == KEY_GESTURE_REPEAT;
    if ((event.key == KEY_WHEEL_DOWN || event.key == KEY_VOLUME_UP) && is_step) {
        if (settings->eq_gains[band] < EQ_GAIN_MAX) {
            settings->eq_gains[band]++;
            settings->eq_preset = EQ_PRESET_CUSTOM;
            apply_eq(settings);
        }
        return screen_action_none();
    }
    if ((event.key == KEY_WHEEL_UP || event.key == KEY_VOLUME_DOWN) && is_step) {
        if (settings->eq_gains[band] > EQ_GAIN_MIN) {
            settings->eq_gains[band]--;
            settings->eq_preset = EQ_PRESET_CUSTOM;
            apply_eq(settings);
        }
        return screen_action_none();
    }
    if ((event.key == KEY_PLAY || event.key == KEY_BACK) && event.gesture == KEY_GESTURE_PRESS) {
        screen->adjusting = false;
        settings_store_save(screen->store);
        return screen_action_none();
    }
    return screen_action_none();
}

static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    CustomEqScreen *screen = context;
    screen->now_ms = now_ms;
    if (screen->adjusting) {
        return handle_adjusting_key(screen, event);
    }
    bool is_step = event.gesture == KEY_GESTURE_PRESS || event.gesture == KEY_GESTURE_REPEAT;
    if ((event.key == KEY_WHEEL_UP || event.key == KEY_VOLUME_DOWN) && is_step) {
        screen->selected_band = (screen->selected_band + EQ_BAND_COUNT - 1) % EQ_BAND_COUNT;
        return screen_action_none();
    }
    if ((event.key == KEY_WHEEL_DOWN || event.key == KEY_VOLUME_UP) && is_step) {
        screen->selected_band = (screen->selected_band + 1) % EQ_BAND_COUNT;
        return screen_action_none();
    }
    if (event.key == KEY_PLAY && event.gesture == KEY_GESTURE_PRESS) {
        screen->adjusting = true;
        return screen_action_none();
    }
    if (event.key == KEY_BACK && event.gesture == KEY_GESTURE_PRESS) {
        settings_store_save(screen->store);
        return screen_action_pop();
    }
    return screen_action_none();
}

static void enter(void *context, uint32_t now_ms)
{
    CustomEqScreen *screen = context;
    screen->now_ms = now_ms;
    screen->adjusting = false;
}

static void update(void *context, uint32_t now_ms)
{
    CustomEqScreen *screen = context;
    screen->now_ms = now_ms;
}

static void draw_grid(Canvas *canvas, const Theme *theme)
{
    EqLayout layout = layout_of(theme);
    int grid_w = layout.right - layout.left;
    canvas_fill_rect(canvas, (Rect){layout.left, layout.middle - layout.range, grid_w, 1},
                     theme->colors.divider);
    canvas_fill_rect(canvas, (Rect){layout.left, layout.middle - layout.range / 2, grid_w, 1},
                     theme->colors.divider);
    canvas_fill_rect(canvas, (Rect){layout.left - 4, layout.middle, grid_w + 8, 1},
                     theme->colors.text_secondary);
    canvas_fill_rect(canvas, (Rect){layout.left, layout.middle + layout.range / 2, grid_w, 1},
                     theme->colors.divider);
    canvas_fill_rect(canvas, (Rect){layout.left, layout.middle + layout.range, grid_w, 1},
                     theme->colors.divider);

    TextStyle scale_style = {.font = theme->fonts.small, .color = theme->colors.text_tertiary};
    label_draw(canvas, scale_style, (Label){.area = {8, layout.middle - layout.range - 6, 22, 12}, .text = "+12"});
    label_draw(canvas, scale_style, (Label){.area = {16, layout.middle - 6, 14, 12}, .text = "0"});
    label_draw(canvas, scale_style, (Label){.area = {8, layout.middle + layout.range - 6, 22, 12}, .text = "-12"});
}

static void draw_columns(const CustomEqScreen *screen, Canvas *canvas, const Theme *theme)
{
    const Settings *settings = &screen->store->settings;
    EqLayout layout = layout_of(theme);
    for (int band = 0; band < EQ_BAND_COUNT; band++) {
        int cx = band_center(layout, band);
        int8_t gain = settings->eq_gains[band];
        int thumb_y = layout.middle - (gain * layout.range / EQ_GAIN_MAX);
        bool selected = band == screen->selected_band;

        canvas_fill_rect(canvas, (Rect){cx - TRACK_WIDTH / 2, layout.middle - layout.range, TRACK_WIDTH, layout.range * 2},
                         theme->colors.divider);

        Color fill_color = selected ? theme->colors.selection_top : theme->colors.text_tertiary;
        if (gain > 0) {
            canvas_fill_rect(canvas, (Rect){cx - FILL_WIDTH / 2, thumb_y, FILL_WIDTH, layout.middle - thumb_y},
                             fill_color);
        } else if (gain < 0) {
            canvas_fill_rect(canvas, (Rect){cx - FILL_WIDTH / 2, layout.middle, FILL_WIDTH, thumb_y - layout.middle},
                             fill_color);
        }

        Color thumb_color = selected
                                ? (screen->adjusting ? theme->colors.selection_top
                                                     : theme->colors.selection_background)
                                : theme->colors.text_secondary;
        canvas_fill_rect(canvas,
                         (Rect){cx - THUMB_WIDTH / 2, thumb_y - THUMB_HEIGHT / 2, THUMB_WIDTH, THUMB_HEIGHT},
                         thumb_color);
        if (selected) {
            canvas_fill_rect(
                canvas,
                (Rect){cx - THUMB_WIDTH / 2 + 1, thumb_y - THUMB_HEIGHT / 2 + 1, THUMB_WIDTH - 2, 1},
                theme->colors.selection_shine);
        }

        TextStyle freq_style = {
            .font = theme->fonts.small,
            .color = selected ? theme->colors.selection_background : theme->colors.text_secondary};
        Rect freq_area = label_centered_area(freq_style.font, (Rect){cx - 18, layout.labels_y, 36, LABEL_HEIGHT},
                                            SHORT_NAMES[band]);
        label_draw(canvas, freq_style, (Label){.area = freq_area, .text = SHORT_NAMES[band]});
        if (portrait_theme(theme)) {
            char value[8];
            StringBuilder b = string_builder_make(value, sizeof value);
            if (gain != 0) string_builder_append_char(&b, gain > 0 ? '+' : '-');
            string_builder_append_unsigned(&b, (uint32_t)(gain < 0 ? -gain : gain));
            Rect value_box = {cx - 18, layout.labels_y + 18, 36, LABEL_HEIGHT};
            label_draw(canvas, freq_style, (Label){
                .area = label_centered_area(freq_style.font, value_box, value), .text = value});
        }
    }
}

static void draw(void *context, Canvas *canvas)
{
    CustomEqScreen *screen = context;
    const Theme *theme = screen->theme;
    Rect content = theme_content_area(theme);
    canvas_fill_rect(canvas, content, theme->colors.background);
    header_draw(canvas, theme, screen->screen.title);

    char title_buf[32];
    int8_t gain = screen->store->settings.eq_gains[screen->selected_band];
    format_band_title(screen->selected_band, gain, screen->adjusting, title_buf, sizeof title_buf);
    TextStyle title_style = {
        .font = theme->fonts.body,
        .color = screen->adjusting ? theme->colors.selection_background : theme->colors.text};
    Rect title_box = {0, theme->metrics.header_height + 4, theme->metrics.screen_width, 20};
    Rect title_area = label_centered_area(title_style.font, title_box, title_buf);
    label_draw(canvas, title_style, (Label){.area = title_area, .text = title_buf});

    draw_grid(canvas, theme);
    draw_columns(screen, canvas, theme);

    bool portrait = portrait_theme(theme);
    const char *hint = screen->adjusting ? "Wheel: Adjust gain" : "Wheel: Choose band";
    const char *action = screen->adjusting ? "OK: Confirm" : "OK: Adjust";
    TextStyle hint_style = {.font = theme->fonts.small, .color = theme->colors.text_secondary};
    int hint_y = theme->metrics.screen_height - (portrait ? 44 : 20);
    Rect hint_box = {0, hint_y, theme->metrics.screen_width, 14};
    Rect action_box = {0, hint_y + (portrait ? 18 : 0), theme->metrics.screen_width, 14};
    if (!portrait) {
        hint_box.width /= 2;
        action_box.x = hint_box.width;
        action_box.width = hint_box.width;
    }
    label_draw(canvas, hint_style, (Label){
        .area = label_centered_area(hint_style.font, hint_box, hint), .text = hint});
    label_draw(canvas, hint_style, (Label){
        .area = label_centered_area(hint_style.font, action_box, action), .text = action});
}

Screen *custom_eq_screen_init(CustomEqScreen *custom_screen, SettingsStore *store,
                              const Theme *theme)
{
    custom_screen->store = store;
    custom_screen->theme = theme;
    custom_screen->selected_band = 0;
    custom_screen->adjusting = false;
    custom_screen->now_ms = 0;
    custom_screen->screen = (Screen){
        .title = "Custom EQ",
        .context = custom_screen,
        .full_screen = false,
        .paints_every_pixel = false,
        .hold_volume_keys = false,
        .stays_on_wake = true,
        .enter = enter,
        .handle_key = handle_key,
        .update = update,
        .draw = draw,
    };
    return &custom_screen->screen;
}
