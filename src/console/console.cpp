#include "console.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ble/jk_ble.h"
#include "bms/bms_data.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display/display.h"
#include "ui/ui.h"

static const char *TAG = "Console";
static const int MAX_FOUND = 20;

static const char *linkName(LinkState l) {
    switch(l) {
    case LinkState::NoDevice: return "no device";
    case LinkState::Picking: return "scanning";
    case LinkState::Connecting: return "connecting";
    case LinkState::Connected: return "connected";
    }
    return "?";
}

static void printStatus() {
    BmsSnapshot d;
    bmsDataGet(d);
    printf("Device: %s %s (%s)\n", d.name[0] ? d.name : "-", d.addr[0] ? d.addr : "", linkName(d.link));
    if(d.info.valid) printf("Model %s  hw %s  sw %s  serial %s\n", d.info.model, d.info.hw, d.info.sw, d.info.serial);
    printf("Protocol %s  cell frames %lu\n", jkProtoName(d.proto), static_cast<unsigned long>(d.cellFrames));
    if(!d.hasCells) return;
    const JkCells &c = d.cells;
    for(int i = 0; i < c.cellCount; i++) {
        printf("  Cell %02d: %.3f V  %.3f Ohm\n", i + 1, c.cellV[i], c.resistOhm[i]);
    }
    printf("Pack %.2f V  avg %.3f V  delta %.3f V  min %d max %d\n", c.packV, c.avgV, c.deltaV, c.minCell + 1, c.maxCell + 1);
    printf("Balance %.3f A (state %u)  uptime %lu s\n", c.balanceA, c.balancing, static_cast<unsigned long>(c.uptimeS));
    if(c.hasBms) {
        printf("Current %.3f A  power %.1f W  SOC %d%%  %.2f/%.2f Ah  cycles %lu\n", c.currentA, c.powerW, c.soc, c.remainAh,
               c.nominalAh, static_cast<unsigned long>(c.cycles));
        printf("T1 %.1f C  T2 %.1f C  MOS %.1f C  charge %d discharge %d balance %d\n", c.t1C, c.t2C, c.mosC, c.chargeOn,
               c.dischargeOn, c.balanceOn);
    }
    const JkSettings &s = d.settings;
    if(s.valid) {
        printf("Settings: %d cells  start %.3f V  trigger %.3f V  max balance %.3f A  power off %.3f V  balancing %s\n",
               s.cellCount, s.startBalanceV, s.triggerDeltaV, s.maxBalanceA, s.powerOffV, s.balanceEnabled ? "on" : "off");
    }
}

static int found(ScanEntry *out) {
    return blePickResults(out, MAX_FOUND);
}

static void printFound() {
    ScanEntry e[MAX_FOUND];
    int n = found(e);
    printf("Found %d JK device(s)%s:\n", n, blePicking() ? " (scanning)" : "");
    for(int i = 0; i < n; i++) {
        char a[18];
        bleAddrStr(e[i].addr, a);
        printf("  [%d] %s  %-20s %d dBm\n", i, a, e[i].name, e[i].rssi);
    }
}

static void select(const char *arg) {
    ScanEntry e[MAX_FOUND];
    int n = found(e);
    uint8_t addr[6];
    if(bleAddrParse(arg, addr)) {
        for(int i = 0; i < n; i++) {
            if(memcmp(e[i].addr, addr, 6) == 0) {
                bleSelect(e[i]);
                printf("Selected %s\n", arg);
                return;
            }
        }
        // not seen in a scan: assume a public address
        ScanEntry s = {};
        s.addrType = 0;
        memcpy(s.addr, addr, 6);
        bleSelect(s);
        printf("Selected %s (not seen in a scan, public address)\n", arg);
        return;
    }
    char *end;
    long i = strtol(arg, &end, 10);
    if(*arg && !*end && i >= 0 && i < n) {
        bleSelect(e[i]);
        printf("Selected [%ld] %s\n", i, e[i].name);
        return;
    }
    printf("select <index from `found`> or select <mac>\n");
}

static void dumpHex(const char *title, const uint8_t *b) {
    printf("%s:\n", title);
    for(int i = 0; i < JK_FRAME_SIZE; i += 16) {
        printf("  %03d:", i);
        for(int j = i; j < i + 16 && j < JK_FRAME_SIZE; j++) printf(" %02X", b[j]);
        printf("\n");
    }
}

static void dump() {
    BmsSnapshot d;
    bmsDataGet(d);
    printf("=== DUMP %s (%s) proto=%s model=%s hw=%s sw=%s ===\n", d.addr, d.name, jkProtoName(d.proto), d.info.model,
           d.info.hw, d.info.sw);
    static uint8_t f[JK_FRAME_SIZE];
    static const struct {
        uint8_t type;
        const char *title;
    } frames[] = {{JK_FRAME_INFO, "Device info frame (0x03)"},
                  {JK_FRAME_SETTINGS, "Settings frame (0x01)"},
                  {JK_FRAME_CELLS, "Cell info frame (0x02)"}};
    for(auto &fr : frames) {
        if(bmsDataGetRaw(fr.type, f)) {
            dumpHex(fr.title, f);
        } else {
            printf("%s: not received\n", fr.title);
        }
    }
    printf("=== END DUMP ===\n");
}

