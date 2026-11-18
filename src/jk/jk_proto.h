#pragma once
// JK BMS / JK active balancer BLE protocol: frame assembly and decoding.
// No BLE or RTOS code here, just bytes in and values out.
//
// The device talks over service FFE0, characteristic FFE1 (notify + write).
// Requests are 20-byte frames (jkBuildCommand). Replies are 300-byte frames:
//   55 AA EB 90 <type> ... <sum of bytes 0..298>
// type 0x01 settings, 0x02 cell info (streamed about once a second),
// 0x03 device info.
//
// Several layouts of the same frames exist, detected from the first cell info
// frame (jkDetect):
//   JK02_24S  older BMS (hw < 11) and JK02 balancers: 24 cells, integers
//   JK02_32S  newer BMS (hw >= 11, e.g. JK-B1A8S10P): 32 cells, integers
//   JK04      older balancers (e.g. JK-B1A16S hw 8.x): little-endian floats

#include <stddef.h>
#include <stdint.h>

#define JK_MAX_CELLS 32
#define JK_FRAME_SIZE 300

enum class JkProto : uint8_t { Unknown, JK02_24S, JK02_32S, JK04 };
const char *jkProtoName(JkProto p);

enum : uint8_t {
    JK_FRAME_SETTINGS = 0x01,
    JK_FRAME_CELLS = 0x02,
    JK_FRAME_INFO = 0x03,
};

enum : uint8_t {
    JK_CMD_CELL_INFO = 0x96,
    JK_CMD_DEVICE_INFO = 0x97,
    JK_CMD_RENAME = 0x9B,
};

#define JK_NAME_MAX 13  // what fits in a request frame (bytes 6..18)

// Device info frame (0x03)
struct JkInfo {
    bool valid;
    char model[17];     // e.g. "JK_B1A8S10P"
    char hw[9];         // e.g. "11.XW"
    char sw[9];
    char name[17];      // name set in the app
    char mfgDate[9];
    char serial[12];
    uint32_t powerOnCount;
    int hwMajor;        // leading number of hw
};

// Settings frame (0x01). Fields a layout doesn't have stay 0.
struct JkSettings {
    bool valid;
    int cellCount;
    float startBalanceV;      // balancing starts above this cell voltage
    float triggerDeltaV;      // ... when the cells are this far apart
    float maxBalanceA;
    float powerOffV;
    bool balanceEnabled;
    // BMS only
    float cellUvpV, cellUvprV, cellOvpV, cellOvprV;
    float maxChargeA, maxDischargeA;
    float capacityAh;
    bool chargeEnabled, dischargeEnabled;
};

// Cell info frame (0x02)
struct JkCells {
    int cellCount;
    float cellV[JK_MAX_CELLS];
    float resistOhm[JK_MAX_CELLS];   // balance wire resistance
    float avgV, deltaV, minV, maxV;
    int minCell, maxCell;            // 0-based, -1 if unknown
    float packV;
    float balanceA;                  // + charging the low cell / - discharging the high one
    uint8_t balancing;               // 0 off/waiting, else balancing (JK02: 1 charge, 2 discharge)
    uint32_t uptimeS;
    // BMS only (hasBms)
    bool hasBms;
    float currentA, powerW;
    float t1C, t2C, mosC;
    int soc;
    float remainAh, nominalAh, cycleAh;
    uint32_t cycles;
    bool chargeOn, dischargeOn, balanceOn;
};

// Collects notification payloads into frames
class JkAssembler {
public:
    void reset() { len_ = 0; started_ = false; }
    // True when a complete frame with a good checksum is ready in frame()
    bool feed(const uint8_t *data, size_t len);
    const uint8_t *frame() const { return buf_; }
    uint8_t type() const { return buf_[4]; }

private:
    uint8_t buf_[JK_FRAME_SIZE];
    size_t len_ = 0;
    bool started_ = false;
};

// Request frame for cmd (JK_CMD_...); returns its length (20)
size_t jkBuildCommand(uint8_t cmd, uint8_t out[20]);
// Rename frame: 0x9B, the length in byte 5, the name from byte 6. Returns 0
// if the name is empty, longer than JK_NAME_MAX or not printable ASCII.
size_t jkBuildRename(const char *name, uint8_t out[20]);
// Any register write (for decoding new devices)
size_t jkBuildRaw(uint8_t reg, uint8_t len, uint32_t value, uint8_t out[20]);

// --- Writing settings
//
// A write is a request frame with a register in byte 4, the value's length
// in byte 5 and the value in bytes 6..9 (little-endian). JK02 takes integers
// (mV, mA); JK04 takes floats for voltages and currents. The device doesn't
// answer; the new value shows in the next settings frame. Register numbers
// from syssi/esphome-jk-bms.
enum class JkSetting : uint8_t {
    StartBalanceV,   // balancing starts above this cell voltage
    TriggerDeltaV,   // ... when the cells are this far apart
    MaxBalanceA,
    PowerOffV,
    CellCount,
    Balancing,       // switch: 0 / 1
    // BMS only
    Charging,        // switch
    Discharging,     // switch
    CellUvpV, CellUvprV, CellOvpV, CellOvprV,
    Count
};

struct JkSettingInfo {
    const char *key;     // console name, e.g. "trigger"
    const char *label;   // e.g. "Trigger delta"
    const char *unit;    // "V", "A", "" (count / switch)
    float min, max, step;
    bool isSwitch;
};
const JkSettingInfo &jkSettingInfo(JkSetting s);
// The setting named key, or JkSetting::Count
JkSetting jkSettingByKey(const char *key);
// Does this layout have the setting?
bool jkSettingSupported(JkProto proto, JkSetting s);
// Write frame for the setting; returns its length (20), or 0 if the layout
// doesn't have it or the value is out of range
size_t jkBuildWrite(JkProto proto, JkSetting s, float value, uint8_t out[20]);

// The setting's current value in a parsed settings frame
float jkSettingValue(const JkSettings &st, JkSetting s);

// Layout of a cell info frame. hwMajor from the info frame (0 if unknown).
JkProto jkDetect(const uint8_t *frame, int hwMajor);

void jkParseInfo(const uint8_t *frame, JkInfo &out);
// From the model name: 1 balancer, 0 BMS, -1 can't tell. BMS models end in
// their current rating ("JK-B1A8S10P", "JK_PB2A16S20P"), balancers in the
// cell count ("JK-B1A16S", "JK_B2A16S").
int jkIsBalancerModel(const char *model);
void jkParseSettings(JkProto proto, const uint8_t *frame, JkSettings &out);
void jkParseCells(JkProto proto, const uint8_t *frame, JkCells &out);
