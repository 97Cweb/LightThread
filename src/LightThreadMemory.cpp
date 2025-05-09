#include "LightThread.h"

#include "nvs_flash.h"
#include "nvs.h"

bool LightThread::saveNetworkInfo() {
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvsHandle);
    if (err != ESP_OK) return false;

    // Save networkKey
    if (nvs_set_str(nvsHandle, "network_key", networkKey.c_str()) != ESP_OK) {
        nvs_close(nvsHandle);
        return false;
    }

    // Save panid
    if (nvs_set_str(nvsHandle, "pan_id", panid.c_str()) != ESP_OK) {
        nvs_close(nvsHandle);
        return false;
    }

    // Save channel
    if (nvs_set_i32(nvsHandle, "channel", channel) != ESP_OK) {
        nvs_close(nvsHandle);
        return false;
    }

    nvs_commit(nvsHandle);
    nvs_close(nvsHandle);
    return true;
}

bool LightThread::saveLeaderIp() {
    log_i("Saving leader IP: %s", leaderIp.c_str());
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvsHandle);
    if (err != ESP_OK) {
        log_w("Failed to open NVS: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_str(nvsHandle, "leader_ip", leaderIp.c_str());
    if (err != ESP_OK) {
        log_w("Failed to save leader IP: %s", esp_err_to_name(err));
        nvs_close(nvsHandle);
        return false;
    }

	nvs_commit(nvsHandle);
    nvs_close(nvsHandle);
    log_i("Leader IP successfully saved.");
    return true;
}



bool LightThread::loadInfo() {
    nvs_handle_t nvsHandle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &nvsHandle);
    if (err != ESP_OK) {
        log_w("Failed to open NVS for reading.");
        return false;
    }

    char buffer[64];
    size_t length = sizeof(buffer);

    // Load networkKey
    if (nvs_get_str(nvsHandle, "network_key", buffer, &length) == ESP_OK) {
        networkKey = String(buffer);
        log_d("Loaded Network Key: %s", networkKey.c_str());
    } else {
        log_w("Network Key not found.");
        nvs_close(nvsHandle);
        return false;
    }

    // Load panid
    length = sizeof(buffer);
    if (nvs_get_str(nvsHandle, "pan_id", buffer, &length) == ESP_OK) {
        panid = String(buffer);
        log_d("Loaded PAN ID: %s", panid.c_str());
    } else {
        log_w("PAN ID not found.");
        nvs_close(nvsHandle);
        return false;
    }

    // Load channel
    int32_t storedChannel;
    if (nvs_get_i32(nvsHandle, "channel", &storedChannel) == ESP_OK) {
        channel = storedChannel;
        log_d("Loaded Channel: %d", channel);
    } else {
        log_w("Channel not found.");
        nvs_close(nvsHandle);
        return false;
    }
	
	// Load leader IP	
	length = sizeof(buffer);
    err = nvs_get_str(nvsHandle, "leader_ip", buffer, &length);
    if (err == ESP_OK) {
        leaderIp = String(buffer);
        log_i("Loaded Leader IP: %s", leaderIp.c_str());
    } else {
        log_w("Failed to load Leader IP: %s", esp_err_to_name(err));
		nvs_close(nvsHandle);
		return false;
    }

    nvs_close(nvsHandle);
    return true;
}


void LightThread::clearNetworkInfo() {
    nvs_flash_init();
    nvs_handle_t nvsHandle;
    nvs_open("storage", NVS_READWRITE, &nvsHandle);
    nvs_erase_all(nvsHandle);
    nvs_commit(nvsHandle);
    nvs_close(nvsHandle);
}