#include "jkbms.h"
#include "../utils/utils.h"
#include "../config/config.h"
#include "prefs.h"

#include <mutex>
#include <algorithm>

//********************************************
// Globals
//********************************************

JKBMS jkBmsDevices[MAX_BMS_DEVICES];
const int bmsDeviceCount = MAX_BMS_DEVICES;

static NimBLEScan *pScan = nullptr;
static uint32_t lastScanTime = 0;
static uint32_t userScanUntil = 0;  // millis() until which a user requested scan should run

// JK devices found while scanning. Written from the NimBLE host task,
// read from the UI (loop task), so access is guarded by a mutex.
static std::mutex foundMutex;
static std::vector<FoundDevice> foundDevices;
static volatile uint32_t foundVersion = 0;
#define MAX_FOUND_DEVICES 20

//********************************************
// Little-endian helpers
//********************************************
static inline uint16_t le_u16(const uint8_t *b, int i) { return (uint16_t)(b[i] | (b[i + 1] << 8)); }
static inline int16_t le_i16(const uint8_t *b, int i) { return (int16_t)le_u16(b, i); }
static inline uint32_t le_u32(const uint8_t *b, int i) {
  return (uint32_t)b[i] | ((uint32_t)b[i + 1] << 8) | ((uint32_t)b[i + 2] << 16) | ((uint32_t)b[i + 3] << 24);
}
static inline int32_t le_i32(const uint8_t *b, int i) { return (int32_t)le_u32(b, i); }
static inline float le_f32(const uint8_t *b, int i) {
  uint32_t v = le_u32(b, i);
  float f;
  memcpy(&f, &v, sizeof(f));
  return f;
}

// Copy a fixed-length, NUL padded string field out of a frame
static std::string frame_str(const uint8_t *b, int start, int len) {
  std::string s(reinterpret_cast<const char *>(b + start), len);
  size_t nul = s.find('\0');
  if (nul != std::string::npos) s.resize(nul);
  return s;
}

const char *jk_protocol_name(JKProtocol p) {
  switch (p) {
    case JKProtocol::JK02_24S: return "JK02_24S";
    case JKProtocol::JK02_32S: return "JK02_32S";
    case JKProtocol::JK04: return "JK04";
    default: return "unknown";
  }
}

//********************************************
// JKBMS
//********************************************

void JKBMS::assign(const std::string &mac, uint8_t type, const std::string &devName) {
  *this = JKBMS();
  targetMAC = mac;
  addrType = type;
  name = devName;
}

void JKBMS::clear() {
  *this = JKBMS();
}

uint8_t JKBMS::crc(const uint8_t data[], uint16_t len) {
  uint8_t crc = 0;
  for (uint16_t i = 0; i < len; i++) crc += data[i];
  return crc;
}

bool JKBMS::connectToServer() {
  DEBUG_PRINTF("Attempting to connect to %s...\n", targetMAC.c_str());
  NimBLEAddress addr = address();
  NimBLEClient *pClient = NimBLEDevice::getClientByPeerAddress(addr);

  if (!pClient) {
    if (NimBLEDevice::getCreatedClientCount() >= CONFIG_BT_NIMBLE_MAX_CONNECTIONS) {
      DEBUG_PRINTLN("Max BLE clients reached, cannot connect.");
      return false;
    }
    pClient = NimBLEDevice::createClient();
    DEBUG_PRINTLN("New client created.");
    pClient->setClientCallbacks(new ClientCallbacks(targetMAC), true);
    pClient->setConnectionParams(12, 12, 0, 150);
    pClient->setConnectTimeout(5000);
  }

  if (!pClient->connect(addr)) {
    DEBUG_PRINTF("Failed to connect to %s\n", targetMAC.c_str());
    return false;
  }

  DEBUG_PRINTF("Connected to: %s RSSI: %d\n", pClient->getPeerAddress().toString().c_str(), pClient->getRssi());
  lastNotifyTime = millis();  // Start the notify timeout from now

  NimBLERemoteService *pSvc = pClient->getService("ffe0");
  if (pSvc) {
    pChr = pSvc->getCharacteristic("ffe1");
    if (pChr && pChr->canNotify()) {
      if (pChr->subscribe(true, notifyCB)) {
        DEBUG_PRINTF("Subscribed to notifications for %s\n", pChr->getUUID().toString().c_str());
        delay(500);
        writeRegister(0x97, 0x00000000, 0x00);  // COMMAND_DEVICE_INFO
        delay(500);
        writeRegister(0x96, 0x00000000, 0x00);  // COMMAND_CELL_INFO
        return true;
      }
    }
  }
  DEBUG_PRINTLN("Service or Characteristic not found or unable to subscribe.");
  pChr = nullptr;
  pClient->disconnect();
  return false;
}

