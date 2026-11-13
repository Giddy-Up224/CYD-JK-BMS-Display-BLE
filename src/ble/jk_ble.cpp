#include "jk_ble.h"
#include <stdio.h>
#include <string.h>
#include "bms/bms_data.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "jk/jk_proto.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "prefs/prefs.h"

static const char *TAG = "BLE";

static const ble_uuid16_t UUID_SVC = {{BLE_UUID_TYPE_16}, 0xFFE0};
static const uint16_t UUID_CHR = 0xFFE1;  // notify (replies) + write (requests)
static const uint16_t UUID_CCCD = 0x2902;

static const int CONNECT_TIMEOUT_MS = 10000;
static const int SETUP_TIMEOUT_MS = 20000;   // connect + discovery
static const int64_t SILENT_US = 20 * 1000000LL;   // no notifications: reconnect
static const int64_t REQUEST_US = 5 * 1000000LL;   // no cell frame yet: ask again
static const int BACKOFF_MIN_MS = 1000;
static const int BACKOFF_MAX_MS = 30000;
static const int LOG_EVERY = 10;  // cell frames between log lines
static const int MAX_RESULTS = 20;

// Event bits
static const EventBits_t SYNCED = BIT0;  // NimBLE host ready
static const EventBits_t READY = BIT1;   // connected, notifications on
static const EventBits_t DOWN = BIT2;    // connect failed or link dropped
static const EventBits_t WAKE = BIT3;    // mode or target changed

enum class Mode : uint8_t { Pick, Run };

static EventGroupHandle_t events_ = nullptr;
static portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;  // target_ and results_
static uint8_t ownAddrType_ = BLE_OWN_ADDR_PUBLIC;
static volatile Mode mode_ = Mode::Pick;
static SavedDevice target_ = {};
static bool haveTarget_ = false;

// Connection, written by the host task
static volatile uint16_t conn_ = BLE_HS_CONN_HANDLE_NONE;
static uint16_t svcStart_, svcEnd_, chrHandle_, chrDef_, chrEnd_, cccd_;

// Frame decoding, host task only (reset by the link task before connecting)
static JkAssembler assembler_;
static JkProto proto_ = JkProto::Unknown;
static int hwMajor_ = 0;
static uint8_t settingsRaw_[JK_FRAME_SIZE];
static bool settingsPending_ = false;  // settings arrived before the layout was known
static uint32_t cellFrames_ = 0;
static volatile int64_t lastNotifyUs_ = 0;
static volatile int64_t lastCellsUs_ = 0;

// Picker results
static ScanEntry results_[MAX_RESULTS];
static int resultCount_ = 0;

void bleAddrStr(const uint8_t addr[6], char out[18]) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

