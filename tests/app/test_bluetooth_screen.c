#include <string.h>
#include "check.h"
#include "bluetooth_manager.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "screens/bluetooth_screen.h"

static BluetoothScreen ui;
static const HalBluetoothDevice phones = {{1,2,3,4,5,6}, "WH-1000XM5"};
static void reset(void)
{
    fake_hal_reset();
    bluetooth_manager_init(1000);
    bluetooth_screen_init(&ui, &DEFAULT_THEME);
    ui.screen.enter(&ui, 1000);
}
static void press(Key key)
{
    ui.screen.handle_key(&ui, (KeyEvent){key, KEY_GESTURE_PRESS}, 1100);
}
static void pair(void)
{
    bluetooth_manager_set_enabled(true, 1000);
    HalBluetoothStatus *s = fake_hal_bluetooth();
    s->state = HAL_BLUETOOTH_READY;
    s->devices[0] = phones;
    s->device_count = 1;
    bluetooth_manager_pairing(true);
    bluetooth_manager_pair(0, 1500);
    s->state = HAL_BLUETOOTH_CONNECTED;
    s->connected = phones;
    bluetooth_manager_update(2000);
    CHECK(!s->streaming);
    bluetooth_manager_update(3000);
    bluetooth_manager_pairing(false);
}
static void off_by_default_and_opening_does_not_enable(void)
{
    reset();
    CHECK(!bluetooth_manager_enabled());
    CHECK(!fake_hal_bluetooth_powered());
    CHECK(strcmp("Off", ui.model.detail(&ui, 0)) == 0);
    CHECK_EQUAL(BLUETOOTH_MAIN, ui.page);
}
static void pairing_menu_waits_for_ready_then_searches(void)
{
    reset(); press(KEY_WHEEL_DOWN); press(KEY_PLAY);
    CHECK_EQUAL(BLUETOOTH_PAIR, ui.page);
    ui.screen.update(&ui, 1200);
    CHECK_EQUAL(0, fake_hal_bluetooth_scan_count());
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_READY;
    ui.screen.update(&ui, 1500);
    ui.screen.update(&ui, 1600);
    CHECK_EQUAL(1, fake_hal_bluetooth_scan_count());
    press(KEY_BACK);
    CHECK_EQUAL(BLUETOOTH_MAIN, ui.page);
    CHECK(fake_hal_bluetooth_powered());
    CHECK_EQUAL(HAL_BLUETOOTH_READY, fake_hal_bluetooth()->state);
}
static void connection_is_saved_and_audio_starts_automatically(void)
{
    reset(); pair();
    CHECK_EQUAL(1, bluetooth_manager_count());
    CHECK(strcmp(phones.name, bluetooth_manager_device(0)->name) == 0);
    CHECK(fake_hal_bluetooth()->streaming);
    ui.screen.handle_key(&ui, (KeyEvent){KEY_BACK, KEY_GESTURE_PRESS}, 2100);
    CHECK(fake_hal_bluetooth_powered());
    CHECK_EQUAL(HAL_BLUETOOTH_CONNECTED, fake_hal_bluetooth()->state);
}
static void saved_device_reconnects_without_discovery_after_reload(void)
{
    reset(); pair();
    bluetooth_manager_init(3000);
    CHECK(bluetooth_manager_enabled());
    CHECK_EQUAL(1, bluetooth_manager_count());
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_READY;
    bluetooth_manager_update(3300);
    bluetooth_manager_update(5400);
    CHECK_EQUAL(HAL_BLUETOOTH_CONNECTING, fake_hal_bluetooth()->state);
    CHECK(memcmp(phones.address, fake_hal_bluetooth()->connected.address, 6) == 0);
    CHECK_EQUAL(0, fake_hal_bluetooth_scan_count());
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_CONNECTED;
    bluetooth_manager_update(6000);
    bluetooth_manager_update(7000);
    CHECK(fake_hal_bluetooth()->streaming);
    CHECK_EQUAL(1, bluetooth_manager_count());
}
static void powered_off_headphones_are_retried_and_can_return(void)
{
    reset(); pair();
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_READY;
    bluetooth_manager_update(3000);
    bluetooth_manager_update(5100);
    CHECK_EQUAL(HAL_BLUETOOTH_CONNECTING, fake_hal_bluetooth()->state);
    bluetooth_manager_update(13200);
    CHECK_EQUAL(HAL_BLUETOOTH_READY, fake_hal_bluetooth()->state);
    bluetooth_manager_update(15300);
    CHECK_EQUAL(HAL_BLUETOOTH_CONNECTING, fake_hal_bluetooth()->state);
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_CONNECTED;
    bluetooth_manager_update(16000);
    bluetooth_manager_update(17000);
    CHECK(fake_hal_bluetooth()->streaming);
}
static void manual_disconnect_stays_disconnected_until_requested(void)
{
    reset(); pair(); bluetooth_manager_disconnect();
    int attempts = fake_hal_bluetooth_connect_count();
    bluetooth_manager_update(3000); bluetooth_manager_update(30000);
    CHECK_EQUAL(attempts, fake_hal_bluetooth_connect_count());
    bluetooth_manager_connect(0, 31000);
    CHECK_EQUAL(attempts + 1, fake_hal_bluetooth_connect_count());
}
static void off_is_saved_and_preserves_devices(void)
{
    reset(); pair(); bluetooth_manager_set_enabled(false, 3000);
    bluetooth_manager_init(4000);
    CHECK(!bluetooth_manager_enabled());
    CHECK(!fake_hal_bluetooth_powered());
    CHECK_EQUAL(1, bluetooth_manager_count());
    bluetooth_manager_update(30000);
    CHECK_EQUAL(HAL_BLUETOOTH_OFF, fake_hal_bluetooth()->state);
}
static void forget_requires_confirmation_and_prevents_reconnection(void)
{
    reset(); pair();
    list_view_select(&ui.list, 2, 2000); press(KEY_PLAY); press(KEY_PLAY);
    CHECK_EQUAL(BLUETOOTH_DEVICE, ui.page);
    press(KEY_WHEEL_DOWN); press(KEY_PLAY);
    CHECK_EQUAL(BLUETOOTH_FORGET, ui.page);
    CHECK_EQUAL(1, bluetooth_manager_count());
    press(KEY_PLAY); /* Cancel is initially selected. */
    CHECK_EQUAL(1, bluetooth_manager_count());
    press(KEY_WHEEL_DOWN); press(KEY_PLAY); press(KEY_WHEEL_DOWN); press(KEY_PLAY);
    CHECK_EQUAL(0, bluetooth_manager_count());
    CHECK_EQUAL(BLUETOOTH_SAVED, ui.page);
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_CONNECTED;
    fake_hal_bluetooth()->connected = phones;
    bluetooth_manager_update(4000);
    CHECK_EQUAL(HAL_BLUETOOTH_READY, fake_hal_bluetooth()->state);
    bluetooth_manager_init(5000);
    CHECK_EQUAL(0, bluetooth_manager_count());
}
static void corrupted_storage_defaults_to_off(void)
{
    reset();
    uint8_t corrupt[240] = {'N','B','T','1',1,255};
    fake_hal_add_file(BLUETOOTH_STORE_PATH, corrupt, sizeof corrupt);
    bluetooth_manager_init(1000);
    CHECK(!bluetooth_manager_enabled());
    CHECK_EQUAL(0, bluetooth_manager_count());
}
static void selecting_saved_device_can_turn_bluetooth_on(void)
{
    reset(); pair(); bluetooth_manager_set_enabled(false, 3000);
    bluetooth_manager_connect(0, 4000);
    CHECK(bluetooth_manager_enabled());
    CHECK_EQUAL(HAL_BLUETOOTH_STARTING, fake_hal_bluetooth()->state);
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_READY;
    bluetooth_manager_update(4300);
    CHECK_EQUAL(HAL_BLUETOOTH_CONNECTING, fake_hal_bluetooth()->state);
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_CONNECTED;
    bluetooth_manager_update(5000);
    bluetooth_manager_update(6000);
    CHECK(fake_hal_bluetooth()->streaming);
}