void JKBMS::disconnect() {
  if (!inUse()) return;
  NimBLEClient *pClient = NimBLEDevice::getClientByPeerAddress(address());
  if (pClient) NimBLEDevice::deleteClient(pClient);  // Disconnects first if needed
  pChr = nullptr;
  connected = false;
  doConnect = false;
}

void JKBMS::handleNotification(uint8_t *pData, size_t length) {
  lastNotifyTime = millis();

  if (ignoreNotifyCount > 0) {
    ignoreNotifyCount--;
    return;
  }

  // Start of a new frame?
  if (length >= 4 && pData[0] == 0x55 && pData[1] == 0xAA && pData[2] == 0xEB && pData[3] == 0x90) {
    frame = 0;
    received_start = true;
    received_complete = false;
  } else if (!received_start) {
    return;  // Mid-frame data without a start, ignore
  }

  for (size_t i = 0; i < length; i++) {
    if (frame < (int)sizeof(receivedBytes)) receivedBytes[frame++] = pData[i];
    if (frame < JK_FRAME_SIZE) continue;

    // Frame complete
    received_complete = true;
    received_start = false;
    new_data = true;

    uint8_t expected = crc(receivedBytes, JK_FRAME_SIZE - 1);
    if (expected != receivedBytes[JK_FRAME_SIZE - 1]) {
      DEBUG_PRINTF("%s: CRC mismatch (0x%02X != 0x%02X), frame dropped\n", targetMAC.c_str(),
                   expected, receivedBytes[JK_FRAME_SIZE - 1]);
      return;
    }

    switch (receivedBytes[4]) {
      case 0x01:
        DEBUG_PRINTLN("BMS Settings frame detected.");
        // Keep a copy: the settings layout depends on the protocol which may
        // only be known once the first cell info frame has arrived.
        memcpy(settingsBytes, receivedBytes, JK_FRAME_SIZE);
        haveSettings = true;
        settingsParsed = false;
        if (protocol != JKProtocol::Unknown) parseSettings();
        break;
      case 0x02:
        DEBUG_PRINTLN("Cell data frame detected.");
        memcpy(lastCellFrame, receivedBytes, JK_FRAME_SIZE);
        parseData();
        break;
      case 0x03:
        DEBUG_PRINTLN("Device info frame detected.");
        parseDeviceInfo();
        break;
      default:
        DEBUG_PRINTF("Unknown frame type: 0x%02X\n", receivedBytes[4]);
        break;
    }
    return;  // Remaining bytes of this notification (if any) are padding
  }
}

static void dump_hex(Print &out, const char *title, const uint8_t *b, int len) {
  out.printf("%s:\n", title);
  for (int i = 0; i < len; i += 16) {
    out.printf("  %03d:", i);
    for (int j = i; j < i + 16 && j < len; j++) out.printf(" %02X", b[j]);
    out.println();
  }
}

void JKBMS::dumpFrames(Print &out) {
  out.printf("=== DUMP %s (%s) proto=%s model=%s hw=%s sw=%s ===\n", targetMAC.c_str(), name.c_str(),
             jk_protocol_name(protocol), vendorId.c_str(), hardwareVersion.c_str(), softwareVersion.c_str());
  if (haveSettings) dump_hex(out, "Settings frame (0x01)", settingsBytes, JK_FRAME_SIZE);
  else out.println("Settings frame (0x01): not received");
  if (hasData()) dump_hex(out, "Cell info frame (0x02)", lastCellFrame, JK_FRAME_SIZE);
  else out.println("Cell info frame (0x02): not received");
  out.println("=== END DUMP ===");
}