// set <key> <value>, or just set: lists the settings
static void setCmd(char *arg) {
    BmsSnapshot d;
    bmsDataGet(d);
    char *val = strchr(arg, ' ');
    if(val) *val++ = '\0';
    JkSetting s = jkSettingByKey(arg);
    if(s == JkSetting::Count || !val) {
        printf("set <key> <value>  (%s)\n", jkProtoName(d.proto));
        for(int i = 0; i < static_cast<int>(JkSetting::Count); i++) {
            JkSetting k = static_cast<JkSetting>(i);
            if(!jkSettingSupported(d.proto, k)) continue;
            const JkSettingInfo &in = jkSettingInfo(k);
            printf("  %-10s %-22s now %g %s  (%g..%g)\n", in.key, in.label,
                   d.settings.valid ? jkSettingValue(d.settings, k) : 0.0f, in.unit, in.min, in.max);
        }
        return;
    }
    float v = strtof(val, nullptr);
    if(!strcmp(val, "on")) v = 1;
    if(!strcmp(val, "off")) v = 0;
    printf(bleWriteSetting(s, v) ? "Sent\n" : "Not sent (not connected, not on this device, or out of range)\n");
}

// wreg <reg hex> <len> <value>: value with a '.' is sent as a float
static void wregCmd(char *arg) {
    char *len = strchr(arg, ' ');
    char *val = len ? strchr(len + 1, ' ') : nullptr;
    if(!len || !val) {
        printf("wreg <reg hex> <len> <value>   e.g. wreg 64 4 0.05 (float), wreg 6c 1 1\n");
        return;
    }
    *len++ = '\0';
    *val++ = '\0';
    uint32_t v;
    if(strchr(val, '.')) {
        float f = strtof(val, nullptr);
        memcpy(&v, &f, sizeof(v));
    } else {
        v = static_cast<uint32_t>(strtol(val, nullptr, 0));
    }
    bool ok = bleWriteRaw(static_cast<uint8_t>(strtol(arg, nullptr, 16)), static_cast<uint8_t>(atoi(len)), v);
    printf(ok ? "Sent\n" : "Not connected\n");
}

static void run(char *line) {
    char *arg = strchr(line, ' ');
    if(arg) {
        *arg++ = '\0';
        while(*arg == ' ') arg++;
    } else {
        arg = line + strlen(line);
    }
    if(!strcmp(line, "status") || !strcmp(line, "list")) {
        printStatus();
    } else if(!strcmp(line, "scan")) {
        uiShowPicker();
        printf("Scanning; `found` lists the devices, `select <n>` picks one\n");
    } else if(!strcmp(line, "found")) {
        printFound();
    } else if(!strcmp(line, "select") || !strcmp(line, "add")) {
        select(arg);
        uiShowMain();
    } else if(!strcmp(line, "cancel")) {
        blePickCancel();
        uiShowMain();
    } else if(!strcmp(line, "forget")) {
        bleForget();
        uiShowPicker();
        printf("Forgotten\n");
    } else if(!strcmp(line, "dump")) {
        dump();
    } else if(!strcmp(line, "shot")) {
        uiScreenshot(*arg ? arg : "screen");
    } else if(!strcmp(line, "set")) {
        setCmd(arg);
    } else if(!strcmp(line, "rename")) {
        if(bleRename(arg)) {
            printf("Sent\n");
        } else {
            printf("rename <name>: 1..%d printable ASCII characters, while connected\n", JK_NAME_MAX);
        }
    } else if(!strcmp(line, "tap")) {
        int x = 0, y = 0, ms = 100;
        if(sscanf(arg, "%d %d %d", &x, &y, &ms) >= 2) {
            displayTap(x, y, ms);
        } else {
            printf("tap <x> <y> [ms]: presses the screen (hold with ms >= 400 for a long press)\n");
        }
    } else if(!strcmp(line, "settings")) {
        uiShowSettings();
    } else if(!strcmp(line, "wreg")) {
        wregCmd(arg);
    } else if(!strcmp(line, "page")) {
        uiGoToPage(atoi(arg));
    } else {
        printf("Commands: status | scan | found | select <n|mac> | cancel | forget | dump | set [<key> <value>] | rename <name> | wreg <reg> <len> <value> | page <n> | settings | tap <x> <y> [ms] | shot [name]\n");
    }
}

static void consoleTask(void *) {
    char line[64];
    size_t len = 0;
    for(;;) {
        uint8_t c;
        if(uart_read_bytes(UART_NUM_0, &c, 1, portMAX_DELAY) != 1) continue;
        if(c != '\n' && c != '\r') {
            if(len < sizeof(line) - 1 && isprint(c)) line[len++] = static_cast<char>(c);
            continue;
        }
        line[len] = '\0';
        len = 0;
        if(line[0]) run(line);
    }
}

void consoleStart() {
    esp_err_t err = uart_driver_install(UART_NUM_0, 256, 0, 0, nullptr, 0);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "UART driver: %s", esp_err_to_name(err));
        return;
    }
    xTaskCreate(consoleTask, "console", 4096, nullptr, 2, nullptr);
}
