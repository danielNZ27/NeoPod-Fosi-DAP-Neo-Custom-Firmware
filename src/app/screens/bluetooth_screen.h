#pragma once
#include "ui/list_view.h"
#include "ui/screen.h"
typedef enum BluetoothPage {
    BLUETOOTH_MAIN, BLUETOOTH_PAIR, BLUETOOTH_SAVED,
    BLUETOOTH_DEVICE, BLUETOOTH_FORGET
} BluetoothPage;
typedef struct BluetoothScreen {
    Screen screen;
    const Theme *theme;
    ListModel model;
    ListView list;
    BluetoothPage page;
    uint8_t device;
    bool scan_requested;
    uint32_t now_ms;
} BluetoothScreen;
Screen *bluetooth_screen_init(BluetoothScreen *bluetooth, const Theme *theme);