void JKBMS::writeRegister(uint8_t address, uint32_t value, uint8_t length) {
  DEBUG_PRINTF("Writing register: address=0x%02X, value=0x%08lX, length=%d\n", address, value, length);
  uint8_t frame[20] = { 0xAA, 0x55, 0x90, 0xEB, address, length };

  // Insert value (Little-Endian)
  frame[6] = value >> 0;  // LSB
  frame[7] = value >> 8;
  frame[8] = value >> 16;
  frame[9] = value >> 24;  // MSB

  // Calculate CRC
  frame[19] = crc(frame, 19);

  if (pChr) {
    pChr->writeValue((uint8_t *)frame, (size_t)sizeof(frame));
  }
}

// Settings frame (0x01). Layout is shared by JK02_24S and JK02_32S.
void JKBMS::bms_settings() {
  DEBUG_PRINTLN("Processing BMS settings...");
  const uint8_t *b = settingsBytes;
  settingsParsed = true;

  cell_voltage_undervoltage_protection = le_u32(b, 10) * 0.001f;
  cell_voltage_undervoltage_recovery = le_u32(b, 14) * 0.001f;
  cell_voltage_overvoltage_protection = le_u32(b, 18) * 0.001f;
  cell_voltage_overvoltage_recovery = le_u32(b, 22) * 0.001f;
  balance_trigger_voltage = le_u32(b, 26) * 0.001f;
  power_off_voltage = le_u32(b, 46) * 0.001f;
  max_charge_current = le_u32(b, 50) * 0.001f;
  charge_overcurrent_protection_delay = le_u32(b, 54);
  charge_overcurrent_protection_recovery_time = le_u32(b, 58);
  max_discharge_current = le_u32(b, 62) * 0.001f;
  discharge_overcurrent_protection_delay = le_u32(b, 66);
  discharge_overcurrent_protection_recovery_time = le_u32(b, 70);
  short_circuit_protection_recovery_time = le_u32(b, 74);
  max_balance_current = le_u32(b, 78) * 0.001f;
  charge_overtemperature_protection = le_u32(b, 82) * 0.1f;
  charge_overtemperature_protection_recovery = le_u32(b, 86) * 0.1f;
  discharge_overtemperature_protection = le_u32(b, 90) * 0.1f;
  discharge_overtemperature_protection_recovery = le_u32(b, 94) * 0.1f;
  charge_undertemperature_protection = le_u32(b, 98) * 0.1f;
  charge_undertemperature_protection_recovery = le_u32(b, 102) * 0.1f;
  power_tube_overtemperature_protection = le_u32(b, 106) * 0.1f;
  power_tube_overtemperature_protection_recovery = le_u32(b, 110) * 0.1f;
  uint32_t settingsCells = le_u32(b, 114);
  total_battery_capacity = le_u32(b, 130) * 0.001f;
  short_circuit_protection_delay = le_u32(b, 134);
  balance_starting_voltage = le_u32(b, 138) * 0.001f;

  // The cell info frame's enabled-cell mask is preferred, use this as fallback
  if (cell_count == 0 && settingsCells > 0 && settingsCells <= JK_MAX_CELLS) cell_count = settingsCells;

  DEBUG_PRINTF("Cell count (settings): %lu\n", (unsigned long)settingsCells);
  DEBUG_PRINTF("Cell UVP: %.3fV  OVP: %.3fV\n", cell_voltage_undervoltage_protection, cell_voltage_overvoltage_protection);
  DEBUG_PRINTF("Balance trigger: %.3fV  start: %.3fV  max current: %.2fA\n",
               balance_trigger_voltage, balance_starting_voltage, max_balance_current);
  DEBUG_PRINTF("Max charge: %.2fA  Max discharge: %.2fA  Capacity: %.2fAh\n",
               max_charge_current, max_discharge_current, total_battery_capacity);
}

