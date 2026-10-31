#include "default_theme.h"

#include "fonts/fonts.h"
#include "hal/display.h"
#include "settings/settings.h"

Theme APP_THEME;

void app_theme_select(uint8_t theme, uint8_t color)
{
    APP_THEME = DEFAULT_THEME;
    APP_THEME.ipod = theme == 1;
    app_theme_apply(color);
}

void app_theme_apply(uint8_t color)
{
    static const Color COLORS[BUTTON_COLOR_COUNT] = {
        COLOR_RGB(34, 96, 208), COLOR_RGB(22, 115, 62), COLOR_RGB(0, 124, 131),
        COLOR_RGB(115, 54, 201), COLOR_RGB(173, 40, 113), COLOR_RGB(181, 44, 54),
        COLOR_RGB(175, 79, 17), COLOR_RGB(138, 105, 11), COLOR_RGB(90, 102, 115)
    };
    bool ipod = APP_THEME.ipod;
    APP_THEME = DEFAULT_THEME;
    if (ipod) {
        APP_THEME.ipod = true;
        APP_THEME.colors = (ThemeColors){
            .background = COLOR_RGB(247, 248, 250),
            .text = COLOR_RGB(16, 22, 28),
            .text_secondary = COLOR_RGB(68, 76, 86),
            .text_tertiary = COLOR_RGB(94, 102, 112),
            .selection_background = COLOR_RGB(0, 112, 207),
            .selection_top = COLOR_RGB(63, 189, 249),
            .selection_shine = COLOR_RGB(141, 220, 255),
            .selection_text = COLOR_RGB(255, 255, 255),
            .header_background = COLOR_RGB(159, 168, 179),
            .header_top = COLOR_RGB(246, 248, 251),
            .header_text = COLOR_RGB(29, 37, 47),
            .divider = COLOR_RGB(173, 181, 191),
            .scrollbar_thumb = COLOR_RGB(114, 131, 151),
            .danger = COLOR_RGB(194, 37, 33),
            .success = COLOR_RGB(39, 149, 72),
            .chevron = COLOR_RGB(95, 109, 128),
        };
        APP_THEME.fonts.body = &FONT_INTER_SEMIBOLD_16;
        return;
    }
    if (color == 0 || color >= BUTTON_COLOR_COUNT) return;
    Color base = COLORS[color];
    APP_THEME.colors.selection_background = base;
    APP_THEME.colors.selection_top = color_blend(base, COLOR_RGB(255, 255, 255), 3);
    APP_THEME.colors.selection_shine = color_blend(base, COLOR_RGB(255, 255, 255), 6);
}

const Theme DEFAULT_THEME = {
    .colors =
        {
            .background = COLOR_RGB(16, 17, 20),
            .text = COLOR_RGB(242, 242, 245),
            .text_secondary = COLOR_RGB(142, 142, 150),
            .text_tertiary = COLOR_RGB(98, 98, 106),
            .selection_background = COLOR_RGB(34, 96, 208),
            .selection_top = COLOR_RGB(82, 145, 240),
            .selection_shine = COLOR_RGB(120, 170, 248),
            .selection_text = COLOR_RGB(255, 255, 255),
            .header_background = COLOR_RGB(22, 23, 27),
            .header_top = COLOR_RGB(46, 47, 54),
            .header_text = COLOR_RGB(242, 242, 245),
            .divider = COLOR_RGB(48, 49, 56),
            .scrollbar_thumb = COLOR_RGB(96, 97, 106),
            .danger = COLOR_RGB(235, 77, 61),
            .success = COLOR_RGB(52, 199, 89),
            .chevron = COLOR_RGB(110, 110, 118),
        },
    .fonts =
        {
            .title = &FONT_INTER_SEMIBOLD_16,
            .body = &FONT_INTER_REGULAR_16,
            .small = &FONT_INTER_REGULAR_12,
        },
    .metrics =
        {
            .screen_width = HAL_DISPLAY_WIDTH,
            .screen_height = HAL_DISPLAY_HEIGHT,
            .header_height = 24,
            .header_inset = 68,
            .row_height = 24,
            .padding = 10,
            .scrollbar_width = 3,
            .scrollbar_inset = 2,
            .detail_gap = 8,
            .status_icon_size = 10,
        },
    .marquee = {.pause_ms = 1200, .speed_px_per_s = 40, .gap_px = 32},
};
