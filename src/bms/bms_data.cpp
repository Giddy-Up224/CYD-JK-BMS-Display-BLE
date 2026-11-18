#include "bms_data.h"
#include <string.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t lock_ = nullptr;
static BmsSnapshot data_ = {};

// Raw frames by type 1..3
static uint8_t raw_[3][JK_FRAME_SIZE];
static bool haveRaw_[3];

struct Lock {
    Lock() { xSemaphoreTake(lock_, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(lock_); }
};

void bmsDataInit() {
    lock_ = xSemaphoreCreateMutex();
}

void bmsDataSetLink(LinkState link) {
    Lock l;
    if(data_.link == link) return;
    data_.link = link;
    data_.seq++;
}

void bmsDataSetDevice(const char *name, const char *addr) {
    Lock l;
    strlcpy(data_.name, name, sizeof(data_.name));
    strlcpy(data_.addr, addr, sizeof(data_.addr));
    data_.seq++;
}

void bmsDataSetProto(JkProto proto) {
    Lock l;
    data_.proto = proto;
    data_.seq++;
}

void bmsDataSetInfo(const JkInfo &info) {
    Lock l;
    data_.info = info;
    data_.infoUs = esp_timer_get_time();
    data_.seq++;
}

void bmsDataSetSettings(const JkSettings &settings) {
    Lock l;
    data_.settings = settings;
    data_.settingsUs = esp_timer_get_time();
    data_.seq++;
}

void bmsDataPutCells(const JkCells &cells) {
    Lock l;
    data_.cells = cells;
    data_.hasCells = true;
    data_.cellsUs = esp_timer_get_time();
    data_.cellFrames++;
    data_.seq++;
}

void bmsDataClear() {
    Lock l;
    data_.proto = JkProto::Unknown;
    memset(&data_.info, 0, sizeof(data_.info));
    memset(&data_.settings, 0, sizeof(data_.settings));
    memset(&data_.cells, 0, sizeof(data_.cells));
    data_.hasCells = false;
    data_.cellFrames = 0;
    memset(haveRaw_, 0, sizeof(haveRaw_));
    data_.seq++;
}

void bmsDataGet(BmsSnapshot &out) {
    Lock l;
    out = data_;
}

void bmsDataPutRaw(uint8_t type, const uint8_t *frame) {
    if(type < 1 || type > 3) return;
    Lock l;
    memcpy(raw_[type - 1], frame, JK_FRAME_SIZE);
    haveRaw_[type - 1] = true;
}

bool bmsDataGetRaw(uint8_t type, uint8_t *out) {
    if(type < 1 || type > 3) return false;
    Lock l;
    if(!haveRaw_[type - 1]) return false;
    memcpy(out, raw_[type - 1], JK_FRAME_SIZE);
    return true;
}