void JKBMS::parseDeviceInfo() {
  new_data = false;
  if (frame < 134) {
    DEBUG_PRINTLN("Error: Not enough data received for device info.");
    return;
  }

  vendorId = frame_str(receivedBytes, 6, 16);
  hardwareVersion = frame_str(receivedBytes, 22, 8);
  softwareVersion = frame_str(receivedBytes, 30, 8);
  hwMajor = atoi(hardwareVersion.c_str());
  uint32_t uptime = le_u32(receivedBytes, 38);
  uint32_t powerOnCount = le_u32(receivedBytes, 42);
  std::string devName = frame_str(receivedBytes, 46, 16);
  std::string manufacturingDate = frame_str(receivedBytes, 78, 8);
  std::string serialNumber = frame_str(receivedBytes, 86, 11);

  DEBUG_PRINTF("Device info for %s:\n", targetMAC.c_str());
  DEBUG_PRINTF("  Vendor ID: %s\n", vendorId.c_str());
  DEBUG_PRINTF("  Hardware version: %s (major %d)\n", hardwareVersion.c_str(), hwMajor);
  DEBUG_PRINTF("  Software version: %s\n", softwareVersion.c_str());
  DEBUG_PRINTF("  Uptime: %lu s\n", (unsigned long)uptime);
  DEBUG_PRINTF("  Power on count: %lu\n", (unsigned long)powerOnCount);
  DEBUG_PRINTF("  Device name: %s\n", devName.c_str());
  DEBUG_PRINTF("  Manufacturing date: %s\n", manufacturingDate.c_str());
  DEBUG_PRINTF("  Serial number: %s\n", serialNumber.c_str());
}

// Check whether a cell info frame is self-consistent when decoded as JK02
// with the given offset (0 = 24 cell layout, 16 = 32 cell layout).
bool JKBMS::jk02LayoutPlausible(int offset) {
  const uint8_t *b = receivedBytes;
  uint32_t mask = le_u32(b, 54 + offset);
  if (mask == 0 || (mask & (mask + 1)) != 0) return false;  // Must be contiguous bits from bit 0
  int n = __builtin_popcount(mask);
  if (n > 24 + offset / 2) return false;

  uint32_t sum = 0;
  for (int i = 0; i < n; i++) {
    uint16_t mv = le_u16(b, 6 + i * 2);
    if (mv < 500 || mv > 5000) return false;
    sum += mv;
  }
  int avg = le_u16(b, 58 + offset);
  return abs(avg - (int)(sum / n)) <= 20;
}

void JKBMS::detectProtocol() {
  // JK04 sends IEEE754 floats. Cell 1 as float is ~2.5..4.5 V there, while a
  // JK02 frame (two uint16 mV values) decodes to a tiny denormal-ish number.
  float f = le_f32(receivedBytes, 6);
  if (f > 0.5f && f < 5.0f) {
    protocol = JKProtocol::JK04;
  } else {
    bool ok24 = jk02LayoutPlausible(0);
    bool ok32 = jk02LayoutPlausible(16);
    if (ok32 && !ok24) protocol = JKProtocol::JK02_32S;
    else if (ok24 && !ok32) protocol = JKProtocol::JK02_24S;
    else if (hwMajor > 0) protocol = hwMajor >= 11 ? JKProtocol::JK02_32S : JKProtocol::JK02_24S;
    else protocol = JKProtocol::JK02_32S;
  }
  DEBUG_PRINTF("%s: detected protocol %s (hw %s)\n", targetMAC.c_str(), jk_protocol_name(protocol),
               hardwareVersion.c_str());

  // Settings arrived before we knew the layout, decode them now
  if (haveSettings && !settingsParsed) parseSettings();
}

void JKBMS::parseData() {
  new_data = false;
  ignoreNotifyCount = BMS_NOTIFY_IGNORE_COUNT;

  if (protocol == JKProtocol::Unknown) detectProtocol();

  switch (protocol) {
    case JKProtocol::JK04: parseCellInfoJK04(); break;
    case JKProtocol::JK02_24S: parseCellInfoJK02(0); break;
    default: parseCellInfoJK02(16); break;
  }
  cellFrameCount++;
  logCellData();
}

