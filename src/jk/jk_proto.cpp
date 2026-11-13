#include "jk_proto.h"
#include <stdlib.h>
#include <string.h>

// --- Little-endian helpers

static inline uint16_t u16(const uint8_t *b, int i) {
    return static_cast<uint16_t>(b[i] | (b[i + 1] << 8));
}
static inline int16_t i16(const uint8_t *b, int i) {
    return static_cast<int16_t>(u16(b, i));
}
static inline uint32_t u32(const uint8_t *b, int i) {
    return static_cast<uint32_t>(b[i]) | (static_cast<uint32_t>(b[i + 1]) << 8) |
           (static_cast<uint32_t>(b[i + 2]) << 16) | (static_cast<uint32_t>(b[i + 3]) << 24);
}
static inline int32_t i32(const uint8_t *b, int i) {
    return static_cast<int32_t>(u32(b, i));
}
static inline float f32(const uint8_t *b, int i) {
    uint32_t v = u32(b, i);
    float f;
    memcpy(&f, &v, sizeof(f));
    return f;
}
// The float at i if it lies in lo..hi (rejects NaN), else fallback
static float f32In(const uint8_t *b, int i, float lo, float hi, float fallback) {
    float f = f32(b, i);
    return (f >= lo && f <= hi) ? f : fallback;
}
// A fixed-length, NUL padded string field
static void str(const uint8_t *b, int start, int len, char *out) {
    memcpy(out, b + start, len);
    out[len] = '\0';
}

const char *jkProtoName(JkProto p) {
    switch(p) {
    case JkProto::JK02_24S: return "JK02_24S";
    case JkProto::JK02_32S: return "JK02_32S";
    case JkProto::JK04: return "JK04";
    default: return "unknown";
    }
}

static uint8_t sum(const uint8_t *b, size_t n) {
    uint8_t s = 0;
    for(size_t i = 0; i < n; i++) s += b[i];
    return s;
}

// --- Frames

bool JkAssembler::feed(const uint8_t *data, size_t len) {
    // A frame starts with 55 AA EB 90 at the start of a notification
    if(len >= 4 && data[0] == 0x55 && data[1] == 0xAA && data[2] == 0xEB && data[3] == 0x90) {
        len_ = 0;
        started_ = true;
    } else if(!started_) {
        return false;  // the middle of a frame we missed the start of
    }
    for(size_t i = 0; i < len; i++) {
        buf_[len_++] = data[i];
        if(len_ < JK_FRAME_SIZE) continue;
        // complete; anything after it in this notification is padding
        started_ = false;
        len_ = 0;
        return sum(buf_, JK_FRAME_SIZE - 1) == buf_[JK_FRAME_SIZE - 1];
    }
    return false;
}

size_t jkBuildCommand(uint8_t cmd, uint8_t out[20]) {
    memset(out, 0, 20);
    out[0] = 0xAA;
    out[1] = 0x55;
    out[2] = 0x90;
    out[3] = 0xEB;
    out[4] = cmd;
    out[19] = sum(out, 19);
    return 20;
}

// --- Device info (same in every layout)

void jkParseInfo(const uint8_t *b, JkInfo &out) {
    memset(&out, 0, sizeof(out));
    str(b, 6, 16, out.model);
    str(b, 22, 8, out.hw);
    str(b, 30, 8, out.sw);
    out.powerOnCount = u32(b, 42);
    str(b, 46, 16, out.name);
    str(b, 78, 8, out.mfgDate);
    str(b, 86, 11, out.serial);
    out.hwMajor = atoi(out.hw);
    out.valid = true;
}

// --- Layout detection

