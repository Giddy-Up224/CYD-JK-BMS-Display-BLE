#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <LVGL_CYD.h>


/* Takes position of USB connector relative to screen. These are synonyms
   as defined in LVGL_CYD.h:

      #define USB_DOWN  LV_DISPLAY_ROTATION_0
      #define USB_RIGHT LV_DISPLAY_ROTATION_90
      #define USB_UP    LV_DISPLAY_ROTATION_180
      #define USB_LEFT  LV_DISPLAY_ROTATION_270
*/ 
#define SCREEN_ORIENTATION USB_LEFT

// Enable or disable debugging output
#define DEBUG_ENABLED true

// Debugging macro
#if DEBUG_ENABLED
#define DEBUG_PRINT(...) Serial.print(__VA_ARGS__)
#define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#define DEBUG_PRINTLN(...)
#define DEBUG_PRINTF(...)
#endif

void handleFileList();
void sendResponce();
void handleFileUpload();
void handleFileDelete();
void handleFileView();
void handleFormat();
String getLastModified(File file);


// WiFi credentials
const char* ssid = "your-ssid";
const char* password = "your-password";
WebServer server(80);  // Webserver auf Port 80

uint32_t minFreeHeap = UINT32_MAX;
unsigned long lastHeapUpdate = 0;

unsigned long lastMillis = 0;
uint32_t totalSeconds = 0;

void calculateUptime() {
  unsigned long currentMillis = millis();
  unsigned long elapsedMillis = currentMillis - lastMillis;

  // Überlauf von millis() behandeln
  if (currentMillis < lastMillis) {
    // Ein Überlauf ist aufgetreten
    elapsedMillis = UINT32_MAX - lastMillis + currentMillis;
  }

  
 if (elapsedMillis >= 1000) {
    // Vergangene Sekunden addieren
    totalSeconds += elapsedMillis / 1000;

    // Aktuellen millis()-Wert speichern
    lastMillis = currentMillis;
  }
}

void monitorFreeHeap() {
  uint32_t currentFreeHeap = ESP.getFreeHeap();

  if (currentFreeHeap < minFreeHeap) {
    minFreeHeap = currentFreeHeap;
  }

  if (millis() - lastHeapUpdate >= 1000) {
    minFreeHeap = UINT32_MAX;
    lastHeapUpdate = millis();
  }
}

void setupLittleFS() {
  if (!LittleFS.begin()) {
    DEBUG_PRINTLN("LittleFS Mount Failed");
    return;
  }
  DEBUG_PRINTLN("LittleFS Mounted Successfully");
}

// lesbare Anzeige der Speichergrößen
void formatBytes(size_t bytes, char* buffer, size_t bufferSize) {
  if (bytes < 1024) {
    snprintf_P(buffer, bufferSize, PSTR("%zu%s"), bytes, PSTR(" Byte"));
  } else if (bytes < 1048576) {
    dtostrf(static_cast<float>(bytes) / 1024.0, 6, 2, buffer);
    strcat_P(buffer, PSTR(" KB"));
  } else {
    dtostrf(static_cast<float>(bytes) / 1048576.0, 6, 2, buffer);
    strcat_P(buffer, PSTR(" MB"));
  }
}


void getCoreVersion(char* version) {
  // Schreibe die Version in den char-Array
  sprintf(version, "%d.%d.%d", ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
}

void getSketchName(char* sketchName) {
  const char* filename = __FILE__;
  int slashIndex = 0;
  for (int i = 0; i < strlen(filename); i++) {
    char c = filename[i];
    if (c == '\\' || c == '/') slashIndex = i + 1;
  }
  int nameLength = 0;
  for (int i = slashIndex; i < strlen(filename); i++) {
    char c = filename[i];
    if (c == '.') break;
    sketchName[nameLength++] = c;
  }
  sketchName[nameLength] = '\0';
}

String getSketchInfo() {
  char coreVersion[20];
  getCoreVersion(coreVersion);

  char sketchName[50];
  getSketchName(sketchName);

  // Compilation date and time
  String compileDate = __DATE__;
  String compileTime = __TIME__;

  // Create JSON object
  DynamicJsonDocument doc(200);
  doc["sketch_name"] = sketchName;
  doc["compile_date"] = compileDate;
  doc["compile_time"] = compileTime;
  doc["esp_core_version"] = coreVersion;

  String jsonResponse;
  serializeJson(doc, jsonResponse);
  return jsonResponse;
}

String formatUptime(uint32_t totalSeconds) {
  uint32_t days = totalSeconds / 86400;
  uint32_t hours = (totalSeconds % 86400) / 3600;
  uint32_t minutes = (totalSeconds % 3600) / 60;
  uint32_t seconds = totalSeconds % 60;

  char buffer[50];
  snprintf(buffer, sizeof(buffer), "%lu days, %02lu:%02lu:%02lu", days, hours, minutes, seconds);
  return String(buffer);
}

//********************************************
// JKBMS Class Definition
//********************************************
class JKBMS {
public:
  JKBMS(const std::string& mac)
    : targetMAC(mac) {}

  // BLE Components
  NimBLERemoteCharacteristic* pChr = nullptr;
  const NimBLEAdvertisedDevice* advDevice = nullptr;
  bool doConnect = false;
  bool connected = false;
  uint32_t lastNotifyTime = 0;
  std::string targetMAC;

  // Data Processing
  byte receivedBytes[320];
  int frame = 0;
  bool received_start = false;
  bool received_complete = false;
  bool new_data = false;
  int ignoreNotifyCount = 0;

  // BMS Data Fields
  float cellVoltage[16] = { 0 };
  float wireResist[16] = { 0 };
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
  uint32_t Uptime;
  uint8_t sec, mi, hr, days;
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




  // Methods
  bool connectToServer();
  void parseDeviceInfo();
  void parseData();
  void bms_settings();
  void writeRegister(uint8_t address, uint32_t value, uint8_t length);
  void handleNotification(uint8_t* pData, size_t length);

private:
  uint8_t crc(const uint8_t data[], uint16_t len) {
    uint8_t crc = 0;
    for (uint16_t i = 0; i < len; i++) crc += data[i];
    return crc;
  }
};

//********************************************
// Global Variables and Callbacks
//********************************************
JKBMS jkBmsDevices[] = {
  // TODO: Change to my MAC
  JKBMS("c8:47:80:23:4f:95"),  // Only one device configured
  // JKBMS("20:aa:08:25:26:8b"),    // Add more devices here
  // JKBMS("MAC_ADDRESS_3")
};

const int bmsDeviceCount = sizeof(jkBmsDevices) / sizeof(jkBmsDevices[0]);  // Dynamic device count

NimBLEScan* pScan;
unsigned long lastScanTime = 0;

class ClientCallbacks : public NimBLEClientCallbacks {
  JKBMS* bms;
public:
  ClientCallbacks(JKBMS* bmsInstance)
    : bms(bmsInstance) {}

  void onConnect(NimBLEClient* pClient) {
    DEBUG_PRINTF("Connected to %s\n", bms->targetMAC.c_str());
    bms->connected = true;
  }

  void onDisconnect(NimBLEClient* pClient, int reason) {
    DEBUG_PRINTF("%s disconnected, reason: %d\n", bms->targetMAC.c_str(), reason);
    bms->connected = false;
    bms->doConnect = false;
  }
};

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) {
    DEBUG_PRINTF("BLE Device found: %s\n", advertisedDevice->toString().c_str());
    for (int i = 0; i < bmsDeviceCount; i++) {
      if (jkBmsDevices[i].targetMAC.empty()) continue;  // Skip empty MAC addresses
      if (advertisedDevice->getAddress().toString() == jkBmsDevices[i].targetMAC && !jkBmsDevices[i].connected && !jkBmsDevices[i].doConnect) {
        DEBUG_PRINTF("Found target device: %s\n", jkBmsDevices[i].targetMAC.c_str());
        jkBmsDevices[i].advDevice = advertisedDevice;
        jkBmsDevices[i].doConnect = true;
        NimBLEDevice::getScan()->stop();
      }
    }
  }
} scanCallbacks;