bool bleAddrParse(const char *s, uint8_t addr[6]) {
    unsigned v[6];
    if(sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
    for(int i = 0; i < 6; i++) {
        if(v[i] > 0xFF) return false;
        addr[5 - i] = static_cast<uint8_t>(v[i]);
    }
    return true;
}

static void fail(const char *step, int status) {
    ESP_LOGW(TAG, "%s failed (%d), disconnecting", step, status);
    if(conn_ != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn_, BLE_ERR_REM_USER_CONN_TERM);
}

// --- Setup after connecting: MTU, discovery, notifications. Runs in the host
// task, each step started from the previous one's callback.

static int onCccdWritten(uint16_t, const ble_gatt_error *error, ble_gatt_attr *, void *) {
    if(error->status != 0) {
        fail("Enabling notifications", error->status);
        return 0;
    }
    ESP_LOGI(TAG, "Ready (MTU %u)", ble_att_mtu(conn_));
    xEventGroupSetBits(events_, READY);
    return 0;
}

static int onDsc(uint16_t conn, const ble_gatt_error *error, uint16_t, const ble_gatt_dsc *dsc, void *) {
    if(error->status == 0) {
        if(!cccd_ && dsc->handle > chrHandle_ && ble_uuid_u16(&dsc->uuid.u) == UUID_CCCD) cccd_ = dsc->handle;
        return 0;
    }
    if(error->status != BLE_HS_EDONE) {
        fail("Descriptor discovery", error->status);
        return 0;
    }
    if(!cccd_) {
        fail("Finding FFE1's notify switch", 0);
        return 0;
    }
    static const uint8_t on[2] = {0x01, 0x00};
    int rc = ble_gattc_write_flat(conn, cccd_, on, sizeof(on), onCccdWritten, nullptr);
    if(rc) fail("Enabling notifications", rc);
    return 0;
}

static int onChr(uint16_t conn, const ble_gatt_error *error, const ble_gatt_chr *chr, void *) {
    if(error->status == 0) {
        // FFE1's descriptors end where the next characteristic starts
        if(chrDef_ && !chrEnd_ && chr->def_handle > chrDef_) chrEnd_ = chr->def_handle - 1;
        if(!chrHandle_ && ble_uuid_u16(&chr->uuid.u) == UUID_CHR) {
            chrHandle_ = chr->val_handle;
            chrDef_ = chr->def_handle;
        }
        return 0;
    }
    if(error->status != BLE_HS_EDONE) {
        fail("Characteristic discovery", error->status);
        return 0;
    }
    if(!chrHandle_) {
        fail("Finding FFE1", 0);
        return 0;
    }
    uint16_t end = chrEnd_ ? chrEnd_ : svcEnd_;
    int rc = ble_gattc_disc_all_dscs(conn, chrHandle_, end, onDsc, nullptr);
    if(rc) fail("Descriptor discovery", rc);
    return 0;
}

static int onSvc(uint16_t conn, const ble_gatt_error *error, const ble_gatt_svc *svc, void *) {
    if(error->status == 0) {
        svcStart_ = svc->start_handle;
        svcEnd_ = svc->end_handle;
        return 0;
    }
    if(error->status != BLE_HS_EDONE || !svcEnd_) {
        fail("Finding service FFE0", error->status);
        return 0;
    }
    int rc = ble_gattc_disc_all_chrs(conn, svcStart_, svcEnd_, onChr, nullptr);
    if(rc) fail("Characteristic discovery", rc);
    return 0;
}

static int onMtu(uint16_t conn, const ble_gatt_error *error, uint16_t mtu, void *) {
    // a refused MTU exchange is fine: frames then come in smaller pieces
    if(error->status == 0) ESP_LOGI(TAG, "MTU %u", mtu);
    svcStart_ = svcEnd_ = chrHandle_ = chrDef_ = chrEnd_ = cccd_ = 0;
    int rc = ble_gattc_disc_svc_by_uuid(conn, &UUID_SVC.u, onSvc, nullptr);
    if(rc) fail("Finding service FFE0", rc);
    return 0;
}

// --- Scan results

// Walks the AD structures of an advertisement: length, type, data
template <typename F>
static void eachAd(const uint8_t *data, size_t length, F f) {
    for(size_t i = 0; i + 1 < length;) {
        size_t len = data[i];
        if(len == 0 || i + 1 + len > length) break;
        f(data[i + 1], data + i + 2, len - 1);
        i += 1 + len;
    }
}

// JK devices put their own address in the last 6 bytes of the manufacturer
// data (e.g. 650b88a0 c84780234f95 for C8:47:80:23:4F:95), are named "JK..."
// or advertise the FFE0 service.
static bool looksLikeJk(const ble_gap_disc_desc &d, const char *name) {
    bool jk = strncmp(name, "JK", 2) == 0;
    eachAd(d.data, d.length_data, [&](uint8_t type, const uint8_t *p, size_t n) {
        if(type == 0xFF && n >= 6) {
            const uint8_t *m = p + n - 6;
            bool same = true;
            for(int i = 0; i < 6; i++) same = same && m[i] == d.addr.val[5 - i];
            if(same) jk = true;
        } else if(type == 0x02 || type == 0x03) {
            for(size_t i = 0; i + 1 < n; i += 2) {
                if((p[i] | (p[i + 1] << 8)) == 0xFFE0) jk = true;
            }
        }
    });
    return jk;
}

static void onScanResult(const ble_gap_disc_desc &d) {
    char name[sizeof(ScanEntry::name)] = "";
    eachAd(d.data, d.length_data, [&](uint8_t type, const uint8_t *p, size_t n) {
        if(type == 0x09 || (type == 0x08 && !name[0])) {
            if(n > sizeof(name) - 1) n = sizeof(name) - 1;
            memcpy(name, p, n);
            name[n] = '\0';
        }
    });
    bool jk = looksLikeJk(d, name);
    bool added = false;
    portENTER_CRITICAL(&mux_);
    int i = 0;
    while(i < resultCount_ && memcmp(results_[i].addr, d.addr.val, 6) != 0) i++;
    if(i < resultCount_) {
        // known device: signal, and the name if this packet (e.g. the scan
        // response) carries it
        results_[i].rssi = d.rssi;
        if(name[0]) strlcpy(results_[i].name, name, sizeof(results_[i].name));
    } else if(jk && resultCount_ < MAX_RESULTS) {
        ScanEntry &e = results_[resultCount_++];
        e.addrType = d.addr.type;
        memcpy(e.addr, d.addr.val, 6);
        strlcpy(e.name, name, sizeof(e.name));
        e.rssi = d.rssi;
        added = true;
    }
    portEXIT_CRITICAL(&mux_);
    if(added) {
        char a[18];
        bleAddrStr(d.addr.val, a);
        ESP_LOGI(TAG, "Found %s (%s, type %u) at %d dBm", name[0] ? name : "(no name)", a, d.addr.type, d.rssi);
    }
}

// --- Frames from the device (host task)

static void logCells(const JkCells &c) {
    if(c.hasBms) {
        ESP_LOGI(TAG, "%.2f V  %.2f A  SOC %d%%  %d cells  delta %.3f V  balance %.3f A (%u)", c.packV, c.currentA, c.soc,
                 c.cellCount, c.deltaV, c.balanceA, c.balancing);
    } else {
        ESP_LOGI(TAG, "%.2f V  %d cells  avg %.3f V  delta %.3f V  balance %.3f A (%u)", c.packV, c.cellCount, c.avgV, c.deltaV,
                 c.balanceA, c.balancing);
    }
}

static void onFrame(const uint8_t *f) {
    uint8_t type = f[4];
    bmsDataPutRaw(type, f);
    switch(type) {
    case JK_FRAME_INFO: {
        JkInfo info;
        jkParseInfo(f, info);
        hwMajor_ = info.hwMajor;
        ESP_LOGI(TAG, "Model %s  hw %s  sw %s  serial %s  name %s", info.model, info.hw, info.sw, info.serial, info.name);
        bmsDataSetInfo(info);
        break;
    }
    case JK_FRAME_SETTINGS:
        memcpy(settingsRaw_, f, JK_FRAME_SIZE);
        settingsPending_ = true;
        break;
    case JK_FRAME_CELLS: {
        lastCellsUs_ = esp_timer_get_time();
        if(proto_ == JkProto::Unknown) {
            proto_ = jkDetect(f, hwMajor_);
            ESP_LOGI(TAG, "Protocol %s (hw major %d)", jkProtoName(proto_), hwMajor_);
            bmsDataSetProto(proto_);
        }
        JkCells cells;
        jkParseCells(proto_, f, cells);
        bmsDataPutCells(cells);
        if(cellFrames_++ % LOG_EVERY == 0) logCells(cells);
        break;
    }
    default:
        ESP_LOGW(TAG, "Unknown frame type 0x%02X", type);
        break;
    }
    // the settings layout depends on the protocol, known from the first cell frame
    if(settingsPending_ && proto_ != JkProto::Unknown) {
        settingsPending_ = false;
        JkSettings s;
        jkParseSettings(proto_, settingsRaw_, s);
        ESP_LOGI(TAG, "Settings: %d cells  start %.3f V  trigger %.3f V  max balance %.3f A  balancing %s", s.cellCount,
                 s.startBalanceV, s.triggerDeltaV, s.maxBalanceA, s.balanceEnabled ? "on" : "off");
        bmsDataSetSettings(s);
    }
}

static void onNotify(uint16_t attr, os_mbuf *om) {
    if(attr != chrHandle_) return;
    lastNotifyUs_ = esp_timer_get_time();
    uint8_t buf[JK_FRAME_SIZE];
    uint16_t len = OS_MBUF_PKTLEN(om);
    if(len > sizeof(buf)) len = sizeof(buf);
    if(os_mbuf_copydata(om, 0, len, buf) != 0) return;
    if(assembler_.feed(buf, len)) onFrame(assembler_.frame());
}

static int onGap(ble_gap_event *event, void *) {
    switch(event->type) {
    case BLE_GAP_EVENT_DISC:
        onScanResult(event->disc);
        break;
    case BLE_GAP_EVENT_CONNECT:
        if(event->connect.status == 0) {
            conn_ = event->connect.conn_handle;
            ESP_LOGI(TAG, "Connected");
            int rc = ble_gattc_exchange_mtu(conn_, onMtu, nullptr);
            if(rc) fail("MTU exchange", rc);
        } else {
            ESP_LOGW(TAG, "Connect failed (%d)", event->connect.status);
            xEventGroupSetBits(events_, DOWN);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "Disconnected (reason 0x%X)", event->disconnect.reason);
        conn_ = BLE_HS_CONN_HANDLE_NONE;
        xEventGroupClearBits(events_, READY);
        xEventGroupSetBits(events_, DOWN);
        break;
    case BLE_GAP_EVENT_NOTIFY_RX:
        onNotify(event->notify_rx.attr_handle, event->notify_rx.om);
        break;
    default:
        break;
    }
    return 0;
}

// --- Link task: picker scan, connecting, watching the stream

static void startScan() {
    ble_gap_disc_params params = {};
    params.passive = 0;            // active: the name may be in the scan response
    params.filter_duplicates = 0;  // keep the signal strength current
    int rc = ble_gap_disc(ownAddrType_, BLE_HS_FOREVER, &params, onGap, nullptr);
    if(rc && rc != BLE_HS_EALREADY) ESP_LOGW(TAG, "Scan start failed (%d)", rc);
}

static void disconnect() {
    if(ble_gap_conn_active()) ble_gap_conn_cancel();
    if(conn_ != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn_, BLE_ERR_REM_USER_CONN_TERM);
}

static bool sendCommand(uint8_t cmd) {
    uint16_t conn = conn_;
    if(conn == BLE_HS_CONN_HANDLE_NONE) return false;
    uint8_t req[20];
    size_t n = jkBuildCommand(cmd, req);
    int rc = ble_gattc_write_no_rsp_flat(conn, chrHandle_, req, n);
    if(rc) ESP_LOGW(TAG, "Write of command 0x%02X failed (%d)", cmd, rc);
    return rc == 0;
}

// Waits up to ms, returning early when the mode or target changes
static void pause(int ms) {
    xEventGroupWaitBits(events_, WAKE, pdTRUE, pdFALSE, pdMS_TO_TICKS(ms));
}

static void linkTask(void *) {
    xEventGroupWaitBits(events_, SYNCED, pdFALSE, pdFALSE, portMAX_DELAY);
    int backoff = BACKOFF_MIN_MS;
    int64_t requestedUs = 0;

    for(;;) {
        if(mode_ == Mode::Pick) {
            if(conn_ != BLE_HS_CONN_HANDLE_NONE) disconnect();
            bmsDataSetLink(LinkState::Picking);
            if(!ble_gap_disc_active()) startScan();
            pause(500);
            continue;
        }
        if(ble_gap_disc_active()) ble_gap_disc_cancel();

        portENTER_CRITICAL(&mux_);
        bool have = haveTarget_;
        SavedDevice target = target_;
        portEXIT_CRITICAL(&mux_);
        if(!have) {
            bmsDataSetLink(LinkState::NoDevice);
            pause(1000);
            continue;
        }

        if(conn_ == BLE_HS_CONN_HANDLE_NONE) {
            bmsDataSetLink(LinkState::Connecting);
            xEventGroupClearBits(events_, READY | DOWN | WAKE);
            // nothing arrives while disconnected: safe to reset the host task's state
            assembler_.reset();
            proto_ = JkProto::Unknown;
            hwMajor_ = 0;
            settingsPending_ = false;
            cellFrames_ = 0;
            bmsDataClear();

            ble_addr_t addr;
            addr.type = target.addrType;
            memcpy(addr.val, target.addr, 6);
            char a[18];
            bleAddrStr(target.addr, a);
            ESP_LOGI(TAG, "Connecting to %s (%s)", target.name[0] ? target.name : "?", a);
            int rc = ble_gap_connect(ownAddrType_, &addr, CONNECT_TIMEOUT_MS, nullptr, onGap, nullptr);
            EventBits_t bits = 0;
            if(rc == 0) {
                bits = xEventGroupWaitBits(events_, READY | DOWN | WAKE, pdFALSE, pdFALSE, pdMS_TO_TICKS(SETUP_TIMEOUT_MS));
            } else {
                ESP_LOGW(TAG, "Connect start failed (%d)", rc);
            }
            if(!(bits & READY)) {
                disconnect();
                if(bits & WAKE) continue;  // mode or target changed meanwhile
                ESP_LOGI(TAG, "Retrying in %d s", backoff / 1000);
                pause(backoff);
                backoff = backoff * 2 > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : backoff * 2;
                continue;
            }
            backoff = BACKOFF_MIN_MS;
            bmsDataSetLink(LinkState::Connected);
            lastNotifyUs_ = lastCellsUs_ = 0;
            // device info first (it tells the layout apart), then the stream
            sendCommand(JK_CMD_DEVICE_INFO);
            vTaskDelay(pdMS_TO_TICKS(300));
            sendCommand(JK_CMD_CELL_INFO);
            requestedUs = esp_timer_get_time();
        }

        EventBits_t bits = xEventGroupWaitBits(events_, WAKE | DOWN, pdFALSE, pdFALSE, pdMS_TO_TICKS(1000));
        if(bits & WAKE) {
            xEventGroupClearBits(events_, WAKE);
            if(mode_ == Mode::Pick) continue;  // disconnects at the top
        }
        if(conn_ == BLE_HS_CONN_HANDLE_NONE) continue;  // dropped: reconnect

        int64_t now = esp_timer_get_time();
        int64_t lastNotify = lastNotifyUs_;
        if(now - (lastNotify ? lastNotify : requestedUs) > SILENT_US) {
            ESP_LOGW(TAG, "No data for %lld s, reconnecting", SILENT_US / 1000000);
            disconnect();
            xEventGroupWaitBits(events_, DOWN, pdFALSE, pdFALSE, pdMS_TO_TICKS(2000));
            continue;
        }
        if(lastCellsUs_ == 0 && now - requestedUs > REQUEST_US) {
            ESP_LOGI(TAG, "No cell info yet, asking again");
            sendCommand(JK_CMD_CELL_INFO);
            requestedUs = now;
        }
    }
}

// --- NimBLE host

static void onSync() {
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &ownAddrType_);
    xEventGroupSetBits(events_, SYNCED);
}

