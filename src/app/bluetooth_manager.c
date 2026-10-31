#include "bluetooth_manager.h"

#include <string.h>
#include "core/hash.h"
#include "hal/storage.h"

const char BLUETOOTH_STORE_PATH[] = "/.echo/bluetooth.bin";
enum { RECORD_SIZE = 38, FILE_SIZE = 8 + RECORD_SIZE * BLUETOOTH_SAVED_CAPACITY + 4,
       RETRY_MS = 2000, ATTEMPT_MS = 8000 };
typedef struct Saved {
    bool enabled;
    uint8_t count;
    uint8_t preferred;
    HalBluetoothDevice devices[BLUETOOTH_SAVED_CAPACITY];
} Saved;
static Saved saved;
static bool pairing, suspended, pending, automatic, save_failed, queued;
static bool pending_pair;
static bool session_authorized;
static HalBluetoothDevice session_device;
static HalBluetoothDevice target;
static uint8_t next_device;
static uint32_t attempted_ms, idle_since_ms, connected_since_ms;
static HalBluetoothState previous;

static bool same(const HalBluetoothDevice *a, const HalBluetoothDevice *b)
{
    return memcmp(a->address, b->address, HAL_BLUETOOTH_ADDRESS_SIZE) == 0;
}

static int find(const HalBluetoothDevice *device)
{
    for (uint8_t i = 0; i < saved.count; ++i) {
        if (same(device, &saved.devices[i])) return i;
    }
    return -1;
}

static bool store(const Saved *value)
{
    uint8_t bytes[FILE_SIZE] = {'N', 'B', 'T', '1'};
    bytes[4] = value->enabled;
    bytes[5] = value->count;
    bytes[6] = value->preferred;
    for (uint8_t i = 0; i < value->count; ++i) {
        memcpy(bytes + 8 + RECORD_SIZE * i, value->devices[i].address, 6);
        memcpy(bytes + 14 + RECORD_SIZE * i, value->devices[i].name, 32);
    }
    uint32_t checksum = hash_update(HASH_START, bytes, FILE_SIZE - 4);
    for (int i = 0; i < 4; ++i) bytes[FILE_SIZE - 4 + i] = (uint8_t)(checksum >> (8 * i));
    save_failed = !hal_file_write(BLUETOOTH_STORE_PATH, bytes, sizeof bytes);
    return !save_failed;
}

static void load(void)
{
    uint8_t bytes[FILE_SIZE];
    HalFile file;
    if (!hal_file_open(&file, BLUETOOTH_STORE_PATH)) return;
    bool ok = file.size >= FILE_SIZE && hal_file_read(file, 0, bytes, FILE_SIZE);
    hal_file_close(&file);
    if (!ok || memcmp(bytes, "NBT1", 4) != 0 || bytes[4] > 1 ||
        bytes[5] > BLUETOOTH_SAVED_CAPACITY ||
        (bytes[5] ? bytes[6] >= bytes[5] : bytes[6] != 0)) return;
    uint32_t checksum = 0;
    for (int i = 0; i < 4; ++i) checksum |= (uint32_t)bytes[FILE_SIZE - 4 + i] << (8 * i);
    if (checksum != hash_update(HASH_START, bytes, FILE_SIZE - 4)) return;
    saved.enabled = bytes[4] != 0;
    saved.count = bytes[5];
    saved.preferred = bytes[6];
    for (uint8_t i = 0; i < saved.count; ++i) {
        memcpy(saved.devices[i].address, bytes + 8 + RECORD_SIZE * i, 6);
        memcpy(saved.devices[i].name, bytes + 14 + RECORD_SIZE * i, 32);
        saved.devices[i].name[31] = 0;
    }
}

void bluetooth_manager_init(uint32_t now_ms)
{
    saved = (Saved){0};
    pairing = suspended = pending = automatic = save_failed = queued = false;
    pending_pair = false;
    session_authorized = false;
    next_device = 0;
    idle_since_ms = now_ms;
    previous = HAL_BLUETOOTH_OFF;
    load();
    next_device = saved.preferred;
    hal_bluetooth_set_power(false);
    if (saved.enabled) hal_bluetooth_set_power(true);
}