// Is the frame self-consistent as JK02 with this offset (0: 24 cells, 16: 32)?
static bool jk02Plausible(const uint8_t *b, int offset) {
    uint32_t mask = u32(b, 54 + offset);
    if(mask == 0 || (mask & (mask + 1)) != 0) return false;  // contiguous from bit 0
    int n = __builtin_popcount(mask);
    if(n > 24 + offset / 2) return false;
    uint32_t total = 0;
    for(int i = 0; i < n; i++) {
        uint16_t mv = u16(b, 6 + i * 2);
        if(mv < 500 || mv > 5000) return false;
        total += mv;
    }
    int avg = u16(b, 58 + offset);
    return abs(avg - static_cast<int>(total / n)) <= 20;
}

JkProto jkDetect(const uint8_t *b, int hwMajor) {
    // JK04 sends floats: cell 1 is 0.5..5 V there, while two JK02 uint16 mV
    // values read as a float are a tiny number
    float f = f32(b, 6);
    if(f > 0.5f && f < 5.0f) return JkProto::JK04;
    bool ok24 = jk02Plausible(b, 0);
    bool ok32 = jk02Plausible(b, 16);
    if(ok32 && !ok24) return JkProto::JK02_32S;
    if(ok24 && !ok32) return JkProto::JK02_24S;
    if(hwMajor > 0) return hwMajor >= 11 ? JkProto::JK02_32S : JkProto::JK02_24S;
    return JkProto::JK02_32S;
}

// --- Settings

// JK02 (both cell counts share this part of the layout)
static void settingsJK02(const uint8_t *b, JkSettings &s) {
    s.cellUvpV = u32(b, 10) * 0.001f;
    s.cellUvprV = u32(b, 14) * 0.001f;
    s.cellOvpV = u32(b, 18) * 0.001f;
    s.cellOvprV = u32(b, 22) * 0.001f;
    s.triggerDeltaV = u32(b, 26) * 0.001f;
    s.powerOffV = u32(b, 46) * 0.001f;
    s.maxChargeA = u32(b, 50) * 0.001f;
    s.maxDischargeA = u32(b, 62) * 0.001f;
    s.maxBalanceA = u32(b, 78) * 0.001f;
    uint32_t cells = u32(b, 114);
    s.cellCount = cells <= JK_MAX_CELLS ? cells : 0;
    s.capacityAh = u32(b, 130) * 0.001f;
    s.startBalanceV = u32(b, 138) * 0.001f;
    s.balanceEnabled = b[126] != 0;
}

// JK04, verified against the JK app on a JK-B1A16S by changing each setting:
//   6 start balance voltage (float)   34 cell count (uint8)
//   38 power off voltage (float)      106 trigger delta (float)
//   110 max balance current (float)   114 balancing switch (uint8)
static void settingsJK04(const uint8_t *b, JkSettings &s) {
    s.startBalanceV = f32In(b, 6, 0.0f, 5.0f, 0);
    s.cellCount = b[34] <= JK_MAX_CELLS ? b[34] : 0;
    s.powerOffV = f32In(b, 38, 0.0f, 5.0f, 0);
    s.triggerDeltaV = f32In(b, 106, 0.0f, 5.0f, 0);
    s.maxBalanceA = f32In(b, 110, 0.0f, 20.0f, 0);
    s.balanceEnabled = b[114] != 0;
}

void jkParseSettings(JkProto proto, const uint8_t *b, JkSettings &out) {
    memset(&out, 0, sizeof(out));
    if(proto == JkProto::JK04) {
        settingsJK04(b, out);
    } else {
        settingsJK02(b, out);
    }
    out.valid = true;
}

// --- Cell info

static void minMax(JkCells &c) {
    c.minCell = c.maxCell = -1;
    for(int i = 0; i < c.cellCount; i++) {
        if(c.cellV[i] <= 0) continue;
        if(c.minCell < 0 || c.cellV[i] < c.cellV[c.minCell]) c.minCell = i;
        if(c.maxCell < 0 || c.cellV[i] > c.cellV[c.maxCell]) c.maxCell = i;
    }
    c.minV = c.minCell >= 0 ? c.cellV[c.minCell] : 0;
    c.maxV = c.maxCell >= 0 ? c.cellV[c.maxCell] : 0;
}