static void onReset(int reason) {
    ESP_LOGE(TAG, "NimBLE reset (%d)", reason);
}

static void hostTask(void *) {
    nimble_port_run();  // returns only on nimble_port_stop()
    nimble_port_freertos_deinit();
}

static void wake() {
    xEventGroupSetBits(events_, WAKE);
}

static void publishTarget(const SavedDevice &d) {
    char a[18];
    bleAddrStr(d.addr, a);
    bmsDataSetDevice(d.name, a);
}

void bleStart() {
    events_ = xEventGroupCreate();
    SavedDevice saved;
    if(prefsGetDevice(saved)) {
        target_ = saved;
        haveTarget_ = true;
        mode_ = Mode::Run;
        publishTarget(saved);
    } else {
        mode_ = Mode::Pick;
    }

    esp_err_t err = nimble_port_init();
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE init: %s", esp_err_to_name(err));
        return;
    }
    ble_hs_cfg.sync_cb = onSync;
    ble_hs_cfg.reset_cb = onReset;
    nimble_port_freertos_init(hostTask);
    xTaskCreate(linkTask, "jk_link", 6144, nullptr, 4, nullptr);
}

void blePickStart() {
    portENTER_CRITICAL(&mux_);
    resultCount_ = 0;
    portEXIT_CRITICAL(&mux_);
    mode_ = Mode::Pick;
    wake();
}