// JK02 cell info frame. offset is 0 for the 24 cell layout and 16 for the 32
// cell layout; fields after the resistances are shifted by 2 * offset.
void JKBMS::parseCellInfoJK02(int offset) {
  const uint8_t *b = receivedBytes;
  const int maxCells = 24 + offset / 2;
  const int o2 = offset * 2;

  uint32_t mask = le_u32(b, 54 + offset);
  int maskCells = __builtin_popcount(mask);
  if (maskCells > 0 && maskCells <= maxCells) cell_count = maskCells;
  if (cell_count > maxCells) cell_count = maxCells;

  for (int i = 0; i < JK_MAX_CELLS; i++) {
    cellVoltage[i] = i < maxCells ? le_u16(b, 6 + i * 2) * 0.001f : 0;
    wireResist[i] = i < maxCells ? le_u16(b, 64 + offset + i * 2) * 0.001f : 0;
  }

  Average_Cell_Voltage = le_u16(b, 58 + offset) * 0.001f;
  Delta_Cell_Voltage = le_u16(b, 60 + offset) * 0.001f;

  MOS_Temp = le_i16(b, 112 + o2) * 0.1f;
  Battery_Voltage = le_u32(b, 118 + o2) * 0.001f;
  Charge_Current = le_i32(b, 126 + o2) * 0.001f;
  Battery_Power = Battery_Voltage * Charge_Current;
  Battery_T1 = le_i16(b, 130 + o2) * 0.1f;
  Battery_T2 = le_i16(b, 132 + o2) * 0.1f;
  Balance_Curr = le_i16(b, 138 + o2) * 0.001f;
  Balancing_Action = b[140 + o2];
  Percent_Remain = b[141 + o2];
  Capacity_Remain = le_u32(b, 142 + o2) * 0.001f;
  Nominal_Capacity = le_u32(b, 146 + o2) * 0.001f;
  Cycle_Count = le_u32(b, 150 + o2);
  Cycle_Capacity = le_u32(b, 154 + o2) * 0.001f;

  Uptime = le_u32(b, 162 + o2);
  uint32_t t = Uptime;
  sec = t % 60; t /= 60;
  mi = t % 60;  t /= 60;
  hr = t % 24;
  days = t / 24;

  Charge = b[166 + o2] > 0;
  Discharge = b[167 + o2] > 0;
  Balance = b[169 + o2] > 0;
}

// Plausible float or fallback (rejects NaN / garbage)
static float sane_f32(const uint8_t *b, int i, float lo, float hi, float fallback) {
  float f = le_f32(b, i);
  return (f >= lo && f <= hi) ? f : fallback;
}

// JK04 cell info frame (older JK balancers, e.g. JK-B1A16S hw 8.x).
// Offsets verified against the JK app on a JK-B1A16S (hw 8.X-W, sw 8.05):
//   6 + 4n  cell voltage n (float, V)       102 + 4n  wire resistance n (float, Ohm)
//   202     average cell voltage (float)    206       delta cell voltage (float)
//   218     highest cell (0-based)          219       lowest cell (0-based)
//   242     uptime (uint32, 0.1 s)          286       uptime (uint24, s)
// Not yet verified while balancing:
//   210     balance current (float, A)      220       balancing action
void JKBMS::parseCellInfoJK04() {
  const uint8_t *b = receivedBytes;
  const int maxCells = 24;

  int n = 0;
  float sum = 0, high = 0, low = 1000;
  for (int i = 0; i < JK_MAX_CELLS; i++) {
    float v = i < maxCells ? le_f32(b, 6 + i * 4) : 0;
    float r = i < maxCells ? le_f32(b, 102 + i * 4) : 0;
    if (!(v > 0.5f && v < 5.0f)) v = 0;   // Also rejects NaN
    if (!(r >= 0.0f && r < 10.0f)) r = 0;
    cellVoltage[i] = v;
    wireResist[i] = r;
    if (v > 0) {
      n = i + 1;
      sum += v;
      high = max(high, v);
      low = min(low, v);
    }
  }

  cell_count = n;
  Battery_Voltage = sum;
  // Prefer the balancer's own figures, fall back to locally computed ones
  Average_Cell_Voltage = sane_f32(b, 202, 0.5f, 5.0f, n ? sum / n : 0);
  Delta_Cell_Voltage = sane_f32(b, 206, 0.0f, 5.0f, n ? high - low : 0);
  Balance_Curr = sane_f32(b, 210, -10.0f, 10.0f, 0);
  Balancing_Action = b[220];
  Charge_Current = 0;
  Battery_Power = 0;
  Percent_Remain = 0;

  Uptime = b[286] | (b[287] << 8) | (b[288] << 16);
  uint32_t t = Uptime;
  sec = t % 60; t /= 60;
  mi = t % 60;  t /= 60;
  hr = t % 24;
  days = t / 24;
}