static void reconnect_prefers_last_device_then_tries_other_saved_devices(void)
{
    reset(); pair(); bluetooth_manager_disconnect();
    HalBluetoothDevice speaker = {{7,8,9,10,11,12}, "Speaker"};
    fake_hal_bluetooth()->devices[1] = speaker;
    fake_hal_bluetooth()->device_count = 2;
    bluetooth_manager_pairing(true);
    bluetooth_manager_pair(1, 3000);
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_CONNECTED;
    bluetooth_manager_update(4000);
    bluetooth_manager_pairing(false);
    CHECK_EQUAL(2, bluetooth_manager_count());
    CHECK(strcmp(phones.name, bluetooth_manager_device(0)->name) == 0);
    CHECK(strcmp(speaker.name, bluetooth_manager_device(1)->name) == 0);
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_READY;
    bluetooth_manager_update(5000); bluetooth_manager_update(7100);
    CHECK(strcmp(speaker.name, fake_hal_bluetooth()->connected.name) == 0);
    bluetooth_manager_update(15200); bluetooth_manager_update(17300);
    CHECK(strcmp(phones.name, fake_hal_bluetooth()->connected.name) == 0);
}
static void later_status_reports_do_not_reject_an_approved_session(void)
{
    reset(); pair();
    int disconnects = fake_hal_bluetooth_disconnect_count();
    fake_hal_bluetooth()->connected.address[0] ^= 0x80;
    bluetooth_manager_update(3000);
    bluetooth_manager_update(4000);
    CHECK_EQUAL(disconnects, fake_hal_bluetooth_disconnect_count());
    CHECK(fake_hal_bluetooth()->streaming);
    CHECK(bluetooth_manager_is_connected(bluetooth_manager_device(0)));
    CHECK(bluetooth_manager_forget(0));
    CHECK_EQUAL(disconnects + 1, fake_hal_bluetooth_disconnect_count());
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_CONNECTED;
    bluetooth_manager_update(5000);
    CHECK_EQUAL(disconnects + 2, fake_hal_bluetooth_disconnect_count());
}

