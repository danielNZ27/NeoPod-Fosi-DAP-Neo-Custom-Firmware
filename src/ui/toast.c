#include "ui/toast.h"

#include "core/deadline.h"
#include "ui/label.h"

enum {
    TOAST_HEIGHT = 24,
    TOAST_BOTTOM_MARGIN = 10,
    BORDER = 1
};

void toast_show(Toast *toast, const char *text, uint32_t now_ms)
{
    *toast = (Toast){.text = text, .until_ms = now_ms + 4000u, .visible = true};
}

void toast_update(Toast *toast, uint32_t now_ms)
{
    if (toast->visible && deadline_reached(now_ms, toast->until_ms)) {
        toast->visible = false;
    }
}

Rect toast_area(const Toast *toast, const Theme *theme)
{
    const ThemeMetrics *metrics = &theme->metrics;
    int width = text_width(theme->fonts.body, toast->text) + 2 * metrics->padding;
    int max_width = metrics->screen_width - 2 * metrics->padding;
    if (width > max_width) {
        width = max_width;
    }
    int height = label_wrapped_height(theme->fonts.body, toast->text,
                                      width - 2 * metrics->padding) + 2 * metrics->padding;
    if (height < TOAST_HEIGHT) {
        height = TOAST_HEIGHT;
    }
    if (height > metrics->screen_height) {
        height = metrics->screen_height;
    }
    int bottom_margin =
        metrics->screen_height > height + TOAST_BOTTOM_MARGIN ? TOAST_BOTTOM_MARGIN : 0;
    return (Rect){(metrics->screen_width - width) / 2,
                  metrics->screen_height - height - bottom_margin, width, height};
}

void toast_draw(const Toast *toast, Canvas *canvas, const Theme *theme)
{
    if (!toast->visible) {
        return;
    }
    Rect area = toast_area(toast, theme);
    Rect inner = {area.x + BORDER, area.y + BORDER, area.width - 2 * BORDER,
                  area.height - 2 * BORDER};
    TextStyle style = {.font = theme->fonts.body, .color = theme->colors.header_text};
    canvas_fill_rect(canvas, area, theme->colors.divider);
    canvas_fill_rect(canvas, inner, theme->colors.header_background);
    int padding = theme->metrics.padding;
    Rect text_area = {inner.x + padding - BORDER, inner.y,
                      area.width - 2 * padding, inner.height};
    label_draw_wrapped(canvas, style, text_area, toast->text);
}
