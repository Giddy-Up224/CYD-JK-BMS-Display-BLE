#include <Arduino.h>
#include <NimBLEDevice.h>

int scanTime = 5 * 1000; // In milliseconds, 0 = scan forever
NimBLEScan *pBLEScan;

bool active = true;

class ScanCallbacks : public NimBLEScanCallbacks
{
  void onDiscovered(const NimBLEAdvertisedDevice *advertisedDevice) override {
    Serial.printf("Discovered Advertised Device: %s \n", advertisedDevice->toString().c_str());
  }

  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override {
    Serial.printf("Advertised Device Result: %s \n", advertisedDevice->toString().c_str());
  }

  void onScanEnd(const NimBLEScanResults &results, int reason) override {
    Serial.print("Scan Ended; reason = ");
    Serial.println(reason);
    // active = !active;
    pBLEScan->setActiveScan(active);
    Serial.printf("scan start, active = %u\n", active);
    pBLEScan->start(scanTime);
  }
} scanCallbacks;

void startScan(uint32_t durationMs) {
  static ScanCallbacks scanCallbacks; // make sure it stays alive

  NimBLEScan *pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setScanCallbacks(&scanCallbacks);
  pBLEScan->setActiveScan(true); // or false
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(100);
  pBLEScan->start(durationMs);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Scanning...");
  NimBLEDevice::init("active-passive-scan");
  startScan(scanTime);
}

void loop() {
  // nothing to do here...
}