static void initial_status_address_does_not_cancel_the_requested_headphones(void)
{
    reset();
    bluetooth_manager_set_enabled(true, 1000);
    HalBluetoothStatus *status = fake_hal_bluetooth();
    status->state = HAL_BLUETOOTH_READY;
    status->devices[0] = phones;
    status->device_count = 1;
    bluetooth_manager_pairing(true);
    bluetooth_manager_pair(0, 1500);
    status->state = HAL_BLUETOOTH_CONNECTED;
    status->connected.address[0] ^= 0x80;
    status->connected.name[0] = 0;
    bluetooth_manager_update(2000);
    CHECK_EQUAL(0, fake_hal_bluetooth_disconnect_count());
    CHECK_EQUAL(1, bluetooth_manager_count());
    CHECK(memcmp(phones.address, bluetooth_manager_device(0)->address, 6) == 0);
    bluetooth_manager_update(2999);
    CHECK(!status->streaming);
    bluetooth_manager_update(3000);
    CHECK(status->streaming);
}

static void discovering_a_device_never_pairs_without_a_click(void)
{
    reset(); press(KEY_WHEEL_DOWN); press(KEY_PLAY);
    HalBluetoothStatus *status = fake_hal_bluetooth();
    status->state = HAL_BLUETOOTH_READY;
    ui.screen.update(&ui, 1500);
    status->devices[0] = phones;
    status->device_count = 1;
    for (uint32_t now = 2000; now <= 20000; now += 1000) {
        bluetooth_manager_update(now);
        ui.screen.update(&ui, now);
    }
    CHECK_EQUAL(0, fake_hal_bluetooth_connect_count());
    CHECK_EQUAL(0, bluetooth_manager_count());
    status->state = HAL_BLUETOOTH_CONNECTED;
    status->connected = phones;
    bluetooth_manager_update(21000);
    CHECK_EQUAL(0, bluetooth_manager_count());
    CHECK_EQUAL(HAL_BLUETOOTH_READY, status->state);
    CHECK(!status->streaming);
    list_view_select(&ui.list, 2, 22000);
    ui.screen.handle_key(&ui, (KeyEvent){KEY_PLAY, KEY_GESTURE_PRESS}, 22000);
    CHECK_EQUAL(1, fake_hal_bluetooth_connect_count());
    CHECK(!fake_hal_bluetooth_last_reconnect());
    status->state = HAL_BLUETOOTH_CONNECTED;
    bluetooth_manager_update(23000);
    CHECK_EQUAL(1, bluetooth_manager_count());
}

