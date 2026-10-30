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

static void centered(Canvas *canvas, TextStyle style, int y, int width, const char *text)
{
    Rect area = {0, y, width, 20};
    label_draw(canvas, style, (Label){
        .area = label_centered_area(style.font, area, text), .text = text});
}

void wheel_lock_draw(const WheelLock *lock, Canvas *canvas, const Theme *theme)
{
    int width = theme->metrics.screen_width;
    int height = theme->metrics.screen_height;
    int cx = width / 2;
    int top = height / 2 - 80;
    Color accent = lock->steps > 0 ? theme->colors.selection_background
                                   : theme->colors.text_secondary;
    canvas_fill_rect(canvas, (Rect){0, 0, width, height}, theme->colors.background);
    header_draw(canvas, theme, "Locked");
    canvas_fill_rect(canvas, (Rect){cx - 17, top, 34, 7}, accent);
    canvas_fill_rect(canvas, (Rect){cx - 20, top + 7, 7, 25}, accent);
    canvas_fill_rect(canvas, (Rect){cx + 13, top + 7, 7, 25}, accent);
    canvas_fill_rect(canvas, (Rect){cx - 27, top + 27, 54, 42}, accent);
    canvas_fill_rect(canvas, (Rect){cx - 3, top + 40, 6, 15}, theme->colors.background);
    TextStyle body = {.font = theme->fonts.body, .color = theme->colors.text};
    centered(canvas, body, height / 2 + 14, width, "Scroll to unlock");
    Rect bar = {30, height - 80, width - 60, 10};
    canvas_fill_rect(canvas, bar, theme->colors.divider);
    bar.width = bar.width * lock->steps / WHEEL_UNLOCK_STEPS;
    canvas_fill_rect(canvas, bar, theme->colors.selection_background);
}
