#pragma once
// Settings kept in NVS: the JK device this display shows.

#include <stdint.h>

struct SavedDevice {
    uint8_t addrType;  // BLE_ADDR_PUBLIC / BLE_ADDR_RANDOM
    uint8_t addr[6];   // NimBLE order (little-endian: addr[5] is the first byte shown)
    char name[24];
};

// False if none is saved
bool prefsGetDevice(SavedDevice &out);
void prefsSetDevice(const SavedDevice &device);
void prefsForgetDevice();