// JK04 settings frame. Verified against the JK app on a JK-B1A16S:
//   34 cell count (uint8)       38 power off voltage (float)
//   106 trigger delta (float)   114 balancing switch (uint8)
// Bytes 6 and 110 are both 1.0 here (start balance voltage and max balance
// current in the app). The order 106/110/114 matches the RS485/CAN protocol
// docs (trigger delta, max balance current, switch), so 110 = max current.
void JKBMS::parseSettingsJK04() {
  const uint8_t *b = settingsBytes;
  settingsParsed = true;

  balance_starting_voltage = sane_f32(b, 6, 0.0f, 5.0f, 0);
  power_off_voltage = sane_f32(b, 38, 0.0f, 5.0f, 0);
  balance_trigger_voltage = sane_f32(b, 106, 0.0f, 5.0f, 0);
  max_balance_current = sane_f32(b, 110, 0.0f, 20.0f, 0);
  Balance = b[114] != 0;
  if (cell_count == 0 && b[34] > 0 && b[34] <= JK_MAX_CELLS) cell_count = b[34];

  DEBUG_PRINTF("JK04 settings: cells %d  trigger delta %.3fV  start %.2fV  max balance %.3fA  power off %.3fV  balancing %s\n",
               b[34], balance_trigger_voltage, balance_starting_voltage, max_balance_current, power_off_voltage,
               Balance ? "on" : "off");
}

void JKBMS::parseSettings() {
  if (protocol == JKProtocol::JK04) parseSettingsJK04();
  else bms_settings();
}

void JKBMS::logCellData() {
  DEBUG_PRINTF("\n--- Data from %s (%s, %d cells) ---\n", targetMAC.c_str(), jk_protocol_name(protocol), cell_count);
  for (int j = 0; j < cell_count; j++) {
    DEBUG_PRINTF("  Cell %02d: %.3f V  %.3f Ohm\n", j + 1, cellVoltage[j], wireResist[j]);
  }
  DEBUG_PRINTF("Avg: %.3fV  Delta: %.3fV  Balance: %.3fA (%d)\n", Average_Cell_Voltage, Delta_Cell_Voltage,
               Balance_Curr, Balancing_Action);
  DEBUG_PRINTF("Battery: %.2fV  %.3fA  %.1fW  SOC %d%%\n", Battery_Voltage, Charge_Current, Battery_Power, Percent_Remain);
  DEBUG_PRINTF("Capacity: %.2f/%.2fAh  Cycles: %.0f\n", Capacity_Remain, Nominal_Capacity, Cycle_Count);
  DEBUG_PRINTF("Temps T1 %.1fC T2 %.1fC MOS %.1fC  Uptime %dd %dh %dm\n", Battery_T1, Battery_T2, MOS_Temp, days, hr, mi);
  DEBUG_PRINTF("Charge: %d  Discharge: %d  Balance: %d\n", Charge, Discharge, Balance);
}

//********************************************
// BLE callbacks
//********************************************

void ClientCallbacks::onConnect(NimBLEClient *pClient) {
  DEBUG_PRINTF("Connected to %s\n", mac.c_str());
  int slot = bms_find_slot(mac);
  if (slot >= 0) jkBmsDevices[slot].connected = true;
}

void ClientCallbacks::onDisconnect(NimBLEClient *pClient, int reason) {
  DEBUG_PRINTF("%s disconnected, reason: %d\n", mac.c_str(), reason);
  int slot = bms_find_slot(mac);
  if (slot >= 0) {
    jkBmsDevices[slot].connected = false;
    jkBmsDevices[slot].doConnect = false;
    jkBmsDevices[slot].pChr = nullptr;
    jkBmsDevices[slot].received_start = false;
  }
}

void notifyCB(NimBLERemoteCharacteristic *pChr, uint8_t *pData, size_t length, bool isNotify) {
  for (int i = 0; i < bmsDeviceCount; i++) {
    if (jkBmsDevices[i].pChr == pChr) {
      jkBmsDevices[i].handleNotification(pData, length);
      break;
    }
  }
}

