#pragma once
// The latest state of the JK device, written by the BLE link (ble/jk_ble.cpp)
// and read by the UI and the console. Thread-safe: each call copies under a
// lock.

#include <stdint.h>
#include "jk/jk_proto.h"

enum class LinkState : uint8_t {
    NoDevice,    // none picked yet
    Picking,     // scanning for the device list
    Connecting,  // looking for the saved device / setting up the link
    Connected,   // link up (data may still be on its way)
};

struct BmsSnapshot {
    LinkState link;
    char name[24];      // advertised name of the saved device
    char addr[18];      // "C8:47:80:23:4F:95"
    uint32_t seq;       // +1 on every change
    JkProto proto;
    JkInfo info;
    int64_t infoUs;     // esp_timer time of the last info frame
    JkSettings settings;
    int64_t settingsUs; // ... and settings frame
    bool hasCells;      // cells is valid (since the last connect)
    int64_t cellsUs;    // esp_timer time of the last cell frame
    uint32_t cellFrames;
    JkCells cells;
};

void bmsDataInit();
void bmsDataSetLink(LinkState link);
void bmsDataSetDevice(const char *name, const char *addr);
void bmsDataSetProto(JkProto proto);
void bmsDataSetInfo(const JkInfo &info);
void bmsDataSetSettings(const JkSettings &settings);
void bmsDataPutCells(const JkCells &cells);
// Forgets everything received, e.g. on a new connection
void bmsDataClear();
void bmsDataGet(BmsSnapshot &out);

// Raw frames for the console's `dump` (type JK_FRAME_...)
void bmsDataPutRaw(uint8_t type, const uint8_t *frame);
bool bmsDataGetRaw(uint8_t type, uint8_t *out);
