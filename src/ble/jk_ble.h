#pragma once
// Bluetooth LE link to one JK BMS or JK active balancer (NimBLE, central).
//
// The device is picked from a scan and saved in NVS; after that the display
// connects to it on every boot and reconnects when the link drops. The JK
// device streams its cell info frames on its own once asked (about one a
// second); everything decoded goes to bms/bms_data.h.
//
// A JK device takes one connection at a time and doesn't advertise while the
// phone app is connected to it.

#include <stdint.h>

struct ScanEntry {
    uint8_t addrType;
    uint8_t addr[6];  // NimBLE order
    char name[24];    // empty if the device doesn't advertise one
    int8_t rssi;
};

// Starts NimBLE and the link task. Connects to the saved device, if any.
void bleStart();

// Device picker: scans (and drops the current link) until bleSelect()
void blePickStart();
bool blePicking();
// The JK devices seen so far, strongest signal first; returns the count
int blePickResults(ScanEntry *out, int max);
// Saves the device and connects to it
void bleSelect(const ScanEntry &entry);
// Stops the picker and goes back to the saved device (if any)
void blePickCancel();

// Forgets the saved device and disconnects
void bleForget();

// "C8:47:80:23:4F:95" from NimBLE's byte order
void bleAddrStr(const uint8_t addr[6], char out[18]);
// Parses "c8:47:80:23:4f:95" into NimBLE's byte order
bool bleAddrParse(const char *s, uint8_t addr[6]);
