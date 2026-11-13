#include "lock_screen.h"

#include "ui/header.h"
#include "ui/label.h"

void wheel_lock_begin(WheelLock *lock, uint32_t now_ms)
{
    *lock = (WheelLock){.active = true, .last_scroll_ms = now_ms};
}

void wheel_lock_update(WheelLock *lock, uint32_t now_ms)
{
    if (lock->active && now_ms - lock->last_scroll_ms >= WHEEL_UNLOCK_IDLE_MS) {
        lock->steps = 0;
    }
}

bool wheel_lock_key(WheelLock *lock, KeyEvent event, uint32_t now_ms)
{
    wheel_lock_update(lock, now_ms);
    if (!lock->active || event.gesture != KEY_GESTURE_PRESS ||
        (event.key != KEY_WHEEL_UP && event.key != KEY_WHEEL_DOWN)) {
        return false;
    }
    lock->last_scroll_ms = now_ms;
    lock->steps++;
    if (lock->steps < WHEEL_UNLOCK_STEPS) return false;
    lock->active = false;
    return true;
}

/* Draw a line twice, clipping the accented layer to unlock progress. */
static void unlock_colored_text(const WheelLock *lock, Canvas *canvas,
                                const Theme *theme, const char *text,
                                int y, TextStyle style)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }
    int width = theme->metrics.screen_width;
    int pad = theme->metrics.padding;
    Rect region = {pad, y, width - 2 * pad, 24};
    Rect text_area = label_centered_area(style.font, region, text);
    TextStyle normal = style;
    normal.color = theme->colors.text;
    TextStyle accent = style;
    accent.color = theme->colors.selection_background;
    Label line = {.area = text_area, .text = text};
    label_draw(canvas, normal, line);

    int filled = (region.width * lock->steps) / WHEEL_UNLOCK_STEPS;
    if (filled <= 0) return;
    int x_start = region.x;
    int x_end = region.x + filled;
    Canvas clipped = *canvas;
    int old_end = clipped.clip.x + clipped.clip.width;
    if (x_start > clipped.clip.x) clipped.clip.x = x_start;
    if (x_end < old_end) old_end = x_end;
    clipped.clip.width = old_end - clipped.clip.x;
    if (clipped.clip.width > 0) label_draw(&clipped, accent, line);
}

void wheel_lock_draw(const WheelLock *lock, Canvas *canvas, const Theme *theme,
                     const char *title, const char *artist, const Image *cover)
{
    int width = theme->metrics.screen_width;
    int height = theme->metrics.screen_height;
    canvas_fill_rect(canvas, (Rect){0, 0, width, height}, theme->colors.background);
    header_draw(canvas, theme, "Locked");

    int size = 112;
    int left = (width - size) / 2;
    int top = 36;
    if (cover != NULL) {
        image_draw_quad(canvas, *cover, (Quad){left, size, top, size, top, size});
    } else {
        canvas_fill_rect(canvas, (Rect){left, top, size, size},
                         theme->colors.header_background);
    }

    unlock_colored_text(lock, canvas, theme, title, 168, (TextStyle){.font = theme->fonts.title});
    unlock_colored_text(lock, canvas, theme, artist, 197, (TextStyle){.font = theme->fonts.body});
    TextStyle hint = {.font = theme->fonts.small, .color = theme->colors.text_secondary};
    const char *message = "Scroll to unlock";
    Rect area = {0, height - 43, width, 20};
    label_draw(canvas, hint,
               (Label){.area = label_centered_area(hint.font, area, message),
                       .text = message});
}
