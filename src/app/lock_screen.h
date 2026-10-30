#pragma once

#include "core/key_tracker.h"
#include "ui/theme.h"
#include "gfx/canvas.h"

enum { WHEEL_UNLOCK_STEPS = 40, WHEEL_UNLOCK_IDLE_MS = 1500 };

typedef struct WheelLock {
    bool active;
    uint8_t steps;
    uint32_t last_scroll_ms;
} WheelLock;

void wheel_lock_begin(WheelLock *lock, uint32_t now_ms);
void wheel_lock_update(WheelLock *lock, uint32_t now_ms);
bool wheel_lock_key(WheelLock *lock, KeyEvent event, uint32_t now_ms);
void wheel_lock_draw(const WheelLock *lock, Canvas *canvas, const Theme *theme);