void notifyCB(NimBLERemoteCharacteristic* pChr, uint8_t* pData, size_t length, bool isNotify) {
  DEBUG_PRINTLN("Notification received...");
  for (int i = 0; i < bmsDeviceCount; i++) {
    if (jkBmsDevices[i].pChr == pChr) {
      jkBmsDevices[i].handleNotification(pData, length);
      break;
    }
  }
}

//********************************************
// JKBMS Method Implementation
//********************************************
bool JKBMS::connectToServer() {
  DEBUG_PRINTF("Attempting to connect to %s...\n", targetMAC.c_str());
  NimBLEClient* pClient = NimBLEDevice::getClientByPeerAddress(advDevice->getAddress());

  if (!pClient) {
    pClient = NimBLEDevice::createClient();
    DEBUG_PRINTLN("New client created.");
    pClient->setClientCallbacks(new ClientCallbacks(this), true);
    pClient->setConnectionParams(12, 12, 0, 150);
    pClient->setConnectTimeout(5000);
  }

  if (!pClient->connect(advDevice)) {
    DEBUG_PRINTF("Failed to connect to %s\n", targetMAC.c_str());
    return false;
  }

  DEBUG_PRINTF("Connected to: %s RSSI: %d\n", pClient->getPeerAddress().toString().c_str(), pClient->getRssi());

  NimBLERemoteService* pSvc = pClient->getService("ffe0");
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
  return false;
}