bool bluetooth_manager_enabled(void) { return saved.enabled; }
uint8_t bluetooth_manager_count(void) { return saved.count; }
const HalBluetoothDevice *bluetooth_manager_device(uint8_t i)
{
    return i < saved.count ? &saved.devices[i] : NULL;
}

bool bluetooth_manager_is_connected(const HalBluetoothDevice *device)
{
    const HalBluetoothStatus *status = hal_bluetooth_status();
    return device != NULL && status->state == HAL_BLUETOOTH_CONNECTED &&
           same(device, session_authorized ? &session_device : &status->connected);
}

void bluetooth_manager_set_enabled(bool on, uint32_t now_ms)
{
    saved.enabled = on;
    suspended = pending = automatic = pairing = queued = false;
    pending_pair = false;
    session_authorized = false;
    next_device = saved.preferred;
    idle_since_ms = now_ms;
    hal_bluetooth_set_power(on);
    store(&saved);
}

void bluetooth_manager_pairing(bool on)
{
    if (on && !pairing) {
        HalBluetoothState state = hal_bluetooth_status()->state;
        if (state == HAL_BLUETOOTH_CONNECTED || state == HAL_BLUETOOTH_CONNECTING) {
            bool was_suspended = suspended;
            bluetooth_manager_disconnect();
            suspended = was_suspended;
        }
        pending = pending_pair = automatic = queued = false;
        session_authorized = false;
    }
    if (pairing && !on) hal_bluetooth_stop_scan();
    pairing = on;
}
void bluetooth_manager_scan(void)
{
    if (saved.enabled) hal_bluetooth_scan();
}

static void connect_device(const HalBluetoothDevice *device, bool auto_connect,
                           bool new_pair, uint32_t now_ms)
{
    target = *device;
    session_authorized = false;
    pending = true;
    pending_pair = new_pair;
    suspended = false;
    automatic = auto_connect;
    attempted_ms = now_ms;
    HalBluetoothState state = hal_bluetooth_status()->state;
    queued = state != HAL_BLUETOOTH_READY && state != HAL_BLUETOOTH_SCANNING;
    if (state == HAL_BLUETOOTH_CONNECTED || state == HAL_BLUETOOTH_CONNECTING)
        hal_bluetooth_disconnect();
    if (!queued) hal_bluetooth_connect_device(&target, !pending_pair);
}

void bluetooth_manager_pair(uint8_t index, uint32_t now_ms)
{
    const HalBluetoothStatus *status = hal_bluetooth_status();
    if (!pairing || !saved.enabled || index >= status->device_count ||
        (find(&status->devices[index]) < 0 && saved.count == BLUETOOTH_SAVED_CAPACITY)) return;
    if (status->state == HAL_BLUETOOTH_READY || status->state == HAL_BLUETOOTH_SCANNING)
        connect_device(&status->devices[index], false, true, now_ms);
}

void bluetooth_manager_connect(uint8_t index, uint32_t now_ms)
{
    if (index >= saved.count) return;
    if (!saved.enabled) bluetooth_manager_set_enabled(true, now_ms);
    connect_device(&saved.devices[index], false, false, now_ms);
}

void bluetooth_manager_disconnect(void)
{
    suspended = true;
    session_authorized = false;
    pending = automatic = queued = false;
    pending_pair = false;
    hal_bluetooth_disconnect();
    hal_bluetooth_set_stream(false);
}

bool bluetooth_manager_forget(uint8_t index)
{
    if (index >= saved.count) return false;
    Saved replacement = saved;
    --replacement.count;
    if (replacement.preferred == index) replacement.preferred = 0;
    else if (replacement.preferred > index) --replacement.preferred;
    for (uint8_t i = index; i < replacement.count; ++i) replacement.devices[i] = saved.devices[i + 1];
    if (!store(&replacement)) return false;
    bool active = bluetooth_manager_is_connected(&saved.devices[index]) ||
                  (pending && same(&target, &saved.devices[index]));
    saved = replacement;
    next_device = saved.preferred;
    if (active) {
        session_authorized = false;
        pending = automatic = queued = false;
        hal_bluetooth_disconnect();
        hal_bluetooth_set_stream(false);
    }
    return true;
}