// Heuristics for spotting JK BMS / balancers in advertisements
static bool looksLikeJK(const NimBLEAdvertisedDevice *dev) {
  // JK devices put their own MAC address in the last 6 bytes of the
  // manufacturer data, e.g. 650b88a0 c84780234f95 for c8:47:80:23:4f:95
  if (dev->haveManufacturerData()) {
    std::string m = dev->getManufacturerData();
    if (m.size() >= 6) {
      const uint8_t *p = reinterpret_cast<const uint8_t *>(m.data()) + m.size() - 6;
      char buf[18];
      snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", p[0], p[1], p[2], p[3], p[4], p[5]);
      if (dev->getAddress().toString() == buf) return true;
    }
  }
  std::string name = dev->getName();
  if (name.rfind("JK", 0) == 0) return true;
  // JK devices use the FFE0 serial service
  return dev->isAdvertisingService(NimBLEUUID((uint16_t)0xFFE0));
}

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    std::string mac = dev->getAddress().toString();

    // Saved device that isn't connected? Queue a connection.
    int slot = bms_find_slot(mac);
    if (slot >= 0 && !jkBmsDevices[slot].connected && !jkBmsDevices[slot].doConnect) {
      DEBUG_PRINTF("Found saved device: %s\n", mac.c_str());
      jkBmsDevices[slot].addrType = dev->getAddress().getType();
      jkBmsDevices[slot].doConnect = true;
    }

    if (slot < 0 && !looksLikeJK(dev)) return;

    std::string name = dev->getName();
    std::lock_guard<std::mutex> lock(foundMutex);
    auto it = std::find_if(foundDevices.begin(), foundDevices.end(),
                           [&](const FoundDevice &f) { return f.mac == mac; });
    if (it == foundDevices.end()) {
      if (foundDevices.size() >= MAX_FOUND_DEVICES) return;
      FoundDevice f;
      f.mac = mac;
      f.name = name;
      f.addrType = dev->getAddress().getType();
      f.rssi = dev->getRSSI();
      f.lastSeen = millis();
      foundDevices.push_back(f);
      foundVersion++;
      DEBUG_PRINTF("Found JK device: %s (%s) RSSI %d\n", name.c_str(), mac.c_str(), f.rssi);
    } else {
      if (!name.empty() && name != it->name) {
        it->name = name;
        foundVersion++;
      }
      it->rssi = dev->getRSSI();
      it->lastSeen = millis();
    }
  }

  void onScanEnd(const NimBLEScanResults &results, int reason) override {
    DEBUG_PRINTF("Scan Ended; reason = %d\n", reason);
  }
} scanCallbacks;

//********************************************
// Device management
//********************************************

static void prefKey(char *buf, size_t len, const char *prefix, int slot) {
  snprintf(buf, len, "%s%d", prefix, slot);
}

static void saveSlot(int i) {
  char k[12];
  const JKBMS &d = jkBmsDevices[i];
  if (d.inUse()) {
    prefKey(k, sizeof(k), "mac", i);  prefs.putString(k, d.targetMAC.c_str());
    prefKey(k, sizeof(k), "name", i); prefs.putString(k, d.name.c_str());
    prefKey(k, sizeof(k), "at", i);   prefs.putUChar(k, d.addrType);
  } else {
    prefKey(k, sizeof(k), "mac", i);  prefs.remove(k);
    prefKey(k, sizeof(k), "name", i); prefs.remove(k);
    prefKey(k, sizeof(k), "at", i);   prefs.remove(k);
  }
}

static void loadSlots() {
  char k[12];
  for (int i = 0; i < bmsDeviceCount; i++) {
    prefKey(k, sizeof(k), "mac", i);
    if (!prefs.isKey(k)) continue;
    String mac = prefs.getString(k, "");
    if (mac.isEmpty()) continue;
    prefKey(k, sizeof(k), "name", i);
    String name = prefs.getString(k, "");
    prefKey(k, sizeof(k), "at", i);
    uint8_t at = prefs.getUChar(k, BLE_ADDR_PUBLIC);
    jkBmsDevices[i].assign(mac.c_str(), at, name.c_str());
    DEBUG_PRINTF("Loaded saved device %d: %s (%s)\n", i, name.c_str(), mac.c_str());
  }
}

static void startScan(uint32_t durationMs) {
  if (!pScan || pScan->isScanning()) return;
  pScan->start(durationMs, false, true);
  lastScanTime = millis();
}

int bms_find_slot(const std::string &mac) {
  if (mac.empty()) return -1;
  for (int i = 0; i < bmsDeviceCount; i++) {
    if (jkBmsDevices[i].targetMAC == mac) return i;
  }
  return -1;
}

int bms_saved_count() {
  int n = 0;
  for (int i = 0; i < bmsDeviceCount; i++) n += jkBmsDevices[i].inUse();
  return n;
}