void JKBMS::handleNotification(uint8_t* pData, size_t length) {
  DEBUG_PRINTLN("Handling notification...");
  lastNotifyTime = millis();

  if (ignoreNotifyCount > 0) {
    ignoreNotifyCount--;
    DEBUG_PRINTF("Ignoring notification. Remaining: %d\n", ignoreNotifyCount);
    return;
  }

  // Check for start of data frame
  if (pData[0] == 0x55 && pData[1] == 0xAA && pData[2] == 0xEB && pData[3] == 0x90) {
    DEBUG_PRINTLN("Start of data frame detected.");
    frame = 0;
    received_start = true;
    received_complete = false;

    // Store the received data
    for (int i = 0; i < length; i++) {
      receivedBytes[frame++] = pData[i];
    }
  } else if (received_start && !received_complete) {
    DEBUG_PRINTLN("Continuing data frame...");
    for (int i = 0; i < length; i++) {
      receivedBytes[frame++] = pData[i];
      if (frame >= 300) {
        received_complete = true;
        received_start = false;
        new_data = true;
        DEBUG_PRINTLN("New data available for parsing.");

        // Determine the type of data frame based on pData[4]
        switch (receivedBytes[4]) {  // Use receivedBytes[4] instead of pData[4]
          case 0x01:
            DEBUG_PRINTLN("BMS Settings frame detected.");
            bms_settings();
            break;
          case 0x02:
            DEBUG_PRINTLN("Cell data frame detected.");
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

        break;  // Exit the loop after processing the complete frame
      }
    }
  }
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

  // Debug: Print the entire frame in hexadecimal format
  DEBUG_PRINTF("Frame to be sent: ");
  for (int i = 0; i < sizeof(frame); i++) {
    DEBUG_PRINTF("%02X ", frame[i]);
  }
  DEBUG_PRINTF("\n");

  if (pChr) {
    pChr->writeValue((uint8_t*)frame, (size_t)sizeof(frame));
  }
}

void JKBMS::bms_settings() {
  DEBUG_PRINTLN("Processing BMS settings...");
  cell_voltage_undervoltage_protection = ((receivedBytes[13] << 24 | receivedBytes[12] << 16 | receivedBytes[11] << 8 | receivedBytes[10]) * 0.001);
  cell_voltage_undervoltage_recovery = ((receivedBytes[17] << 24 | receivedBytes[16] << 16 | receivedBytes[15] << 8 | receivedBytes[14]) * 0.001);
  cell_voltage_overvoltage_protection = ((receivedBytes[21] << 24 | receivedBytes[20] << 16 | receivedBytes[19] << 8 | receivedBytes[18]) * 0.001);
  cell_voltage_overvoltage_recovery = ((receivedBytes[25] << 24 | receivedBytes[24] << 16 | receivedBytes[23] << 8 | receivedBytes[22]) * 0.001);
  balance_trigger_voltage = ((receivedBytes[29] << 24 | receivedBytes[28] << 16 | receivedBytes[27] << 8 | receivedBytes[26]) * 0.001);
  power_off_voltage = ((receivedBytes[49] << 24 | receivedBytes[48] << 16 | receivedBytes[47] << 8 | receivedBytes[46]) * 0.001);
  max_charge_current = ((receivedBytes[53] << 24 | receivedBytes[52] << 16 | receivedBytes[51] << 8 | receivedBytes[50]) * 0.001);
  charge_overcurrent_protection_delay = ((receivedBytes[57] << 24 | receivedBytes[56] << 16 | receivedBytes[55] << 8 | receivedBytes[54]));
  charge_overcurrent_protection_recovery_time = ((receivedBytes[61] << 24 | receivedBytes[60] << 16 | receivedBytes[59] << 8 | receivedBytes[58]));
  max_discharge_current = ((receivedBytes[65] << 24 | receivedBytes[64] << 16 | receivedBytes[63] << 8 | receivedBytes[62]) * 0.001);
  discharge_overcurrent_protection_delay = ((receivedBytes[69] << 24 | receivedBytes[68] << 16 | receivedBytes[67] << 8 | receivedBytes[66]));
  discharge_overcurrent_protection_recovery_time = ((receivedBytes[73] << 24 | receivedBytes[72] << 16 | receivedBytes[71] << 8 | receivedBytes[70]));
  short_circuit_protection_recovery_time = ((receivedBytes[77] << 24 | receivedBytes[76] << 16 | receivedBytes[75] << 8 | receivedBytes[74]));
  max_balance_current = ((receivedBytes[81] << 24 | receivedBytes[80] << 16 | receivedBytes[79] << 8 | receivedBytes[78]) * 0.001);
  charge_overtemperature_protection = ((receivedBytes[85] << 24 | receivedBytes[84] << 16 | receivedBytes[83] << 8 | receivedBytes[82]) * 0.1);
  charge_overtemperature_protection_recovery = ((receivedBytes[89] << 24 | receivedBytes[88] << 16 | receivedBytes[87] << 8 | receivedBytes[86]) * 0.1);
  discharge_overtemperature_protection = ((receivedBytes[93] << 24 | receivedBytes[92] << 16 | receivedBytes[91] << 8 | receivedBytes[90]) * 0.1);
  discharge_overtemperature_protection_recovery = ((receivedBytes[97] << 24 | receivedBytes[96] << 16 | receivedBytes[95] << 8 | receivedBytes[94]) * 0.1);
  charge_undertemperature_protection = ((receivedBytes[101] << 24 | receivedBytes[100] << 16 | receivedBytes[99] << 8 | receivedBytes[98]) * 0.1);
  charge_undertemperature_protection_recovery = ((receivedBytes[105] << 24 | receivedBytes[104] << 16 | receivedBytes[103] << 8 | receivedBytes[102]) * 0.1);
  power_tube_overtemperature_protection = ((receivedBytes[109] << 24 | receivedBytes[108] << 16 | receivedBytes[107] << 8 | receivedBytes[106]) * 0.1);
  power_tube_overtemperature_protection_recovery = ((receivedBytes[113] << 24 | receivedBytes[112] << 16 | receivedBytes[111] << 8 | receivedBytes[110]) * 0.1);
  cell_count = ((receivedBytes[117] << 24 | receivedBytes[116] << 16 | receivedBytes[115] << 8 | receivedBytes[114]));
  // 118   4   0x01 0x00 0x00 0x00    Charge switch
  // 122   4   0x01 0x00 0x00 0x00    Discharge switch
  // 126   4   0x01 0x00 0x00 0x00    Balancer switch
  total_battery_capacity = ((receivedBytes[133] << 24 | receivedBytes[132] << 16 | receivedBytes[131] << 8 | receivedBytes[130]) * 0.001);
  short_circuit_protection_delay = ((receivedBytes[137] << 24 | receivedBytes[136] << 16 | receivedBytes[135] << 8 | receivedBytes[134]) * 1);
  balance_starting_voltage = ((receivedBytes[141] << 24 | receivedBytes[140] << 16 | receivedBytes[139] << 8 | receivedBytes[138]) * 0.001);




  DEBUG_PRINTF("Cell voltage undervoltage protection: %.2fV\n", cell_voltage_undervoltage_protection);
  DEBUG_PRINTF("Cell voltage undervoltage recovery: %.2fV\n", cell_voltage_undervoltage_recovery);
  DEBUG_PRINTF("Cell voltage overvoltage protection: %.2fV\n", cell_voltage_overvoltage_protection);
  DEBUG_PRINTF("Cell voltage overvoltage recovery: %.2fV\n", cell_voltage_overvoltage_recovery);
  DEBUG_PRINTF("Balance trigger voltage: %.2fV\n", balance_trigger_voltage);
  DEBUG_PRINTF("Power off voltage: %.2fV\n", power_off_voltage);

  DEBUG_PRINTF("Max charge current: %.2fA\n", max_charge_current);
  DEBUG_PRINTF("Charge overcurrent protection delay: %.2fs\n", charge_overcurrent_protection_delay);
  DEBUG_PRINTF("Charge overcurrent protection recovery time: %.2fs\n", charge_overcurrent_protection_recovery_time);
  DEBUG_PRINTF("Max discharge current: %.2fA\n", max_discharge_current);
  DEBUG_PRINTF("Discharge overcurrent protection delay: %.2fs\n", discharge_overcurrent_protection_delay);
  DEBUG_PRINTF("Discharge overcurrent protection recovery time: %.2fs\n", discharge_overcurrent_protection_recovery_time);
  DEBUG_PRINTF("Short circuit protection recovery time: %.2fs\n", short_circuit_protection_recovery_time);
  DEBUG_PRINTF("Max balance current: %.2fA\n", max_balance_current);
  DEBUG_PRINTF("Charge overtemperature protection: %.2fC\n", charge_overtemperature_protection);
  DEBUG_PRINTF("Charge overtemperature protection recovery: %.2fC\n", charge_overtemperature_protection_recovery);
  DEBUG_PRINTF("Discharge overtemperature protection: %.2fC\n", discharge_overtemperature_protection);
  DEBUG_PRINTF("Discharge overtemperature protection recovery: %.2fC\n", discharge_overtemperature_protection_recovery);
  DEBUG_PRINTF("Charge undertemperature protection: %.2fC\n", charge_undertemperature_protection);
  DEBUG_PRINTF("Charge undertemperature protection recovery: %.2fC\n", charge_undertemperature_protection_recovery);
  DEBUG_PRINTF("Power tube overtemperature protection: %.2fC\n", power_tube_overtemperature_protection);
  DEBUG_PRINTF("Power tube overtemperature protection recovery: %.2fC\n", power_tube_overtemperature_protection_recovery);
  DEBUG_PRINTF("Cell count: %.d\n", cell_count);
  DEBUG_PRINTF("Total battery capacity: %.2fAh\n", total_battery_capacity);
  DEBUG_PRINTF("Short circuit protection delay: %.2fus\n", short_circuit_protection_delay);
  DEBUG_PRINTF("Balance starting voltage: %.2fV\n", balance_starting_voltage);
}

void JKBMS::parseDeviceInfo() {
  DEBUG_PRINTLN("Processing device info...");
  new_data = false;

  // Debugging: Ausgabe der empfangenen Bytes
  DEBUG_PRINTLN("Raw data received:");
  for (int i = 0; i < frame; i++) {
    DEBUG_PRINTF("%02X ", receivedBytes[i]);
    if ((i + 1) % 16 == 0) DEBUG_PRINTLN();  // Neue Zeile nach 16 Bytes
  }
  DEBUG_PRINTLN();

  // Überprüfen, ob genügend Daten empfangen wurden
  if (frame < 134) {  // 134 Bytes sind für die Geräteinformationen erforderlich
    DEBUG_PRINTLN("Error: Not enough data received for device info.");
    return;
  }

  // Extrahieren der Geräteinformationen aus den empfangenen Bytes
  std::string vendorID(receivedBytes + 6, receivedBytes + 6 + 16);
  std::string hardwareVersion(receivedBytes + 22, receivedBytes + 22 + 8);
  std::string softwareVersion(receivedBytes + 30, receivedBytes + 30 + 8);
  uint32_t uptime = (receivedBytes[41] << 24) | (receivedBytes[40] << 16) | (receivedBytes[39] << 8) | receivedBytes[38];
  uint32_t powerOnCount = (receivedBytes[45] << 24) | (receivedBytes[44] << 16) | (receivedBytes[43] << 8) | receivedBytes[42];
  std::string deviceName(receivedBytes + 46, receivedBytes + 46 + 16);
  std::string devicePasscode(receivedBytes + 62, receivedBytes + 62 + 16);
  std::string manufacturingDate(receivedBytes + 78, receivedBytes + 78 + 8);
  std::string serialNumber(receivedBytes + 86, receivedBytes + 86 + 11);
  std::string passcode(receivedBytes + 97, receivedBytes + 97 + 5);
  std::string userData(receivedBytes + 102, receivedBytes + 102 + 16);
  std::string setupPasscode(receivedBytes + 118, receivedBytes + 118 + 16);

  // Ausgabe der Geräteinformationen
  DEBUG_PRINTF("  Vendor ID: %s\n", vendorID.c_str());
  DEBUG_PRINTF("  Hardware version: %s\n", hardwareVersion.c_str());
  DEBUG_PRINTF("  Software version: %s\n", softwareVersion.c_str());
  DEBUG_PRINTF("  Uptime: %d s\n", uptime);
  DEBUG_PRINTF("  Power on count: %d\n", powerOnCount);
  DEBUG_PRINTF("  Device name: %s\n", deviceName.c_str());
  DEBUG_PRINTF("  Device passcode: %s\n", devicePasscode.c_str());
  DEBUG_PRINTF("  Manufacturing date: %s\n", manufacturingDate.c_str());
  DEBUG_PRINTF("  Serial number: %s\n", serialNumber.c_str());
  DEBUG_PRINTF("  Passcode: %s\n", passcode.c_str());
  DEBUG_PRINTF("  User data: %s\n", userData.c_str());
  DEBUG_PRINTF("  Setup passcode: %s\n", setupPasscode.c_str());
}

void JKBMS::parseData() {
  DEBUG_PRINTLN("Parsing data...");
  new_data = false;
  ignoreNotifyCount = 10;
  // Cell voltages
  for (int j = 0, i = 7; i < 38; j++, i += 2) {
    cellVoltage[j] = ((receivedBytes[i] << 8 | receivedBytes[i - 1]) * 0.001);
  }

  Average_Cell_Voltage = (((int)receivedBytes[75] << 8 | receivedBytes[74]) * 0.001);

  Delta_Cell_Voltage = (((int)receivedBytes[77] << 8 | receivedBytes[76]) * 0.001);

  for (int j = 0, i = 81; i < 112; j++, i += 2) {
    wireResist[j] = (((int)receivedBytes[i] << 8 | receivedBytes[i - 1]) * 0.001);
  }

  if (receivedBytes[145] == 0xFF) {
    MOS_Temp = ((0xFF << 24 | 0xFF << 16 | receivedBytes[145] << 8 | receivedBytes[144]) * 0.1);
  } else {
    MOS_Temp = ((receivedBytes[145] << 8 | receivedBytes[144]) * 0.1);
  }

  // Battery voltage
  Battery_Voltage = ((receivedBytes[153] << 24 | receivedBytes[152] << 16 | receivedBytes[151] << 8 | receivedBytes[150]) * 0.001);

  Charge_Current = ((receivedBytes[161] << 24 | receivedBytes[160] << 16 | receivedBytes[159] << 8 | receivedBytes[158]) * 0.001);

  Battery_Power = Battery_Voltage * Charge_Current;

  if (receivedBytes[163] == 0xFF) {
    Battery_T1 = ((0xFF << 24 | 0xFF << 16 | receivedBytes[163] << 8 | receivedBytes[162]) * 0.1);
  } else {
    Battery_T1 = ((receivedBytes[163] << 8 | receivedBytes[162]) * 0.1);
  }

  if (receivedBytes[165] == 0xFF) {
    Battery_T2 = ((0xFF << 24 | 0xFF << 16 | receivedBytes[165] << 8 | receivedBytes[164]) * 0.1);
  } else {
    Battery_T2 = ((receivedBytes[165] << 8 | receivedBytes[164]) * 0.1);
  }

  if ((receivedBytes[171] & 0xF0) == 0x0) {
    Balance_Curr = ((receivedBytes[171] << 8 | receivedBytes[170]) * 0.001);
  } else if ((receivedBytes[171] & 0xF0) == 0xF0) {
    Balance_Curr = (((receivedBytes[171] & 0x0F) << 8 | receivedBytes[170]) * -0.001);
  }

  Balancing_Action = receivedBytes[172];
  Percent_Remain = (receivedBytes[173]);
  Capacity_Remain = ((receivedBytes[177] << 24 | receivedBytes[176] << 16 | receivedBytes[175] << 8 | receivedBytes[174]) * 0.001);
  Nominal_Capacity = ((receivedBytes[181] << 24 | receivedBytes[180] << 16 | receivedBytes[179] << 8 | receivedBytes[178]) * 0.001);
  Cycle_Count = ((receivedBytes[185] << 24 | receivedBytes[184] << 16 | receivedBytes[183] << 8 | receivedBytes[182]));
  Cycle_Capacity = ((receivedBytes[189] << 24 | receivedBytes[188] << 16 | receivedBytes[187] << 8 | receivedBytes[186]) * 0.001);

  Uptime = receivedBytes[196] << 16 | receivedBytes[195] << 8 | receivedBytes[194];
  sec = Uptime % 60;
  Uptime /= 60;
  mi = Uptime % 60;
  Uptime /= 60;
  hr = Uptime % 24;
  days = Uptime / 24;

  if (receivedBytes[198] > 0) {
    Charge = true;
  } else if (receivedBytes[198] == 0) {
    Charge = false;
  }
  if (receivedBytes[199] > 0) {
    Discharge = true;
  } else if (receivedBytes[199] == 0) {
    Discharge = false;
  }
  if (receivedBytes[201] > 0) {
    Balance = true;
  } else if (receivedBytes[201] == 0) {
    Balance = false;
  }


  // Output values

  DEBUG_PRINTF("\n--- Data from %s ---\n", targetMAC.c_str());
  DEBUG_PRINTLN("Cell Voltages:");
  for (int j = 0; j < 16; j++) {
    DEBUG_PRINTF("  Cell %02d: %.3f V\n", j + 1, cellVoltage[j]);
  }
  DEBUG_PRINTLN("wire Resist:");
  for (int j = 0; j < 16; j++) {
    DEBUG_PRINTF("  Cell %02d: %.3f Ohm\n", j + 1, wireResist[j]);
  }
  DEBUG_PRINTF("Average Cell Voltage: %.2fV\n", Average_Cell_Voltage);
  DEBUG_PRINTF("Delta Cell Voltage: %.2fV\n", Delta_Cell_Voltage);
  DEBUG_PRINTF("Balance Curr: %.2fA\n", Balance_Curr);
  DEBUG_PRINTF("Battery Voltage: %.2fV\n", Battery_Voltage);
  DEBUG_PRINTF("Battery Power: %.2fW\n", Battery_Power);
  DEBUG_PRINTF("Charge Current: %.2fA\n", Charge_Current);
  DEBUG_PRINTF("Charge: %d%%\n", Percent_Remain);
  DEBUG_PRINTF("Capacity Remain: %.2fAh\n", Capacity_Remain);
  DEBUG_PRINTF("Nominal Capacity: %.2fAh\n", Nominal_Capacity);
  DEBUG_PRINTF("Cycle Count: %.2f\n", Cycle_Count);
  DEBUG_PRINTF("Cycle Capacity: %.2fAh\n", Cycle_Capacity);
  DEBUG_PRINTF("Temperature T1: %.1fC\n", Battery_T1);
  DEBUG_PRINTF("Temperature T2: %.1fC\n", Battery_T2);
  DEBUG_PRINTF("Temperature MOS: %.1fC\n", MOS_Temp);
  DEBUG_PRINTF("Uptime: %dd %dh %dm\n", days, hr, mi);
  DEBUG_PRINTF("Charge: %d\n", Charge);
  DEBUG_PRINTF("Discharge: %d\n", Discharge);
  DEBUG_PRINTF("Balance: %d\n", Balance);
  DEBUG_PRINTF("Balancing Action: %d\n", Balancing_Action);
}

// TODO: Convert to LVGL (All the way down to line:964 needs to be eliminated or changed.)
void handleRoot() {
  if (LittleFS.exists("/index.html")) {
    server.sendHeader("Location", "/main");
    server.send(302, "text/plain", "Umleitung auf /main");
  } else {
    String redirectPage = "<html><body style='text-align:center;'>";
    redirectPage += "<p>index.html wurde nicht gefunden.</p>";
    redirectPage += "<p><a href='/fs'>File-Server</a></p>";
    redirectPage += "</body></html>";

    server.send(200, "text/html", redirectPage);
  }
}

void handleJSON() {
  const size_t capacity = JSON_OBJECT_SIZE(50) + JSON_ARRAY_SIZE(16) + 2000;  // Ausreichend großer Buffer
  DynamicJsonDocument doc(capacity);

  for (int i = 0; i < bmsDeviceCount; i++) {
    JKBMS& bms = jkBmsDevices[i];
    JsonObject device = doc.createNestedObject(bms.targetMAC);

    // Allgemeine Informationen
    device["battery_voltage"] = bms.Battery_Voltage;
    device["battery_power"] = bms.Battery_Power;
    device["charge_current"] = bms.Charge_Current;
    device["percent_remain"] = bms.Percent_Remain;
    device["capacity_remain"] = bms.Capacity_Remain;
    device["nominal_capacity"] = bms.Nominal_Capacity;
    device["cycle_count"] = bms.Cycle_Count;
    device["cycle_capacity"] = bms.Cycle_Capacity;
    device["uptime"] = String(bms.days) + "d " + String(bms.hr) + "h " + String(bms.mi) + "m";

    // Temperaturen
    device["battery_t1"] = bms.Battery_T1;
    device["battery_t2"] = bms.Battery_T2;
    device["mos_temp"] = bms.MOS_Temp;

    // Zellspannungen
    JsonArray cell_voltages = device.createNestedArray("cell_voltages");
    for (int j = 0; j < bms.cell_count; j++) {
      cell_voltages.add(bms.cellVoltage[j]);
    }
    device["average_cell_voltage"] = bms.Average_Cell_Voltage;
    device["delta_cell_voltage"] = bms.Delta_Cell_Voltage;

    // Widerstände
    JsonArray wire_resist = device.createNestedArray("wire_resist");
    for (int j = 0; j < bms.cell_count; j++) {
      wire_resist.add(bms.wireResist[j]);
    }

    // Statusflags
    device["charge"] = bms.Charge;
    device["discharge"] = bms.Discharge;
    device["balance"] = bms.Balance;
    device["balancing_action"] = bms.Balancing_Action;
    device["balance_curr"] = bms.Balance_Curr;

    // BMS Einstellungen
    device["cell_count"] = bms.cell_count;
    device["total_battery_capacity"] = bms.total_battery_capacity;
    device["balance_trigger_voltage"] = bms.balance_trigger_voltage;
    device["balance_starting_voltage"] = bms.balance_starting_voltage;
    device["max_charge_current"] = bms.max_charge_current;
    device["max_discharge_current"] = bms.max_discharge_current;
    device["max_balance_current"] = bms.max_balance_current;
    device["cell_undervoltage_protection"] = bms.cell_voltage_undervoltage_protection;
    device["cell_undervoltage_recovery"] = bms.cell_voltage_undervoltage_recovery;
    device["cell_overvoltage_protection"] = bms.cell_voltage_overvoltage_protection;
    device["cell_overvoltage_recovery"] = bms.cell_voltage_overvoltage_recovery;
    device["power_off_voltage"] = bms.power_off_voltage;
    device["charge_overcurrent_protection_delay"] = bms.charge_overcurrent_protection_delay;
    device["charge_overcurrent_protection_recovery_time"] = bms.charge_overcurrent_protection_recovery_time;
    device["discharge_overcurrent_protection_delay"] = bms.discharge_overcurrent_protection_delay;
    device["discharge_overcurrent_protection_recovery_time"] = bms.discharge_overcurrent_protection_recovery_time;
    device["short_circuit_protection_recovery_time"] = bms.short_circuit_protection_recovery_time;
    device["charge_overtemperature_protection"] = bms.charge_overtemperature_protection;
    device["charge_overtemperature_protection_recovery"] = bms.charge_overtemperature_protection_recovery;
    device["discharge_overtemperature_protection"] = bms.discharge_overtemperature_protection;
    device["discharge_overtemperature_protection_recovery"] = bms.discharge_overtemperature_protection_recovery;
    device["charge_undertemperature_protection"] = bms.charge_undertemperature_protection;
    device["charge_undertemperature_protection_recovery"] = bms.charge_undertemperature_protection_recovery;
    device["power_tube_overtemperature_protection"] = bms.power_tube_overtemperature_protection;
    device["power_tube_overtemperature_protection_recovery"] = bms.power_tube_overtemperature_protection_recovery;
    device["short_circuit_protection_delay"] = bms.short_circuit_protection_delay;
  }

  String jsonResponse;
  serializeJson(doc, jsonResponse);
  server.send(200, "application/json", jsonResponse);
}

void handleControl() {
  if (server.method() == HTTP_POST) {
    // JSON-Daten aus dem Request lesen
    DynamicJsonDocument doc(200);
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
      server.send(400, "text/plain", "Invalid JSON");
      return;
    }

    // Daten aus dem JSON-Objekt extrahieren
    String mac = doc["mac"];
    String action = doc["action"];
    String state = doc["state"];

    // Parameter für writeRegister() bestimmen
    uint8_t address;
    uint32_t value;

    if (action == "charging") {
      address = 0x1D;
    } else if (action == "discharging") {
      address = 0x1E;
    } else if (action == "balancing") {
      address = 0x1F;
    } else {
      server.send(400, "text/plain", "Invalid action");
      return;
    }

    if (state == "on") {
      value = 0x0000001;
    } else if (state == "off") {
      value = 0x00000000;
    } else {
      server.send(400, "text/plain", "Invalid state");
      return;
    }

    // writeRegister() für das entsprechende BMS aufrufen
    for (int i = 0; i < bmsDeviceCount; i++) {
      if (jkBmsDevices[i].targetMAC == mac.c_str()) {
        jkBmsDevices[i].writeRegister(address, value, 0x04);
        server.send(200, "text/plain", "Command executed");
        return;
      }
    }

    server.send(404, "text/plain", "BMS not found");
  } else {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}

void handleSketchInfo() {
  String sketchInfo = getSketchInfo();
  server.send(200, "application/json", sketchInfo);
}

void handleFreeHeap() {
  char formattedHeap[20];
  formatBytes(minFreeHeap, formattedHeap, sizeof(formattedHeap));

  // JSON-Objekt erstellen
  DynamicJsonDocument doc(100);
  doc["free_heap"] = formattedHeap;

  String jsonResponse;
  serializeJson(doc, jsonResponse);
  server.send(200, "application/json", jsonResponse);
}

void handleUptime() {
  calculateUptime();
  DynamicJsonDocument doc(100);
  doc["uptime_seconds"] = totalSeconds;
  doc["uptime_formatted"] = formatUptime(totalSeconds);

  String jsonResponse;
  serializeJson(doc, jsonResponse);
  server.send(200, "application/json", jsonResponse);
}

void fileserverSetup() {
  server.on("/fs", HTTP_GET, handleFileList);
  server.on("/upload", HTTP_POST, sendResponce, handleFileUpload);
  server.on("/delete", HTTP_GET, handleFileDelete);
  server.on("/view", HTTP_GET, handleFileView);
  server.on("/format", HTTP_GET, handleFormat);
}

void handleFileList() {
  String files;
  File root = LittleFS.open("/");
  File file = root.openNextFile();

  while (file) {
    char buffer1[20];
    size_t sizeofFile = file.size();
    formatBytes(sizeofFile, buffer1, sizeof(buffer1));
    files += "<div class='file-container'>";
    files += "<span class='filename'>" + String(file.name()) + "</span>";
    files += "<span class='file-size'>" + String(buffer1) + "</span>";
    files += "<span class='file-date'>" + getLastModified(file) + "</span>";
    files += "<span class='file-actions'>";
    files += "<a href='/delete?file=/" + String(file.name()) + "'>Delete</a>";
    files += "<a href='/view?file=" + String(file.name()) + "'>View</a>";
    files += "</span></div>";
    file = root.openNextFile();
  }

  String html = "<html><head>";
  html += "<style>body { text-align: center; }";
  html += ".container { display: inline-block; text-align: center; padding: 20px; border: 1px solid #ccc; border-radius: 10px; }";
  html += ".file-container { margin: 10px 0; display: flex; align-items: center; }";
  html += ".filename { flex: 1; margin-right: 10px; text-align: left; }";
  html += ".file-size { font-size: 60%; margin-right: 10px; }";
  html += ".file-date { font-size: 60%; margin-right: 10px; }";
  html += ".file-actions { display: flex; }";
  html += ".file-actions a { margin-right: 5px; text-decoration: none; color: #007BFF; }";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<h2 style='margin-top: 0;'>Files on LittleFS</h2>";
  html += "<div class='file-list'>" + files + "</div>";
  html += "<br>";
  html += "<hr>";
  html += "<h3>Upload</h3>";
  html += "<form method='post' action='/upload' enctype='multipart/form-data'>";
  html += "<input type='file' name='upload[]' id='uploadFile' multiple>";
  html += "<input type='submit' value='Upload'>";
  html += "</form>";
  html += "<br>";
  html += "<hr>";
  html += "<h3>Format Filesystem</h3>";
  html += "<form method='get' action='/format'>";
  html += "<input type='submit' value='Format'>";
  html += "</form>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handleFileUpload() {
  if (server.uri() != "/upload") return;
  HTTPUpload& upload = server.upload();
  static File file;

  if (upload.status == UPLOAD_FILE_START) {
    String filename = "/" + upload.filename;
    DEBUG_PRINTLN("Upload started: " + filename);
    file = LittleFS.open(filename, "w");
    if (!file) {
      DEBUG_PRINTLN(F("Failed to open file for writing"));
      return;
    }
    DEBUG_PRINTLN(F("File opened for writing"));
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (file) {
      size_t bytesWritten = file.write(upload.buf, upload.currentSize);
      if (bytesWritten != upload.currentSize) {
        DEBUG_PRINTLN(F("Error writing to file"));
      } else {
        DEBUG_PRINTLN("Bytes written to file: " + String(upload.currentSize));
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (file) {
      file.close();
      DEBUG_PRINTLN(F("Upload finished. File closed"));
    } else {
      DEBUG_PRINTLN(F("Upload finished, but file was not open"));
    }
  }
}

void handleFileDelete() {
  String filename = server.arg("file");
  DEBUG_PRINTLN("Delete: " + filename);

  if (LittleFS.remove(filename)) {
    DEBUG_PRINTLN(F("File deleted"));
  } else {
    DEBUG_PRINTLN(F("Failed to delete file"));
  }
  sendResponce();
}

void handleFileView() {
  String filename = server.arg("file");
  DEBUG_PRINTLN("View: " + filename);

  if (!filename.startsWith("/")) {
    filename = "/" + filename;
  }

  File file = LittleFS.open(filename, "r");
  if (!file) {
    DEBUG_PRINTLN(F("Failed to open file for reading"));
    server.send(404, "text/plain", "File not found");
    return;
  }

  String content = file.readString();
  file.close();

  String html = "<html><body><h2>File Viewer</h2><p>Content of " + filename + ":</p><pre>" + content + "</pre></body></html>";
  server.send(200, "text/html", html);
}

void handleFormat() {
  LittleFS.format();
  DEBUG_PRINTLN(F("Formating LittleFS"));
  sendResponce();
}

void sendResponce() {
  server.sendHeader("Location", "/fs");
  server.send(303, "message/http");
}

String getLastModified(File file) {
  time_t lastWriteTime = file.getLastWrite();
  struct tm* timeinfo;
  char buffer[20];
  timeinfo = localtime(&lastWriteTime);
  strftime(buffer, sizeof(buffer), "%d:%m:%y %H:%M", timeinfo);
  return String(buffer);
}

// creates a new obj to use as base screen, and set some properties, such as flex
// vertical arrangement, centered horizontally, transparent background, etc.
lv_obj_t * new_screen(lv_obj_t * parent) {

  lv_obj_t * obj = lv_obj_create(parent);
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(obj, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_row(obj, 10, LV_PART_MAIN);

  return obj;
}

lv_obj_t * scr_main = nullptr;
lv_obj_t * btn_exit = nullptr;
lv_obj_t * lbl_header = nullptr;
lv_obj_t * scr_led;
// global because referenced in the callback
lv_obj_t * slider_led[3];
lv_obj_t * sw_led_true;

// callback when any of the values change
static void scr_led_cb(lv_event_t * e) {
  LVGL_CYD::led(
    lv_slider_get_value(slider_led[0]),
    lv_slider_get_value(slider_led[1]),
    lv_slider_get_value(slider_led[2]),
    lv_obj_has_state(sw_led_true, LV_STATE_CHECKED)
  );
}

void go_led() {

  if (!scr_led) {
    scr_led = new_screen(NULL);

    // switch between true colors and max brightness
    lv_obj_t * sw_container = lv_obj_create(scr_led);
    lv_obj_set_style_bg_opa(sw_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(sw_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(sw_container, 0, 0);
    lv_obj_set_height(sw_container, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(sw_container, 0, 0);
    lv_obj_set_flex_flow(sw_container, LV_FLEX_FLOW_ROW);
    lv_obj_t * lbl_max = lv_label_create(sw_container);
    lv_label_set_text(lbl_max, "max");
    sw_led_true = lv_switch_create(sw_container);
    lv_obj_add_state(sw_led_true, LV_STATE_CHECKED);
    lv_obj_set_size(sw_led_true, 40, 20);
    lv_obj_add_event_cb(sw_led_true, scr_led_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t * lbl_true = lv_label_create(sw_container);
    lv_label_set_text(lbl_true, "true");
    lv_obj_set_style_pad_column(sw_container, 5, 0);

    // colors for the 4 sliders
    lv_color_t color[3] {
      lv_color_make(255, 0, 0),
      lv_color_make(0, 200, 0),  // Full-on green looks too yellow on display
      lv_color_make(0, 0, 255)
    };

    // set up the sliders
    for (int n = 0; n < 3; n++) {
      slider_led[n] = lv_slider_create(scr_led);
      lv_obj_set_width(slider_led[n], lv_pct(80));
      lv_obj_set_style_margin_top(slider_led[n], 25, LV_PART_MAIN);
      lv_obj_set_style_bg_color(slider_led[n], color[n], LV_PART_MAIN);  // set the track color
      lv_obj_set_style_bg_color(slider_led[n], color[n], LV_PART_INDICATOR); // set the indicator color
      lv_obj_set_style_bg_color(slider_led[n], lv_color_lighten(color[n], LV_OPA_30), LV_PART_KNOB); // set the knob color
      lv_slider_set_range(slider_led[n], 0, 255);
      lv_obj_add_event_cb(slider_led[n], scr_led_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }

  }

  lv_label_set_text(lbl_header, "RGB LED color");
  lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
  lv_screen_load(scr_led);

}

//
// LBacklight brightness screen
//

lv_obj_t * scr_bl;

// These need to be global as they are referenced in the callback.
lv_obj_t * slider_bl;


void go_bl() {

  if (!scr_bl) {
    scr_bl = new_screen(NULL);

    slider_bl = lv_slider_create(scr_bl);
    lv_obj_set_width(slider_bl, lv_pct(80));
    lv_slider_set_range(slider_bl, 0, 255);
    lv_obj_add_event_cb(slider_bl, [](lv_event_t * e) -> void {
      LVGL_CYD::backlight(lv_slider_get_value(slider_bl));
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // turn on backlight initially
    lv_slider_set_value(slider_bl, 255, LV_ANIM_OFF);

    // set backlight to initial slider value
    LVGL_CYD::backlight(lv_slider_get_value(slider_bl));
  }

  lv_label_set_text(lbl_header, "Backlight brightness");
  lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
  lv_screen_load(scr_bl);

}

//
// scr_touch
//

lv_obj_t * scr_touch;

// These need to be global as they are used in the callback.
lv_obj_t * horizontal;
lv_obj_t * vertical;

void go_touch() {

  if (!scr_touch) {

    scr_touch = new_screen(NULL);
    lv_obj_set_layout(scr_touch, LV_LAYOUT_NONE);         // need to place children (the crosshairs) manually
    lv_obj_set_style_pad_top(scr_touch, 0, LV_PART_MAIN);
    lv_obj_add_flag(scr_touch, LV_OBJ_FLAG_CLICKABLE);

    horizontal = lv_obj_create(scr_touch);
    lv_obj_set_style_border_width(horizontal, 0, 0);
    lv_obj_set_style_radius(horizontal, 0, LV_PART_MAIN);
    lv_obj_set_size(horizontal, lv_pct(100), 3);
    lv_obj_set_style_bg_color(horizontal, lv_color_black(), LV_PART_MAIN);
    lv_obj_clear_flag(horizontal, LV_OBJ_FLAG_CLICKABLE);

    vertical = lv_obj_create(scr_touch);
    lv_obj_set_style_border_width(vertical, 0, 0);
    lv_obj_set_style_radius(vertical, 0, LV_PART_MAIN);
    lv_obj_set_size(vertical, 3, lv_pct(100));
    lv_obj_set_style_bg_color(vertical, lv_color_black(), LV_PART_MAIN);
    lv_obj_clear_flag(vertical, LV_OBJ_FLAG_CLICKABLE);
    
    lv_obj_add_event_cb(scr_touch, [](lv_event_t * e) -> void {
      if (lv_event_get_code(e) == LV_EVENT_PRESSED || lv_event_get_code(e) == LV_EVENT_PRESSING) {
        lv_point_t point;
        lv_indev_get_point(lv_indev_get_act(), &point); // Get the current touch point
        lv_obj_set_y(horizontal, point.y);
        lv_obj_set_x(vertical, point.x);
        lv_obj_clear_flag(horizontal, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(vertical, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
      } else if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        lv_obj_add_flag(horizontal, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(vertical, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
      }
    }, LV_EVENT_ALL, NULL);
  }

  lv_obj_add_flag(horizontal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(vertical, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(lbl_header, "Touch indicator");
  lv_screen_load(scr_touch);

}


//
// Rotate
//

void go_rotate() {

  lv_disp_t * display = lv_disp_get_default();
  lv_display_rotation_t rotation = lv_display_get_rotation(display);
  if (rotation == USB_LEFT) lv_display_set_rotation(display, USB_DOWN);
  else if (rotation == USB_DOWN) lv_display_set_rotation(display, USB_RIGHT);
  else if (rotation == USB_RIGHT) lv_display_set_rotation(display, USB_UP);
  else lv_display_set_rotation(display, USB_LEFT);

}

void go_main() {
  if (!scr_main) {
    // if screen doesn't exist yet, create it and its children
    scr_main = new_screen(NULL);

    lv_obj_set_size(scr_main, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));

    lv_obj_t * btn_led = lv_button_create(scr_main);
    lv_obj_t * lbl_led = lv_label_create(btn_led);
    lv_label_set_text(lbl_led, "RGB LED color");
    lv_obj_add_event_cb(btn_led, [](lv_event_t * e) -> void {
      go_led();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_bl = lv_button_create(scr_main);
    lv_obj_t * lbl_bl = lv_label_create(btn_bl);
    lv_label_set_text(lbl_bl, "Backlight brightness");
    lv_obj_add_event_cb(btn_bl, [](lv_event_t * e) -> void {
      go_bl();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_touch = lv_button_create(scr_main);
    lv_obj_t * lbl_touch = lv_label_create(btn_touch);
    lv_label_set_text(lbl_touch, "Touch indicator");
    lv_obj_add_event_cb(btn_touch, [](lv_event_t * e) -> void {
      go_touch();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_rotate = lv_button_create(scr_main);
    lv_obj_t * lbl_rotate = lv_label_create(btn_rotate);
    lv_label_set_text(lbl_rotate, "Rotate display");
    lv_obj_add_event_cb(btn_rotate, [](lv_event_t * e) -> void {
      go_rotate();
    }, LV_EVENT_CLICKED, NULL);
  
  }

  lv_label_set_text(lbl_header, "LVGL_CYD demo");   // screen header
  lv_obj_add_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);    // disable exit, already at main screen
  lv_screen_load(scr_main);                         // Tell LVGL to load as active screen

}


//********************************************
// Main Program
//********************************************
void setup() {
  Serial.begin(115200);
  lastMillis = millis();

  LVGL_CYD::begin(SCREEN_ORIENTATION);
  // LVGL has multiple layers, one below and two above the 'active screen'.
  // I use the 'bottom' layer for the background color and keep all further
  // objects on transparent background. I use the 'top' layer for the screen
  // header and the exit button.
  // lv_obj_t * btn_exit;
  // lv_obj_t * lbl_header;
  // bottom layer opaque and white
  lv_obj_set_style_bg_opa(lv_layer_bottom(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(lv_layer_bottom(), lv_color_white(), LV_PART_MAIN);

  // btn_exit is not really an LVGL button, just an obj made clickable.
  // It holds the little 'x' to exit back to main screen, but is bigger
  // because the little 'x' would be near impossible to hit.
  btn_exit = lv_obj_create(lv_layer_top());
  lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btn_exit, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(btn_exit, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(btn_exit, 0, LV_PART_MAIN);
  lv_obj_set_size(btn_exit, 40, 40);
  lv_obj_align(btn_exit, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_add_event_cb(btn_exit, [](lv_event_t * e) -> void {
    go_main();
  }, LV_EVENT_CLICKED, NULL);

  // holds actual little 'x', lives inside btn_exit
  lv_obj_t * lbl_exit_symbol = lv_label_create(btn_exit);
  lv_obj_set_style_text_font(lbl_exit_symbol, &lv_font_montserrat_18, LV_PART_MAIN);
  lv_obj_set_style_text_align(lbl_exit_symbol, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(lbl_exit_symbol, LV_SYMBOL_CLOSE);
  lv_obj_align(lbl_exit_symbol, LV_ALIGN_TOP_RIGHT, 5, -10);  

  // page header
  lbl_header = lv_label_create(lv_layer_top());
  lv_obj_set_style_text_font(lbl_header, &lv_font_montserrat_18, LV_PART_MAIN);
  lv_obj_align(lbl_header, LV_ALIGN_TOP_LEFT, 5, 3);

  go_main();

  // TODO: This can all go once I have LVGL set up.
  // WiFi initialisieren
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  configTime(0, 0, "fritz.box", "de.pool.ntp.org");
  setupLittleFS();
  server.serveStatic("/main", LittleFS, "/index.html");
  server.serveStatic("/style.css", LittleFS, "/style.css", "text/css");
  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleJSON);
  server.on("/control", HTTP_POST, handleControl);
  server.on("/sketchinfo", HTTP_GET, handleSketchInfo);
  server.on("/freeheap", HTTP_GET, handleFreeHeap);
  server.on("/uptime", HTTP_GET, handleUptime);
  fileserverSetup();
  server.begin();


  DEBUG_PRINTLN("Initializing NimBLE Client...");

  NimBLEDevice::init("MultiJKBMS-Client");
  NimBLEDevice::setPower(3);

  pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(&scanCallbacks);
  pScan->setInterval(100);
  pScan->setWindow(100);
  pScan->setActiveScan(true);
}

void loop() {
  lv_task_handler();
  calculateUptime();
  monitorFreeHeap();

  server.handleClient();
  // Connection management
  int connectedCount = 0;  // Counter for connected devices

  for (int i = 0; i < bmsDeviceCount; i++) {
    if (jkBmsDevices[i].targetMAC.empty()) continue;  // Skip empty MAC addresses

    if (jkBmsDevices[i].doConnect && !jkBmsDevices[i].connected) {
      if (jkBmsDevices[i].connectToServer()) {
        DEBUG_PRINTF("%s connected successfully\n", jkBmsDevices[i].targetMAC.c_str());
      }
      jkBmsDevices[i].doConnect = false;
    }

    if (jkBmsDevices[i].connected) {
      connectedCount++;  // Increment counter for each connected device
      if (millis() - jkBmsDevices[i].lastNotifyTime > 20000) {
        DEBUG_PRINTF("%s connection timeout\n", jkBmsDevices[i].targetMAC.c_str());
        NimBLEClient* pClient = NimBLEDevice::getClientByPeerAddress(jkBmsDevices[i].advDevice->getAddress());
        if (pClient) pClient->disconnect();
      }
    }
  }

  // Start scan only if not all devices are connected
  if (connectedCount < bmsDeviceCount && (millis() - lastScanTime >= 10000)) {
    DEBUG_PRINTLN("Starting scan...");
    pScan->start(5000, false, true);
    lastScanTime = millis();
  }

  delay(10);
}