const char *bluetooth_manager_status(void)
{
    if (save_failed) return "Could not save Bluetooth settings";
    if (!saved.enabled) return "Bluetooth is off";
    switch (hal_bluetooth_status()->state) {
    case HAL_BLUETOOTH_STARTING: return "Turning on...";
    case HAL_BLUETOOTH_NO_REPLY: return "Module not responding. Turn off/on.";
    case HAL_BLUETOOTH_SCANNING: return "Searching for devices...";
    case HAL_BLUETOOTH_CONNECTING: return automatic ? "Reconnecting..." : "Connecting...";
    case HAL_BLUETOOTH_CONNECTED: return "Connected";
    default:
        if (hal_bluetooth_status()->pairing == HAL_BLUETOOTH_PAIRING_FAILED) return "Connection failed";
        return suspended ? "Disconnected" : "Ready to connect";
    }
}

void bluetooth_manager_update(uint32_t now_ms)
{
    hal_bluetooth_save_trace();
    if (!saved.enabled) return;
    const HalBluetoothStatus *status = hal_bluetooth_status();
    if (status->state != HAL_BLUETOOTH_CONNECTED) session_authorized = false;
    if (queued) {
        if (status->state == HAL_BLUETOOTH_CONNECTED) hal_bluetooth_disconnect();
        if (status->state == HAL_BLUETOOTH_READY || status->state == HAL_BLUETOOTH_SCANNING) {
            queued = false;
            attempted_ms = now_ms;
            hal_bluetooth_connect_device(&target, !pending_pair);
        }
        previous = status->state;
        return;
    }
    if (status->state == HAL_BLUETOOTH_CONNECTED) {
        if (pairing && !pending && !session_authorized) {
            hal_bluetooth_set_stream(false);
            hal_bluetooth_disconnect();
            return;
        }
        bool selected = pending;
        int index = selected ? find(&target) : find(&status->connected);
        if (selected && index < 0 && !pending_pair) {
            hal_bluetooth_set_stream(false);
            hal_bluetooth_disconnect();
            pending = automatic = false;
            return;
        }
        if ((!session_authorized && index < 0 && !selected) || suspended) {
            hal_bluetooth_set_stream(false);
            hal_bluetooth_disconnect();
            return;
        }
        if (!session_authorized) {
            session_device = selected ? target : saved.devices[index];
            session_authorized = true;
            connected_since_ms = now_ms;
        }
        if (selected) {
            if (pending_pair && index < 0 && saved.count < BLUETOOTH_SAVED_CAPACITY) index = saved.count++;
            if (index >= 0) {
                saved.devices[index] = session_device;
                saved.preferred = (uint8_t)index;
                store(&saved);
            }
        }
        pending = automatic = false;
        pending_pair = false;
        next_device = saved.preferred;
        if (!status->streaming && now_ms - connected_since_ms >= 1000)
            hal_bluetooth_set_stream(true);
    } else {
        if (pending && !automatic && now_ms - attempted_ms >= 30000) pending = false;
        if (previous == HAL_BLUETOOTH_CONNECTED ||
            (previous != HAL_BLUETOOTH_READY && status->state == HAL_BLUETOOTH_READY))
            idle_since_ms = now_ms;
        if (automatic && now_ms - attempted_ms >= ATTEMPT_MS) {
            pending = automatic = false;
            hal_bluetooth_disconnect();
            idle_since_ms = now_ms;
        } else if (!pairing && !suspended && !pending && saved.count && status->state == HAL_BLUETOOTH_READY &&
                   now_ms - idle_since_ms >= RETRY_MS) {
            if (next_device >= saved.count) next_device = 0;
            connect_device(&saved.devices[next_device++], true, false, now_ms);
            idle_since_ms = now_ms;
        }
    }
    previous = status->state;
}
