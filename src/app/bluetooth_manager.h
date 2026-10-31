#pragma once

#include "hal/bluetooth.h"

enum { BLUETOOTH_SAVED_CAPACITY = 6 };
extern const char BLUETOOTH_STORE_PATH[];

void bluetooth_manager_init(uint32_t now_ms);
void bluetooth_manager_update(uint32_t now_ms);
bool bluetooth_manager_enabled(void);
void bluetooth_manager_set_enabled(bool on, uint32_t now_ms);
uint8_t bluetooth_manager_count(void);
const HalBluetoothDevice *bluetooth_manager_device(uint8_t index);
bool bluetooth_manager_is_connected(const HalBluetoothDevice *device);
const char *bluetooth_manager_status(void);
void bluetooth_manager_pairing(bool on);
void bluetooth_manager_scan(void);
void bluetooth_manager_pair(uint8_t index, uint32_t now_ms);
void bluetooth_manager_connect(uint8_t index, uint32_t now_ms);
void bluetooth_manager_disconnect(void);
bool bluetooth_manager_forget(uint8_t index);
