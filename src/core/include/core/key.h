#pragma once

typedef enum Key {
    KEY_VOLUME_UP,
    KEY_VOLUME_DOWN,
    KEY_PLAY,
    KEY_MENU,
    KEY_BACK,
    KEY_POWER,
    KEY_WHEEL_UP,
    KEY_WHEEL_DOWN
} Key;

enum {
    KEY_COUNT = KEY_WHEEL_DOWN + 1
};
