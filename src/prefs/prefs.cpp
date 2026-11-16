#include "prefs.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "Prefs";
static const char *NS = "jkbms";
static const char *KEY_DEVICE = "device";

bool prefsGetDevice(SavedDevice &out) {
    nvs_handle_t h;
    if(nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t size = sizeof(out);
    esp_err_t err = nvs_get_blob(h, KEY_DEVICE, &out, &size);
    nvs_close(h);
    if(err != ESP_OK || size != sizeof(out)) return false;
    out.name[sizeof(out.name) - 1] = '\0';
    return true;
}

void prefsSetDevice(const SavedDevice &device) {
    nvs_handle_t h;
    if(nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    esp_err_t err = nvs_set_blob(h, KEY_DEVICE, &device, sizeof(device));
    if(err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if(err != ESP_OK) ESP_LOGE(TAG, "Saving the device: %s", esp_err_to_name(err));
}

void prefsForgetDevice() {
    nvs_handle_t h;
    if(nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, KEY_DEVICE);
    nvs_commit(h);
    nvs_close(h);
}
