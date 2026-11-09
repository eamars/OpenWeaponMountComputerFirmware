#include <math.h>
#include <string.h>
#include "common.h"
#include "app_cfg.h"

#include "nvs.h"
#include "esp_check.h"
#include "esp_crc.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include <inttypes.h>
#include <stdio.h>


#define TAG "Common"
// Passed as esp_crc32_le's initial value (the polynomial is built in). The name used to say "polynomial"; the value
// stays, because every saved setting carries a CRC made with it
#define CRC32_INIT 0xEDB88320

uint32_t crc32_wrapper(void * data, size_t length, size_t offset) {
    uint8_t * data_start_addr = (uint8_t *) data + offset;
    size_t crc_length = length - offset;

    return esp_crc32_le(CRC32_INIT, data_start_addr, crc_length);
}



float wrap_angle(float rad) {
    rad = fmodf(rad + M_PI, 2 * M_PI);
    if (rad < 0) rad += 2 * M_PI;
    return rad - M_PI;
}


esp_err_t parse_git_describe_version(const char *describe, version_t * version) {
    // Captures formats like v1.0.0-dirty, v1.0.0 or 1.0.0; anything else is an error, so the
    // caller never compares unset fields (ADR-005 B14)
    if (describe[0] == 'v' || describe[0] == 'V') {
        describe++;
    }
    if (sscanf(describe, "%d.%d.%d", &version->major, &version->minor, &version->patch) != 3) {
        return ESP_ERR_INVALID_VERSION;
    }

    return ESP_OK;
}

int compare_version(version_t *self, version_t *other) {
    if (self->major != other->major) return (self->major > other->major) ? 1 : -1;
    if (self->minor != other->minor) return (self->minor > other->minor) ? 1 : -1;
    if (self->patch != other->patch) return (self->patch > other->patch) ? 1 : -1;

    return 0; // versions are equal
}


esp_err_t load_config(const char *ns, void *cfg, const void *default_cfg, size_t size) {
    return load_config_grown(ns, cfg, default_cfg, size, size);
}


esp_err_t load_config_grown(const char *ns, void *cfg, const void *default_cfg, size_t size, size_t min_size) {
    esp_err_t ret;

    // Open NVS partition
    nvs_handle_t handle;
    ret = nvs_open(ns, NVS_READWRITE, &handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "NS: %s, Failed to nvs_open: %s", ns, esp_err_to_name(ret));

    // Allocate memory for data buffer, this step will have additional space at the end of the structure
    size_t read_size = size + sizeof(uint32_t);
    uint8_t * buf = heap_caps_malloc(read_size, HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);

    if (!buf) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    ret = nvs_get_blob(handle, NVS_KEY_NAME, buf, &read_size);
    nvs_close(handle);
    
    if (ret == ESP_OK) {
        // do nothing
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND || ret == ESP_ERR_NVS_INVALID_LENGTH) {
        ESP_LOGI(TAG, "NS: %s, NVS Not Found. Will initialize with default values", ns);

        // Populate the default value and write to NVS
        heap_caps_free(buf);
        memcpy(cfg, default_cfg, size);
        return save_config(ns, cfg, size);
    }
    else {
        // if ret != ESP_OK then we cannot handle the error
        heap_caps_free(buf);
        ESP_LOGE(TAG, "NS: %s, Failed to read NVS blob: %s", ns, esp_err_to_name(ret));
        return ret;
    }

    // The stored size: the structure's, or an older, shorter one it grew from (min_size up to size)
    size_t stored_size = read_size >= sizeof(uint32_t) ? read_size - sizeof(uint32_t) : 0;
    bool size_ok = stored_size >= min_size && stored_size <= size;

    // Verify CRC
    uint32_t calculated_crc32 = 0;
    uint32_t received_crc32 = 0;

    if (size_ok) {
        calculated_crc32 = crc32_wrapper(buf, stored_size, 0);
        memcpy(&received_crc32, buf + stored_size, sizeof(received_crc32));
    }

    if (!size_ok || calculated_crc32 != received_crc32) {
        ESP_LOGW(TAG, "NS: %s, CRC32 mismatch. Expected 0x%08x, Received 0x%08x Will use default configuration", ns, calculated_crc32, received_crc32);

        heap_caps_free(buf);
        memcpy(cfg, default_cfg, size);
        
        return save_config(ns, cfg, size);
    }
    else {
        // copy content from data buffer; what an older, shorter structure lacks takes the defaults (saved with the
        // next save)
        memcpy(cfg, default_cfg, size);
        memcpy(cfg, buf, stored_size);
        heap_caps_free(buf);
        if (stored_size < size) {
            ESP_LOGI(TAG, "NS: %s, Configuration read from an older %u-byte version, the rest default", ns,
                     (unsigned) stored_size);
        }
        else {
            ESP_LOGI(TAG, "NS: %s, Configuration read successfully", ns);
        }
    }

    return ESP_OK;
}


void unit_number(char *buf, size_t size) {
    // As the Kestrel HUD name has been since ADR-007, so a unit paired with a Kestrel keeps its name
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    uint32_t number = ((uint32_t) mac[3] << 16 | mac[4] << 8 | mac[5]) % 10000000;
    snprintf(buf, size, "%07" PRIu32, number);
}


esp_err_t save_config(const char *ns, void *cfg, size_t size) {
    esp_err_t ret;

    // Open NVS partition
    nvs_handle_t handle;
    ret = nvs_open(ns, NVS_READWRITE, &handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "NS: %s, Failed to nvs_open: %s", ns, esp_err_to_name(ret));

    // Calculate crc
    uint32_t calculated_crc32 = 0;
    calculated_crc32 = crc32_wrapper(cfg, size, 0);

    // Append data to data buffer
    size_t write_size = size + sizeof(uint32_t);
    uint8_t * buf = heap_caps_malloc(write_size, HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);
    if (!buf) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    memcpy(buf, cfg, size);
    memcpy(buf + size, &calculated_crc32, sizeof(calculated_crc32));

    // Write to NVS
    ret = nvs_set_blob(handle, NVS_KEY_NAME, buf, write_size);
    ESP_GOTO_ON_ERROR(ret, finally, TAG, "NS: %s, Failed to nvs_set_blob: %s", ns, esp_err_to_name(ret));

    ret = nvs_commit(handle);
    ESP_GOTO_ON_ERROR(ret, finally, TAG, "NS: %s, Failed to nvs_commit: %s", ns, esp_err_to_name(ret));

    ESP_LOGI(TAG, "NS: %s, Configuration write successfully", ns);
    
finally:
    heap_caps_free(buf);
    nvs_close(handle);

    return ret;
}