static void pairing_menu_cancels_and_pauses_saved_device_reconnects(void)
{
    reset(); pair();
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_READY;
    bluetooth_manager_update(4000); bluetooth_manager_update(6100);
    CHECK(fake_hal_bluetooth_last_reconnect());
    CHECK_EQUAL(HAL_BLUETOOTH_CONNECTING, fake_hal_bluetooth()->state);
    int attempts = fake_hal_bluetooth_connect_count();
    bluetooth_manager_pairing(true);
    CHECK_EQUAL(HAL_BLUETOOTH_READY, fake_hal_bluetooth()->state);
    bluetooth_manager_update(20000);
    CHECK_EQUAL(attempts, fake_hal_bluetooth_connect_count());
    fake_hal_bluetooth()->state = HAL_BLUETOOTH_CONNECTED;
    fake_hal_bluetooth()->connected = phones;
    bluetooth_manager_update(21000);
    CHECK_EQUAL(HAL_BLUETOOTH_READY, fake_hal_bluetooth()->state);
    CHECK_EQUAL(1, bluetooth_manager_count());
}

static void a_matching_name_does_not_authorize_an_unknown_address(void)
{
    reset(); pair(); bluetooth_manager_init(4000);
    HalBluetoothStatus *status = fake_hal_bluetooth();
    status->state = HAL_BLUETOOTH_CONNECTED;
    status->connected = phones;
    status->connected.address[0] ^= 0x80;
    bluetooth_manager_update(5000);
    CHECK_EQUAL(HAL_BLUETOOTH_READY, status->state);
    CHECK_EQUAL(1, bluetooth_manager_count());
    CHECK(memcmp(phones.address, bluetooth_manager_device(0)->address, 6) == 0);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(off_by_default_and_opening_does_not_enable),
        TEST_CASE(discovering_a_device_never_pairs_without_a_click),
        TEST_CASE(pairing_menu_cancels_and_pauses_saved_device_reconnects),
        TEST_CASE(a_matching_name_does_not_authorize_an_unknown_address),
        TEST_CASE(initial_status_address_does_not_cancel_the_requested_headphones),
        TEST_CASE(later_status_reports_do_not_reject_an_approved_session),
        TEST_CASE(pairing_menu_waits_for_ready_then_searches),
        TEST_CASE(connection_is_saved_and_audio_starts_automatically),
        TEST_CASE(saved_device_reconnects_without_discovery_after_reload),
        TEST_CASE(powered_off_headphones_are_retried_and_can_return),
        TEST_CASE(manual_disconnect_stays_disconnected_until_requested),
        TEST_CASE(off_is_saved_and_preserves_devices),
        TEST_CASE(forget_requires_confirmation_and_prevents_reconnection),
        TEST_CASE(corrupted_storage_defaults_to_off),
        TEST_CASE(selecting_saved_device_can_turn_bluetooth_on),
        TEST_CASE(reconnect_prefers_last_device_then_tries_other_saved_devices),
    };
    return test_run_all(cases, sizeof cases / sizeof cases[0]);
}