// JK02: offset 0 for 24 cells, 16 for 32 cells. Fields after the wire
// resistances move by twice the offset.
static void cellsJK02(const uint8_t *b, int offset, JkCells &c) {
    const int maxCells = 24 + offset / 2;
    const int o2 = offset * 2;
    int n = __builtin_popcount(u32(b, 54 + offset));
    c.cellCount = n > maxCells ? maxCells : n;
    for(int i = 0; i < maxCells && i < JK_MAX_CELLS; i++) {
        c.cellV[i] = u16(b, 6 + i * 2) * 0.001f;
        c.resistOhm[i] = u16(b, 64 + offset + i * 2) * 0.001f;
    }
    c.avgV = u16(b, 58 + offset) * 0.001f;
    c.deltaV = u16(b, 60 + offset) * 0.001f;
    minMax(c);

    c.hasBms = true;
    c.mosC = i16(b, 112 + o2) * 0.1f;
    c.packV = u32(b, 118 + o2) * 0.001f;
    c.currentA = i32(b, 126 + o2) * 0.001f;
    c.powerW = c.packV * c.currentA;
    c.t1C = i16(b, 130 + o2) * 0.1f;
    c.t2C = i16(b, 132 + o2) * 0.1f;
    c.balanceA = i16(b, 138 + o2) * 0.001f;
    c.balancing = b[140 + o2];
    c.soc = b[141 + o2];
    c.remainAh = u32(b, 142 + o2) * 0.001f;
    c.nominalAh = u32(b, 146 + o2) * 0.001f;
    c.cycles = u32(b, 150 + o2);
    c.cycleAh = u32(b, 154 + o2) * 0.001f;
    c.uptimeS = u32(b, 162 + o2);
    c.chargeOn = b[166 + o2] != 0;
    c.dischargeOn = b[167 + o2] != 0;
    c.balanceOn = b[169 + o2] != 0;
}

// JK04, offsets verified against the JK app on a JK-B1A16S (hw 8.X-W, sw 8.05):
//   6 + 4n  cell voltage n (float, V)       102 + 4n  wire resistance n (float, Ohm)
//   202     average cell voltage (float)    206       delta cell voltage (float)
//   218     highest cell (0-based)          219       lowest cell (0-based)
//   220     balancing state (0 waiting, 2 balancing)
//   222     balance current (float, A, negative while balancing)
//   286     uptime (uint24, s)
static void cellsJK04(const uint8_t *b, JkCells &c) {
    const int maxCells = 24;
    int n = 0;
    float total = 0;
    for(int i = 0; i < maxCells; i++) {
        float v = f32(b, 6 + i * 4);
        float r = f32(b, 102 + i * 4);
        if(!(v > 0.5f && v < 5.0f)) v = 0;  // also rejects NaN
        if(!(r >= 0.0f && r < 10.0f)) r = 0;
        c.cellV[i] = v;
        c.resistOhm[i] = r;
        if(v > 0) {
            n = i + 1;
            total += v;
        }
    }
    c.cellCount = n;
    c.packV = total;
    minMax(c);
    // the balancer's own figures if they look right
    c.avgV = f32In(b, 202, 0.5f, 5.0f, n ? total / n : 0);
    c.deltaV = f32In(b, 206, 0.0f, 5.0f, c.maxV - c.minV);
    if(b[218] < n) c.maxCell = b[218];
    if(b[219] < n) c.minCell = b[219];
    c.balancing = b[220];
    c.balanceA = f32In(b, 222, -20.0f, 20.0f, 0);
    c.uptimeS = b[286] | (b[287] << 8) | (b[288] << 16);
    c.hasBms = false;
}

void jkParseCells(JkProto proto, const uint8_t *b, JkCells &out) {
    memset(&out, 0, sizeof(out));
    switch(proto) {
    case JkProto::JK04: cellsJK04(b, out); break;
    case JkProto::JK02_24S: cellsJK02(b, 0, out); break;
    default: cellsJK02(b, 16, out); break;
    }
}
