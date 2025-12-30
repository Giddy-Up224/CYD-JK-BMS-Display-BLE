#pragma once

#include <Arduino.h>

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

// Memory monitoring functions
void calculateUptime();
void monitorFreeHeap();
void formatBytes(size_t bytes, char *buffer, size_t bufferSize);

// Utility functions
void getCoreVersion(char *version);
void getSketchName(char *sketchName);
String getSketchInfo();
String formatUptime(uint32_t totalSeconds);

// Global variables for monitoring
extern uint32_t minFreeHeap;
extern unsigned long lastHeapUpdate;
extern unsigned long lastMillis;
extern uint32_t totalSeconds;