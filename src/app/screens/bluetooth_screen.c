#include "bluetooth_screen.h"
#include <stddef.h>
#include "bluetooth_manager.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/list_navigation.h"
#include "ui/navigation.h"

static int count(const void *context)
{
    const BluetoothScreen *s = context;
    switch (s->page) {
    case BLUETOOTH_MAIN: return 3;
    case BLUETOOTH_PAIR: return 2 + hal_bluetooth_status()->device_count;
    case BLUETOOTH_SAVED: return bluetooth_manager_count() ? bluetooth_manager_count() : 1;
    default: return 2;
    }
}
static const char *label(const void *context, int index)
{
    const BluetoothScreen *s = context;
    const HalBluetoothStatus *status = hal_bluetooth_status();
    switch (s->page) {
    case BLUETOOTH_MAIN: {
        static const char *const rows[] = {"Bluetooth", "Pair new device", "Saved devices"};
        return rows[index];
    }
    case BLUETOOTH_PAIR:
        if (index == 0) return status->state == HAL_BLUETOOTH_SCANNING ? "Searching..." : "Search again";
        if (index == 1) return "Headphones: pairing mode";
        return status->devices[index - 2].name;
    case BLUETOOTH_SAVED: {
        const HalBluetoothDevice *d = bluetooth_manager_device((uint8_t)index);
        return d ? d->name : "No saved devices";
    }
    case BLUETOOTH_DEVICE:
        if (index == 1) return "Forget device";
        return bluetooth_manager_is_connected(bluetooth_manager_device(s->device)) ? "Disconnect" : "Connect";
    case BLUETOOTH_FORGET: return index == 0 ? "Cancel" : "Forget device";
    }
    return "";
}
static const char *detail(const void *context, int index)
{
    const BluetoothScreen *s = context;
    if (s->page == BLUETOOTH_MAIN && index == 0) return bluetooth_manager_enabled() ? "On" : "Off";
    const HalBluetoothDevice *d = NULL;
    if (s->page == BLUETOOTH_SAVED) d = bluetooth_manager_device((uint8_t)index);
    if (s->page == BLUETOOTH_PAIR && index >= 2) d = &hal_bluetooth_status()->devices[index - 2];
    return bluetooth_manager_is_connected(d) ? "Connected" : NULL;
}
static bool opens(const void *context, int index)
{
    const BluetoothScreen *s = context;
    return (s->page == BLUETOOTH_MAIN && index > 0) ||
           (s->page == BLUETOOTH_SAVED && bluetooth_manager_count() > 0) ||
           (s->page == BLUETOOTH_DEVICE && index == 1);
}
static void show(BluetoothScreen *s, BluetoothPage page)
{
    s->page = page;
    bluetooth_manager_pairing(page == BLUETOOTH_PAIR);
    static const char *const titles[] = {"Bluetooth", "Pair device", "Devices", "Device", "Forget device?"};
    s->screen.title = titles[page];
    list_view_init(&s->list, &s->model, s->theme, s->now_ms);
    s->list.visible_rows = (theme_content_area(s->theme).height - 34) / s->theme->metrics.row_height;
}
static ScreenAction select_row(void *context, int index)
{
    BluetoothScreen *s = context;
    switch (s->page) {
    case BLUETOOTH_MAIN:
        if (index == 0) bluetooth_manager_set_enabled(!bluetooth_manager_enabled(), s->now_ms);
        if (index == 1) {
            if (!bluetooth_manager_enabled()) bluetooth_manager_set_enabled(true, s->now_ms);
            s->scan_requested = true;
            show(s, BLUETOOTH_PAIR);
        }
        if (index == 2) show(s, BLUETOOTH_SAVED);
        break;
    case BLUETOOTH_PAIR:
        if (index == 0) s->scan_requested = true;
        if (index >= 2) bluetooth_manager_pair((uint8_t)(index - 2), s->now_ms);
        break;
    case BLUETOOTH_SAVED:
        if (bluetooth_manager_count()) { s->device = (uint8_t)index; show(s, BLUETOOTH_DEVICE); }
        break;
    case BLUETOOTH_DEVICE:
        if (index == 1) show(s, BLUETOOTH_FORGET);
        else if (bluetooth_manager_is_connected(bluetooth_manager_device(s->device))) bluetooth_manager_disconnect();
        else bluetooth_manager_connect(s->device, s->now_ms);
        break;
    case BLUETOOTH_FORGET:
        if (index == 0) show(s, BLUETOOTH_DEVICE);
        else if (bluetooth_manager_forget(s->device)) show(s, BLUETOOTH_SAVED);
        break;
    }
    return screen_action_none();
}
static void enter(void *context, uint32_t now_ms)
{
    BluetoothScreen *s = context;
    s->now_ms = now_ms;
    show(s, BLUETOOTH_MAIN);
}
static ScreenAction handle_key(void *context, KeyEvent event, uint32_t now_ms)
{
    BluetoothScreen *s = context;
    s->now_ms = now_ms;
    NavigationIntent intent = navigation_from_key(event).intent;
    if (intent == NAVIGATION_HOME) { bluetooth_manager_pairing(false); return screen_action_pop_to_root(); }
    if (intent == NAVIGATION_BACK) {
        if (s->page == BLUETOOTH_MAIN) return screen_action_pop();
        show(s, s->page == BLUETOOTH_FORGET ? BLUETOOTH_DEVICE : s->page == BLUETOOTH_DEVICE ? BLUETOOTH_SAVED : BLUETOOTH_MAIN);
        return screen_action_none();
    }
    return list_navigation_handle(&s->list, event, now_ms, (ListSelection){.select = select_row, .context = s});
}
static void update(void *context, uint32_t now_ms)
{
    BluetoothScreen *s = context;
    s->now_ms = now_ms;
    if (s->page == BLUETOOTH_PAIR && s->scan_requested && hal_bluetooth_status()->state == HAL_BLUETOOTH_READY) {
        bluetooth_manager_scan(); s->scan_requested = false;
    }
    if (s->list.selected >= count(s)) list_view_select(&s->list, count(s) - 1, now_ms);
}
static void draw(void *context, Canvas *canvas)
{
    BluetoothScreen *s = context;
    Rect content = theme_content_area(s->theme);
    canvas_fill_rect(canvas, content, s->theme->colors.background);
    header_draw(canvas, s->theme, s->screen.title);
    content.height -= 34;
    list_view_draw(&s->list, canvas, content, s->now_ms);
    TextStyle style = {.font = s->theme->fonts.small, .color = s->theme->colors.text_secondary};
    const char *text = bluetooth_manager_status();
    if (s->page == BLUETOOTH_PAIR && bluetooth_manager_count() == BLUETOOTH_SAVED_CAPACITY)
        text = "Saved list full. Forget a device first.";
    label_draw(canvas, style, (Label){.area = {content.x, content.y + content.height + 2, content.width, 15}, .text = text});
    const HalBluetoothDevice *selected = (s->page == BLUETOOTH_DEVICE || s->page == BLUETOOTH_FORGET)
        ? bluetooth_manager_device(s->device) : NULL;
    const char *name = selected ? selected->name :
        hal_bluetooth_status()->state == HAL_BLUETOOTH_CONNECTED ? hal_bluetooth_status()->connected.name : NULL;
    if (name) label_draw(canvas, style, (Label){.area = {content.x, content.y + content.height + 17, content.width, 15}, .text = name});
}
Screen *bluetooth_screen_init(BluetoothScreen *s, const Theme *theme)
{
    *s = (BluetoothScreen){.theme = theme};
    s->model = (ListModel){.context = s, .count = count, .label = label, .detail = detail, .opens = opens, .detail_max_width = 90};
    s->screen = (Screen){.title = "Bluetooth", .stays_on_wake = true, .context = s,
                        .enter = enter, .handle_key = handle_key, .update = update, .draw = draw};
    show(s, BLUETOOTH_MAIN);
    return &s->screen;
}