bool blePicking() {
    return mode_ == Mode::Pick;
}

int blePickResults(ScanEntry *out, int max) {
    portENTER_CRITICAL(&mux_);
    int n = resultCount_ < max ? resultCount_ : max;
    memcpy(out, results_, n * sizeof(ScanEntry));
    portEXIT_CRITICAL(&mux_);
    // strongest first (insertion sort, a handful of entries)
    for(int i = 1; i < n; i++) {
        ScanEntry e = out[i];
        int j = i - 1;
        while(j >= 0 && out[j].rssi < e.rssi) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = e;
    }
    return n;
}

void bleSelect(const ScanEntry &entry) {
    SavedDevice d = {};
    d.addrType = entry.addrType;
    memcpy(d.addr, entry.addr, 6);
    strlcpy(d.name, entry.name, sizeof(d.name));
    prefsSetDevice(d);
    portENTER_CRITICAL(&mux_);
    target_ = d;
    haveTarget_ = true;
    portEXIT_CRITICAL(&mux_);
    publishTarget(d);
    mode_ = Mode::Run;
    disconnect();  // from a previous device, if still up
    wake();
}

void blePickCancel() {
    mode_ = Mode::Run;
    wake();
}

void bleForget() {
    prefsForgetDevice();
    portENTER_CRITICAL(&mux_);
    haveTarget_ = false;
    portEXIT_CRITICAL(&mux_);
    bmsDataSetDevice("", "");
    bmsDataClear();
    blePickStart();
}