int bms_add_device(const std::string &mac, uint8_t addrType, const std::string &name) {
  int slot = bms_find_slot(mac);
  if (slot >= 0) return slot;
  for (int i = 0; i < bmsDeviceCount; i++) {
    if (jkBmsDevices[i].inUse()) continue;
    jkBmsDevices[i].assign(mac, addrType, name);
    jkBmsDevices[i].doConnect = true;  // Was just seen in a scan, try right away
    saveSlot(i);
    DEBUG_PRINTF("Saved device %d: %s (%s)\n", i, name.c_str(), mac.c_str());
    return i;
  }
  DEBUG_PRINTLN("No free device slot!");
  return -1;
}

bool bms_forget_device(const std::string &mac) {
  int slot = bms_find_slot(mac);
  if (slot < 0) return false;
  jkBmsDevices[slot].disconnect();
  jkBmsDevices[slot].clear();
  saveSlot(slot);
  DEBUG_PRINTF("Forgot device %d: %s\n", slot, mac.c_str());
  return true;
}

void bms_start_user_scan() {
  {
    std::lock_guard<std::mutex> lock(foundMutex);
    foundDevices.clear();
    foundVersion++;
  }
  DEBUG_PRINTLN("Starting user scan...");
  userScanUntil = millis() + BLE_USER_SCAN_TIME;
  if (pScan && pScan->isScanning()) pScan->stop();
  startScan(BLE_USER_SCAN_TIME);
}

bool bms_is_scanning() {
  return (int32_t)(userScanUntil - millis()) > 0 || (pScan && pScan->isScanning());
}

std::vector<FoundDevice> bms_get_found_devices() {
  std::lock_guard<std::mutex> lock(foundMutex);
  return foundDevices;
}

uint32_t bms_found_devices_version() {
  return foundVersion;
}

void bms_init() {
  DEBUG_PRINTLN("Initializing NimBLE Client...");
  loadSlots();

  NimBLEDevice::init("MultiJKBMS-Client");
  NimBLEDevice::setPower(3);

  pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(&scanCallbacks);
  pScan->setActiveScan(true);
  pScan->setInterval(BLE_SCAN_INTERVAL);
  pScan->setWindow(BLE_SCAN_WINDOW);

  // Only scan on boot if there is something to reconnect to. Otherwise the
  // device list screen starts a scan when it is opened.
  if (bms_saved_count() > 0) startScan(BLE_SCAN_TIME);
}

void bms_loop() {
  // Connect to devices that were seen during scanning (or just added)
  bool pending = false;
  for (int i = 0; i < bmsDeviceCount; i++) {
    if (jkBmsDevices[i].inUse() && jkBmsDevices[i].doConnect && !jkBmsDevices[i].connected) pending = true;
  }
  if (pending && pScan->isScanning()) pScan->stop();

  int connectedCount = 0;
  for (int i = 0; i < bmsDeviceCount; i++) {
    JKBMS &d = jkBmsDevices[i];
    if (!d.inUse()) continue;

    if (d.doConnect && !d.connected) {
      DEBUG_PRINTF("Attempting to connect to: %d (%s)...\n", i, d.targetMAC.c_str());
      if (d.connectToServer()) {
        DEBUG_PRINTF("%s connected successfully\n", d.targetMAC.c_str());
      } else {
        DEBUG_PRINTF("%s connection failed\n", d.targetMAC.c_str());
      }
      d.doConnect = false;
    }

    if (d.connected) {
      connectedCount++;
      if (millis() - d.lastNotifyTime > BMS_CONNECTION_TIMEOUT) {
        DEBUG_PRINTF("%s connection timeout\n", d.targetMAC.c_str());
        NimBLEClient *pClient = NimBLEDevice::getClientByPeerAddress(d.address());
        if (pClient) pClient->disconnect();
        d.lastNotifyTime = millis();  // Don't spam disconnect while it's in progress
      }
    }
  }

  if (!pScan->isScanning()) {
    int32_t userRemaining = (int32_t)(userScanUntil - millis());
    if (userRemaining > 200) {
      // Resume a user scan that was interrupted by a connection attempt
      startScan(userRemaining);
    } else if (connectedCount < bms_saved_count() && millis() - lastScanTime >= BLE_SCAN_PERIOD) {
      // Look for saved devices that aren't connected
      startScan(BLE_SCAN_TIME);
    }
  }
}
