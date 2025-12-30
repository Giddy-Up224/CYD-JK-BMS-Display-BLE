#include "utils.h"

// Global variables for monitoring
uint32_t minFreeHeap = UINT32_MAX;
unsigned long lastHeapUpdate = 0;
unsigned long lastMillis = 0;
uint32_t totalSeconds = 0;

void calculateUptime() {
  unsigned long currentMillis = millis();
  unsigned long elapsedMillis = currentMillis - lastMillis;

  // Handle millis() overflow
  if (currentMillis < lastMillis) {
    // An overflow occurred
    elapsedMillis = UINT32_MAX - lastMillis + currentMillis;
  }

  if (elapsedMillis >= 1000) {
    // Add elapsed seconds
    totalSeconds += elapsedMillis / 1000;

    // Save current millis() value
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

// Readable display of memory sizes
void formatBytes(size_t bytes, char *buffer, size_t bufferSize) {
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

void getCoreVersion(char *version) {
  // Write the version to the char array
  sprintf(version, "%d.%d.%d", ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
}

void getSketchName(char *sketchName) {
  const char *filename = __FILE__;
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
  return compileDate, compileTime, sketchName, coreVersion;
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