#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string>
#include <vector>

// Maximum number of cells any supported JK frame layout can carry
#define JK_MAX_CELLS 32
// Size of a complete JK frame (header ... CRC)
#define JK_FRAME_SIZE 300

// JK devices speak several variants of the same BLE protocol.
// The variant is auto-detected from the first cell info frame (and the
// hardware version reported in the device info frame).
enum class JKProtocol : uint8_t {
  Unknown,
  JK02_24S,  // Older BMS (hw < 11) and JK active balancers: 24 cell slots
  JK02_32S,  // Newer BMS (hw >= 11), e.g. JK-B1A8S10P, JK-PB series: 32 cell slots
  JK04,      // Old JK-B2A16S style balancers: values are IEEE754 floats
};

const char *jk_protocol_name(JKProtocol p);

class JKBMS {
public:
  JKBMS() = default;

  // Assign this slot to a device and reset all data
  void assign(const std::string &mac, uint8_t addrType, const std::string &name);
  // Free this slot (does not touch BLE connections)
  void clear();
  bool inUse() const { return !targetMAC.empty(); }
  NimBLEAddress address() const { return NimBLEAddress(targetMAC, addrType); }

  // Device identity
  std::string targetMAC;
  uint8_t addrType = BLE_ADDR_PUBLIC;
  std::string name;  // Advertised name (what the user picked from the list)

  // BLE state
  NimBLERemoteCharacteristic *pChr = nullptr;
  volatile bool doConnect = false;
  volatile bool connected = false;
  volatile uint32_t lastNotifyTime = 0;

  // Detected protocol / device info
  JKProtocol protocol = JKProtocol::Unknown;
  std::string vendorId;         // Model, e.g. "JK_B1A8S10P"
  std::string hardwareVersion;  // e.g. "11.XW"
  std::string softwareVersion;
  int hwMajor = 0;

  // Data Processing
  byte receivedBytes[320];
  byte settingsBytes[JK_FRAME_SIZE];
  bool haveSettings = false;
  bool settingsParsed = false;
  int frame = 0;
  bool received_start = false;
  bool received_complete = false;
  bool new_data = false;
  int ignoreNotifyCount = 0;
  uint32_t cellFrameCount = 0;

  // BMS Data Fields
  float cellVoltage[JK_MAX_CELLS] = { 0 };
  float wireResist[JK_MAX_CELLS] = { 0 };
  float Average_Cell_Voltage = 0;
  float Delta_Cell_Voltage = 0;
  float Battery_Voltage = 0;
  float Battery_Power = 0;
  float Charge_Current = 0;
  float Battery_T1 = 0;
  float Battery_T2 = 0;
  float MOS_Temp = 0;
  int Percent_Remain = 0;
  float Capacity_Remain = 0;
  float Nominal_Capacity = 0;
  float Cycle_Count = 0;
  float Cycle_Capacity = 0;
  uint32_t Uptime = 0;
  uint8_t sec = 0, mi = 0, hr = 0, days = 0;
  float Balance_Curr = 0;
  bool Balance = false;
  bool Charge = false;
  bool Discharge = false;
  int Balancing_Action = 0;

  float balance_trigger_voltage = 0;
  float cell_voltage_undervoltage_protection = 0;
  float cell_voltage_undervoltage_recovery = 0;
  float cell_voltage_overvoltage_protection = 0;
  float cell_voltage_overvoltage_recovery = 0;
  float power_off_voltage = 0;
  float max_charge_current = 0;
  float charge_overcurrent_protection_delay = 0;
  float charge_overcurrent_protection_recovery_time = 0;
  float max_discharge_current = 0;
  float discharge_overcurrent_protection_delay = 0;
  float discharge_overcurrent_protection_recovery_time = 0;
  float short_circuit_protection_recovery_time = 0;
  float max_balance_current = 0;
  float charge_overtemperature_protection = 0;
  float charge_overtemperature_protection_recovery = 0;
  float discharge_overtemperature_protection = 0;
  float discharge_overtemperature_protection_recovery = 0;
  float charge_undertemperature_protection = 0;
  float charge_undertemperature_protection_recovery = 0;
  float power_tube_overtemperature_protection = 0;
  float power_tube_overtemperature_protection_recovery = 0;
  int cell_count = 0;
  float total_battery_capacity = 0;
  float short_circuit_protection_delay = 0;
  float balance_starting_voltage = 0;

  // True once at least one cell info frame has been decoded
  bool hasData() const { return cellFrameCount > 0; }
  // Balancers (and BMS without a current sensor) report no SOC/capacity
  bool hasSoc() const { return protocol != JKProtocol::JK04 && Nominal_Capacity > 0; }

  // Methods
  bool connectToServer();
  void disconnect();
  void parseDeviceInfo();
  void parseData();
  void bms_settings();
  void writeRegister(uint8_t address, uint32_t value, uint8_t length);
  void handleNotification(uint8_t *pData, size_t length);

private:
  uint8_t crc(const uint8_t data[], uint16_t len);
  void detectProtocol();
  bool jk02LayoutPlausible(int offset);
  void parseCellInfoJK02(int offset);
  void parseCellInfoJK04();
  void logCellData();
};

// BLE Callbacks. Looks the slot up by MAC on every event so a slot that was
// forgotten/reassigned while a disconnect was pending isn't corrupted.
class ClientCallbacks : public NimBLEClientCallbacks {
  std::string mac;
public:
  explicit ClientCallbacks(const std::string &mac) : mac(mac) {}
  void onConnect(NimBLEClient *pClient) override;
  void onDisconnect(NimBLEClient *pClient, int reason) override;
};

// Global callback function for notifications
void notifyCB(NimBLERemoteCharacteristic *pChr, uint8_t *pData, size_t length, bool isNotify);

//********************************************
// Device management
//********************************************

// A JK device seen during scanning
struct FoundDevice {
  std::string mac;
  std::string name;
  uint8_t addrType = BLE_ADDR_PUBLIC;
  int rssi = 0;
  uint32_t lastSeen = 0;
};

// Init NimBLE, load saved devices from Preferences and start scanning for them
void bms_init();
// Connection management, call from loop()
void bms_loop();

// Save a device to a free slot and connect to it. Returns slot index or -1 if full.
int bms_add_device(const std::string &mac, uint8_t addrType, const std::string &name);
// Disconnect, remove from slot and Preferences. Returns false if not saved.
bool bms_forget_device(const std::string &mac);
// Slot index for a MAC address or -1
int bms_find_slot(const std::string &mac);
int bms_saved_count();

// Start a scan requested by the user (clears the found device list)
void bms_start_user_scan();
bool bms_is_scanning();
// Thread-safe copy of the JK devices found by scanning
std::vector<FoundDevice> bms_get_found_devices();
// Incremented whenever the found list gains/loses a device
uint32_t bms_found_devices_version();

// Legacy name kept for screens.cpp
inline void scanForDevices() { bms_start_user_scan(); }

// Device slots - defined in jkbms.cpp
extern JKBMS jkBmsDevices[];
extern const int bmsDeviceCount;
