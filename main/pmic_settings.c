#include "pmic_axp2101.h"

#include "app_cfg.h"
#include "common.h"

// The PMIC service's stored settings: charging (applied by the service) and the idle and power-off timeouts (used by
// low power). The Power page (pmic_config.c) shows and edits them; the service owns them and loads them at its init.

#define NVS_NAMESPACE "PMG"

HEAPS_CAPS_ATTR power_management_config_t power_management_config;
const power_management_config_t default_power_management_config = {
    .vbus_current_limit = PMIC_VBUS_CURRENT_LIMIT_1000MA,  // the running unit shares it with the charge (ADR-009)
    .battery_charge_current = PMIC_BATTERY_CHARGE_CURRENT_500MA,
    .battery_charge_voltage = PMIC_BATTERY_CHARGE_VOLTAGE_4V2,
    .idle_timeout = IDLE_TIMEOUT_5_MIN,
    .sleep_timeout = SLEEP_TIMEOUT_5_HRS,  // a long wait between stages ends idle, not off (ADR-009)
};



esp_err_t save_pmic_config() {
    return save_config(NVS_NAMESPACE, &power_management_config, sizeof(power_management_config));
}


esp_err_t load_pmic_config() {
    return load_config(NVS_NAMESPACE, &power_management_config, &default_power_management_config, sizeof(power_management_config));
}

uint32_t idle_timeout_to_secs(idle_timeout_t timeout) {
    switch (timeout) {
        case IDLE_TIMEOUT_NEVER:
            return 0;
        case IDLE_TIMEOUT_1_MIN:
            return 60;
        case IDLE_TIMEOUT_5_MIN:
            return 300;
        case IDLE_TIMEOUT_10_MIN:
            return 600;
        case IDLE_TIMEOUT_10_SEC:
            return 10;
        default:
            return 0;
    }

    return 0;
}

uint32_t sleep_timeout_to_secs(sleep_timeout_t timeout) {
    switch (timeout) {
        case SLEEP_TIMEOUT_NEVER:
            return 0;
        case SLEEP_TIMEOUT_1_HRS:
            return 60 * 60;
        case SLEEP_TIMEOUT_2_HRS:
            return 60 * 60 * 2;
        case SLEEP_TIMEOUT_5_HRS:
            return 60 * 60 * 5;
        case SLEEP_TIMEOUT_1_MIN:
            return 60;
        default:
            return 0;
    }

    return 0;
}
