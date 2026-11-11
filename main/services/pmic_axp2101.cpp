#include "pmic_axp2101.h"
#include "esp_err.h"
#include "esp_log.h"

// Configuration is included in sdkconfig
#include "app_cfg.h"
#include "sdkconfig.h"
#include "XPowersLib.h"

#include <math.h>
#include <string.h>

#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"

#include "low_power_mode.h"

static XPowersPMU PMU;

#define TAG "AXP2101"

#define PMIC_EVENT_POST_TIMEOUT_MS  50
// Readings: every 2 s while in use; while idle nobody sees them, and every wake costs (ADR-009)
#define PMIC_READ_PERIOD_MS         2000
#define PMIC_IDLE_READ_PERIOD_MS    30000

// Low battery (ADR-009). The AXP2101's two levels only raise interrupts (datasheet p.20, p.28): nothing powers off by
// itself until the hard cut-off. The unit powers off cleanly at the shutdown level, on battery, when the voltage
// agrees (a fuel gauge misreading alone must not switch a unit off)
#define PMIC_LOW_BATTERY_WARNING_PCT    10
#define PMIC_LOW_BATTERY_SHUTDOWN_PCT   5
#define PMIC_LOW_BATTERY_SHUTDOWN_MV    3600
// Hard cut-off (reg 0x24), a backstop behind the clean power off: the usual floor for a Li-ion cell
#define PMIC_POWER_DOWN_VOLTAGE_MV      3000

// TS: TH1, a 10 kOhm NTC on the PCB (reads the board, not the cell). Reg 0x50: an input only, not gating the charge,
// until its reading is checked (bit 4); current source on only while the TS channel is converting (bits 3:2 = 10);
// 50 uA (bits 1:0 = 10). The ADC reads 0.5 mV a step (datasheet p.25-26, p.45)
#define PMIC_TS_PIN_CTRL_INPUT_ONLY_50UA  0x1A
#define PMIC_TS_CURRENT_UA                50.0f
#define PMIC_TS_ADC_MV_PER_STEP           0.5f
#define PMIC_TS_ADC_INVALID               0x2000


// C Function region
#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DEFINE_BASE(PMIC_SERVICE_EVENT);

// The service's context, set once by axp2101_init; NULL on a unit without a PMIC
static axp2101_ctx_t * pmic_ctx = NULL;
static portMUX_TYPE pmic_status_lock = portMUX_INITIALIZER_UNLOCKED;


axp2101_status_t pmic_get_status() {
    axp2101_status_t status = {};
    if (pmic_ctx != NULL) {
        taskENTER_CRITICAL(&pmic_status_lock);
        status = pmic_ctx->status;
        taskEXIT_CRITICAL(&pmic_status_lock);
    }
    return status;
}


static void pmic_publish(pmic_service_event_e event) {
    // Dropped if the default event loop is full; the next reading posts again
    esp_err_t ret = esp_event_post(PMIC_SERVICE_EVENT, event, NULL, 0, pdMS_TO_TICKS(PMIC_EVENT_POST_TIMEOUT_MS));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to post PMIC event %d: %s", event, esp_err_to_name(ret));
    }
}


esp_err_t pmic_request_power_off() {
    if (pmic_ctx == NULL || pmic_ctx->pmic_event_control == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xEventGroupSetBits(pmic_ctx->pmic_event_control, PMIC_POWER_OFF_EVENT_BIT);
    return ESP_OK;
}


// Written by the monitor task, then PMIC_CHARGER_REGS_READ_EVENT_BIT set
static pmic_charger_regs_t pmic_charger_regs;
static bool pmic_charger_regs_ok;


static bool pmic_read_reg(uint8_t reg, uint8_t *value) {
    int v = PMU.readRegister(reg);
    if (v < 0) {
        return false;
    }
    *value = (uint8_t) v;
    return true;
}


static bool pmic_read_charger_regs_now(pmic_charger_regs_t *regs) {
    uint8_t tdie_h = 0;
    uint8_t tdie_l = 0;
    bool ok = pmic_read_reg(XPOWERS_AXP2101_STATUS1, &regs->status1) &&
              pmic_read_reg(XPOWERS_AXP2101_STATUS2, &regs->status2) &&
              pmic_read_reg(0x13, &regs->die_temp_ctrl) &&
              pmic_read_reg(0x14, &regs->vsys_dpm) &&
              pmic_read_reg(0x15, &regs->vindpm) &&
              pmic_read_reg(0x16, &regs->input_current) &&
              pmic_read_reg(0x18, &regs->charger_ctrl) &&
              pmic_read_reg(XPOWERS_AXP2101_ADC_CHANNEL_CTRL, &regs->adc_ctrl) &&
              pmic_read_reg(0x61, &regs->precharge) &&
              pmic_read_reg(0x62, &regs->charge_current) &&
              pmic_read_reg(0x63, &regs->termination) &&
              pmic_read_reg(0x64, &regs->charge_voltage) &&
              pmic_read_reg(0x65, &regs->thermal_regulation) &&
              pmic_read_reg(0x67, &regs->charge_timer) &&
              pmic_read_reg(0x68, &regs->battery_detect) &&
              pmic_read_reg(XPOWERS_AXP2101_ADC_DATA_RELUST8, &tdie_h) &&
              pmic_read_reg(XPOWERS_AXP2101_ADC_DATA_RELUST9, &tdie_l);
    regs->tdie_raw = (uint16_t) (((tdie_h & 0x3F) << 8) | tdie_l);
    return ok;
}


// Written by the monitor task with pmic_charger_regs
static uint8_t pmic_dump[PMIC_DUMP_REG_COUNT];
static bool pmic_dump_ok;


static bool pmic_read_dump_now(uint8_t *regs) {
    bool ok = true;
    for (int reg = 0; reg < PMIC_DUMP_REG_COUNT; reg++) {
        if (!pmic_read_reg((uint8_t) reg, &regs[reg])) {
            regs[reg] = 0;
            ok = false;
        }
    }
    return ok;
}


// Asks the monitor task to read the charger registers and the dump, and waits for it
static esp_err_t pmic_request_reads(uint32_t timeout_ms) {
    if (pmic_ctx == NULL || pmic_ctx->pmic_event_control == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    EventGroupHandle_t events = pmic_ctx->pmic_event_control;
    xEventGroupClearBits(events, PMIC_CHARGER_REGS_READ_EVENT_BIT);
    xEventGroupSetBits(events, PMIC_READ_CHARGER_REGS_EVENT_BIT);
    EventBits_t bits = xEventGroupWaitBits(events, PMIC_CHARGER_REGS_READ_EVENT_BIT, pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    return (bits & PMIC_CHARGER_REGS_READ_EVENT_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}


esp_err_t pmic_read_charger_regs(pmic_charger_regs_t *regs, uint32_t timeout_ms) {
    if (regs == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = pmic_request_reads(timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }
    *regs = pmic_charger_regs;
    return pmic_charger_regs_ok ? ESP_OK : ESP_FAIL;
}


esp_err_t pmic_dump_registers(uint8_t regs[PMIC_DUMP_REG_COUNT], uint32_t timeout_ms) {
    if (regs == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = pmic_request_reads(timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }
    memcpy(regs, pmic_dump, PMIC_DUMP_REG_COUNT);
    return pmic_dump_ok ? ESP_OK : ESP_FAIL;
}


esp_err_t pmic_request_die_temp_adc(bool on) {
    if (pmic_ctx == NULL || pmic_ctx->pmic_event_control == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xEventGroupSetBits(pmic_ctx->pmic_event_control,
                       on ? PMIC_DIE_TEMP_ADC_ON_EVENT_BIT : PMIC_DIE_TEMP_ADC_OFF_EVENT_BIT);
    return ESP_OK;
}


// The datasheet's NTC table (p.26, a 10 kOhm TH11-3H103F at 50 uA): resistance in kOhm every 5 degC from -20 to 60.
// XPowersLib's getTsTemperature() assumes 50 uA after its enableTSPinMeasure() sets 60 uA, so it read about 5 degC
// low; we set the current and convert here
static const float pmic_ts_table_kohm[] = {
    63.00f, 50.15f, 40.26f, 32.55f, 26.49f, 21.68f, 17.78f, 14.63f, 12.07f,
    10.00f, 8.320f, 6.954f, 5.839f, 4.924f, 4.171f, 3.549f, 3.032f,
};
#define PMIC_TS_TABLE_FIRST_C   -20.0f
#define PMIC_TS_TABLE_STEP_C    5.0f


// Degrees C from the TS reading, NAN when the pin reads open or out of the table
static float pmic_ts_celsius(uint16_t raw) {
    if (raw == 0 || raw >= PMIC_TS_ADC_INVALID) {
        return NAN;
    }
    const float kohm = raw * PMIC_TS_ADC_MV_PER_STEP / PMIC_TS_CURRENT_UA;  // mV / uA = kOhm
    const int n = sizeof(pmic_ts_table_kohm) / sizeof(pmic_ts_table_kohm[0]);
    if (kohm > pmic_ts_table_kohm[0] || kohm < pmic_ts_table_kohm[n - 1]) {
        return NAN;
    }
    // Interpolated in log resistance, which is close to linear in temperature over one 5 degC step
    for (int i = 0; i < n - 1; i++) {
        if (kohm <= pmic_ts_table_kohm[i] && kohm >= pmic_ts_table_kohm[i + 1]) {
            float f = logf(pmic_ts_table_kohm[i] / kohm) / logf(pmic_ts_table_kohm[i] / pmic_ts_table_kohm[i + 1]);
            return PMIC_TS_TABLE_FIRST_C + (i + f) * PMIC_TS_TABLE_STEP_C;
        }
    }
    return NAN;
}


// Zeroed first, so a change check with memcmp does not see padding
static void pmic_read_status(axp2101_status_t *status) {
    memset(status, 0, sizeof(*status));
    status->valid = true;
    status->charge_status = PMU.getChargerStatus();
    status->battery_percentage = PMU.getBatteryPercent();
    status->ts_raw = PMU.getTsPinValue();
    status->ts_temperature = pmic_ts_celsius(status->ts_raw);

    status->vbus_voltage_mv = PMU.getVbusVoltage();
    status->vbatt_voltage_mv = PMU.getBattVoltage();
    status->vsys_voltage_mv = PMU.getSystemVoltage();

    status->is_usb_connected = PMU.isVbusIn();
}


static void pmic_power_off() {
    // Perform the shutdown
    ESP_LOGW(TAG, "System is powering off...");

    // disable battery fet
    PMU.disableBATFET();

    // Perform shutdown
    PMU.shutdown();
}


static void IRAM_ATTR axp2101_interrupt_handler(void * args) {
    axp2101_ctx_t *ctx = (axp2101_ctx_t *) args;

    // Allow the consumer to unblock
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xEventGroupSetBitsFromISR(ctx->pmic_event_control, PMIC_INTERRUPT_EVENT_BIT, &xHigherPriorityTaskWoken) != pdFAIL) {
        // Yield a context switch if needed
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}


static xpowers_axp2101_vbus_cur_limit_t pmic_vbus_current_limit_to_xpowers(pmic_vbus_current_limit_e limit) {
    switch (limit) {
        case PMIC_VBUS_CURRENT_LIMIT_100MA:
            return XPOWERS_AXP2101_VBUS_CUR_LIM_100MA;
        case PMIC_VBUS_CURRENT_LIMIT_500MA:
            return XPOWERS_AXP2101_VBUS_CUR_LIM_500MA;
        case PMIC_VBUS_CURRENT_LIMIT_900MA:
            return XPOWERS_AXP2101_VBUS_CUR_LIM_900MA;
        case PMIC_VBUS_CURRENT_LIMIT_1000MA:
            return XPOWERS_AXP2101_VBUS_CUR_LIM_1000MA;
        case PMIC_VBUS_CURRENT_LIMIT_1500MA:
            return XPOWERS_AXP2101_VBUS_CUR_LIM_1500MA;
        case PMIC_VBUS_CURRENT_LIMIT_2000MA:
            return XPOWERS_AXP2101_VBUS_CUR_LIM_2000MA;
        default:
            return XPOWERS_AXP2101_VBUS_CUR_LIM_500MA;
    }
}

static xpowers_axp2101_chg_curr_t pmic_battery_charge_current_to_xpowers(pmic_battery_charge_current_e current) {
    switch (current) {
        case PMIC_BATTERY_CHARGE_CURRENT_100MA:
            return XPOWERS_AXP2101_CHG_CUR_100MA;
        case PMIC_BATTERY_CHARGE_CURRENT_200MA:
            return XPOWERS_AXP2101_CHG_CUR_200MA;
        case PMIC_BATTERY_CHARGE_CURRENT_300MA:
            return XPOWERS_AXP2101_CHG_CUR_300MA;
        case PMIC_BATTERY_CHARGE_CURRENT_400MA:
            return XPOWERS_AXP2101_CHG_CUR_400MA;
        case PMIC_BATTERY_CHARGE_CURRENT_500MA:
            return XPOWERS_AXP2101_CHG_CUR_500MA;
        case PMIC_BATTERY_CHARGE_CURRENT_600MA:
            return XPOWERS_AXP2101_CHG_CUR_600MA;
        case PMIC_BATTERY_CHARGE_CURRENT_700MA:
            return XPOWERS_AXP2101_CHG_CUR_700MA;
        case PMIC_BATTERY_CHARGE_CURRENT_800MA:
            return XPOWERS_AXP2101_CHG_CUR_800MA;
        case PMIC_BATTERY_CHARGE_CURRENT_900MA:
            return XPOWERS_AXP2101_CHG_CUR_900MA;
        case PMIC_BATTERY_CHARGE_CURRENT_1000MA:
            return XPOWERS_AXP2101_CHG_CUR_1000MA;
        default:
            return XPOWERS_AXP2101_CHG_CUR_500MA;
    }
}


static xpowers_axp2101_chg_vol_t pmic_battery_charge_voltage_to_xpowers(pmic_battery_charge_voltage_e voltage) {
    switch (voltage) {
        case PMIC_BATTERY_CHARGE_VOLTAGE_4V:
            return XPOWERS_AXP2101_CHG_VOL_4V;
        case PMIC_BATTERY_CHARGE_VOLTAGE_4V1:
            return XPOWERS_AXP2101_CHG_VOL_4V1;
        case PMIC_BATTERY_CHARGE_VOLTAGE_4V2:
            return XPOWERS_AXP2101_CHG_VOL_4V2;
        case PMIC_BATTERY_CHARGE_VOLTAGE_4V35:
            return XPOWERS_AXP2101_CHG_VOL_4V35;
        case PMIC_BATTERY_CHARGE_VOLTAGE_4V4:
            return XPOWERS_AXP2101_CHG_VOL_4V4;
        default:
            return XPOWERS_AXP2101_CHG_VOL_4V2;
    }
}

// The settings page's charge settings. Written at init, then only by the monitor task, which owns the chip
// after init: the page asks for it on save instead of writing over I2C from the LVGL task (ADR-005 B15)
static void axp2101_apply_charge_settings() {
    PMU.setVbusCurrentLimit(pmic_vbus_current_limit_to_xpowers(power_management_config.vbus_current_limit));
    ESP_LOGI(TAG, "Set VBUS current limit to %d", PMU.getVbusCurrentLimit());

    PMU.setChargerConstantCurr(pmic_battery_charge_current_to_xpowers(power_management_config.battery_charge_current));
    ESP_LOGI(TAG, "Set battery charge current to %d", PMU.getChargerConstantCurr());

    PMU.setChargeTargetVoltage(pmic_battery_charge_voltage_to_xpowers(power_management_config.battery_charge_voltage));
    ESP_LOGI(TAG, "Set battery charge voltage to %d", PMU.getChargeTargetVoltage());
}


esp_err_t axp2101_request_apply_charge_settings(axp2101_ctx_t *ctx) {
    if (ctx == NULL || ctx->pmic_event_control == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xEventGroupSetBits(ctx->pmic_event_control, PMIC_APPLY_CHARGE_SETTINGS_EVENT_BIT);
    return ESP_OK;
}


void axp2101_monitor_task(void * args) {
    axp2101_ctx_t *ctx = (axp2101_ctx_t *) args;

    ESP_LOGI(TAG, "AXP2101 monitor task started");
    int low_battery_reads = 0;

    while (1) {
        // Wait until the interrupt happens, or the settings page asks for the charge settings to be applied
        EventBits_t bits = xEventGroupWaitBits(ctx->pmic_event_control,
                                               PMIC_INTERRUPT_EVENT_BIT | PMIC_APPLY_CHARGE_SETTINGS_EVENT_BIT |
                                               PMIC_POWER_OFF_EVENT_BIT | PMIC_READ_CHARGER_REGS_EVENT_BIT |
                                               PMIC_DIE_TEMP_ADC_ON_EVENT_BIT | PMIC_DIE_TEMP_ADC_OFF_EVENT_BIT,
                                               pdTRUE, pdFALSE, pdMS_TO_TICKS(is_idle_mode_activated() ?
                                                   PMIC_IDLE_READ_PERIOD_MS : PMIC_READ_PERIOD_MS));
        // The IRQ line stays low until its status is cleared (datasheet p.29), and the interrupt is on the falling
        // edge: a missed edge would leave it low with no further interrupt (the power key dead). A low line is an
        // interrupt, whatever woke the task
        if (ctx->interrupt_pin != GPIO_NUM_NC && gpio_get_level(ctx->interrupt_pin) == 0) {
            bits |= PMIC_INTERRUPT_EVENT_BIT;
        }
        if (bits & PMIC_POWER_OFF_EVENT_BIT) {
            pmic_power_off();
        }
        if (bits & PMIC_APPLY_CHARGE_SETTINGS_EVENT_BIT) {
            axp2101_apply_charge_settings();
        }
        if (bits & PMIC_DIE_TEMP_ADC_ON_EVENT_BIT) {
            PMU.enableTemperatureMeasure();
        }
        if (bits & PMIC_DIE_TEMP_ADC_OFF_EVENT_BIT) {
            PMU.disableTemperatureMeasure();
        }
        if (bits & PMIC_READ_CHARGER_REGS_EVENT_BIT) {
            pmic_charger_regs_ok = pmic_read_charger_regs_now(&pmic_charger_regs);
            pmic_dump_ok = pmic_read_dump_now(pmic_dump);
            xEventGroupSetBits(ctx->pmic_event_control, PMIC_CHARGER_REGS_READ_EVENT_BIT);
        }
        if (bits & PMIC_INTERRUPT_EVENT_BIT) {
            // Handle PMIC interrupt
            uint32_t irq_status = (uint32_t) PMU.getIrqStatus();

            if (PMU.isDropWarningLevel2Irq()) {
                ESP_LOGI(TAG, "isDropWarningLevel2");
            }
            if (PMU.isDropWarningLevel1Irq()) {
                ESP_LOGI(TAG, "isDropWarningLevel1");
            }
            if (PMU.isGaugeWdtTimeoutIrq()) {
                ESP_LOGI(TAG, "isWdtTimeout");
            }
            if (PMU.isStateOfChargeLowIrq()) {
                ESP_LOGI(TAG, "isStateOfChargeLow");
            }
            if (PMU.isBatChargerOverTemperatureIrq()) {
                ESP_LOGI(TAG, "isBatChargeOverTemperature");
            }
            if (PMU.isBatWorkOverTemperatureIrq()) {
                ESP_LOGI(TAG, "isBatWorkOverTemperature");
            }
            if (PMU.isBatWorkUnderTemperatureIrq()) {
                ESP_LOGI(TAG, "isBatWorkUnderTemperature");
            }
            if (PMU.isVbusInsertIrq()) {
                update_low_power_mode_last_activity_event();
                ESP_LOGI(TAG, "isVbusInsert");
            }
            if (PMU.isVbusRemoveIrq()) {
                update_low_power_mode_last_activity_event();
                ESP_LOGI(TAG, "isVbusRemove");
            }
            if (PMU.isBatInsertIrq()) {
                ESP_LOGI(TAG, "isBatInsert");
            }
            if (PMU.isBatRemoveIrq()) {
                ESP_LOGI(TAG, "isBatRemove");
            }
            if (PMU.isPekeyShortPressIrq()) {
                update_low_power_mode_last_activity_event();
                if (is_idle_mode_activated()) {
                    low_power_mode_note_wake_cause("key");
                    wake_from_idle_mode();
                }
                ESP_LOGI(TAG, "isPekeyShortPress");
            }
            if (PMU.isPekeyLongPressIrq()) {
                update_low_power_mode_last_activity_event();
                if (is_idle_mode_activated()) {
                    low_power_mode_note_wake_cause("key");
                    wake_from_idle_mode();
                }

                pmic_publish(PMIC_SERVICE_EVENT_POWER_KEY_LONG_PRESS);
                ESP_LOGI(TAG, "isPekeyLongPress");
            }
            if (PMU.isPekeyNegativeIrq()) {
                ESP_LOGI(TAG, "isPekeyNegative");
            }
            if (PMU.isPekeyPositiveIrq()) {
                ESP_LOGI(TAG, "isPekeyPositive");
            }
            if (PMU.isWdtExpireIrq()) {
                ESP_LOGI(TAG, "isWdtExpire");
            }
            if (PMU.isLdoOverCurrentIrq()) {
                ESP_LOGI(TAG, "isLdoOverCurrentIrq");
            }
            if (PMU.isBatfetOverCurrentIrq()) {
                ESP_LOGI(TAG, "isBatfetOverCurrentIrq");
            }
            if (PMU.isBatChargeDoneIrq()) {
                ESP_LOGI(TAG, "isBatChargeDone");
            }
            if (PMU.isBatChargeStartIrq()) {
                ESP_LOGI(TAG, "isBatChargeStart");
            }
            if (PMU.isBatDieOverTemperatureIrq()) {
                ESP_LOGI(TAG, "isBatDieOverTemperature");
            }
            if (PMU.isChargeOverTimeoutIrq()) {
                ESP_LOGI(TAG, "isChargeOverTimeout");
            }
            if (PMU.isBatOverVoltageIrq()) {
                ESP_LOGI(TAG, "isBatOverVoltage");
            }
            // Clear only what was read and handled (write 1 to clear, p.43): an event that came in meanwhile stays
            // set, keeps the line low and is handled on the next pass
            PMU.writeRegister(XPOWERS_AXP2101_INTSTS1, (irq_status >> 16) & 0xFF);
            PMU.writeRegister(XPOWERS_AXP2101_INTSTS2, (irq_status >> 8) & 0xFF);
            PMU.writeRegister(XPOWERS_AXP2101_INTSTS3, irq_status & 0xFF);
        }

        // Read status
        axp2101_status_t status;
        pmic_read_status(&status);

        // Publish it; the UI bridge shows it whenever the UI exists
        taskENTER_CRITICAL(&pmic_status_lock);
        bool changed = memcmp(&status, &ctx->status, sizeof(status)) != 0;
        ctx->status = status;
        taskEXIT_CRITICAL(&pmic_status_lock);
        if (changed) {
            pmic_publish(PMIC_SERVICE_EVENT_STATUS_CHANGED);
        }

        // Low battery: power off cleanly on battery, after two readings in a row (one bad gauge reading is not enough)
        bool low = !status.is_usb_connected && status.battery_percentage <= PMIC_LOW_BATTERY_SHUTDOWN_PCT &&
                   status.vbatt_voltage_mv > 0 && status.vbatt_voltage_mv < PMIC_LOW_BATTERY_SHUTDOWN_MV;
        low_battery_reads = low ? low_battery_reads + 1 : 0;
        if (low_battery_reads >= 2) {
            ESP_LOGW(TAG, "Battery low (%d%%, %u mV): powering off", status.battery_percentage,
                     (unsigned) status.vbatt_voltage_mv);
            pmic_power_off();
        }

        // No light sleep while USB power is in (ADR-009); a plug-in raises the interrupt, which wakes the chip
        low_power_mode_set_usb_power(status.is_usb_connected);

        // // Print it out
        // ESP_LOGI(TAG, "Charge Status: %d, Battery: %d%%, TS Temp: %.2f C",
        //     ctx->status.charge_status,
        //     ctx->status.battery_percentage,
        //     ctx->status.ts_temperature
        // );
        // ESP_LOGI(TAG, "VBUS: %dmV, VBATT: %dmV, VSYS: %dmV",
        //     ctx->status.vbus_voltage_mv,
        //     ctx->status.vbatt_voltage_mv,
        //     ctx->status.vsys_voltage_mv
        // );
    }

}


esp_err_t axp2101_init(axp2101_ctx_t *ctx, i2c_master_bus_handle_t i2c_bus_handle, gpio_num_t interrupt_pin) {
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Read configuration
    ESP_ERROR_CHECK(load_pmic_config());

    // Initialize the context structure
    memset(ctx, 0, sizeof(axp2101_ctx_t));

    // Create event group
    ctx->pmic_event_control = xEventGroupCreate();
    if (ctx->pmic_event_control == NULL) {
        ESP_LOGE(TAG, "Failed to create pmic_event_control");
        return ESP_FAIL;
    }

    // Reset pin assignment
    ctx->interrupt_pin = interrupt_pin;

    // Configure interrupt
    if (ctx->interrupt_pin != GPIO_NUM_NC) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << ctx->interrupt_pin),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,  // enable internal pull up to avoid floating state during AXP2101 chip reset
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_NEGEDGE, // Trigger on falling edge (active low)
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));

        // Setup interrupt handler
        // Note: Actual ISR handler function should be defined elsewhere
        ESP_ERROR_CHECK(gpio_isr_handler_add(ctx->interrupt_pin, axp2101_interrupt_handler, (void *) ctx));

        ESP_LOGI(TAG, "Configured AXP2101 interrupt on pin %d", ctx->interrupt_pin);
    }

    // Send an probe command to verify if the device is available
    // ESP_ERROR_CHECK(i2c_master_probe(i2c_bus_handle, AXP2101_I2C_SLAVE_ADDRESS, AXP2101_I2C_WRITE_TIMEOUT_MS));

    // Configure I2C interface for PMIC
    // i2c_device_config_t dev_cfg = {
    //     .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    //     .device_address = AXP2101_I2C_SLAVE_ADDRESS,
    //     .scl_speed_hz = 100000,
    // };
    // ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &ctx->dev_handle));

    // Initialize PMU object
    if (PMU.begin(i2c_bus_handle, AXP2101_I2C_SLAVE_ADDRESS)) {
        ESP_LOGI(TAG, "AXP2101 PMU initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to initialize AXP2101 PMU");
        return ESP_FAIL;
    }

    // Under-voltage power off on the unused DC-DCs off; on DCDC1, which powers the whole board (one +3V3 rail), it
    // stays on, with the over-voltage power off: a failing rail powers the unit off cleanly instead of brown-out
    // resets (ADR-009; datasheet p.20, reg 0x23)
    PMU.enableDCHighVoltageTurnOff();
    PMU.enableDC1LowVoltageTurnOff();
    PMU.disableDC5LowVoltageTurnOff();
    PMU.disableDC4LowVoltageTurnOff();
    PMU.disableDC3LowVoltageTurnOff();
    PMU.disableDC2LowVoltageTurnOff();

    // Disable unused power outputs
    PMU.disableDC2();
    PMU.disableDC3();
    PMU.disableDC4();
    PMU.disableDC5();

    PMU.disableALDO1();
    PMU.disableALDO2();
    PMU.disableALDO3();
    PMU.disableALDO4();
    PMU.disableBLDO1();
    PMU.disableBLDO2();

    PMU.disableDLDO1();
    PMU.disableDLDO2();

    // Enable measurements
    PMU.enableSystemVoltageMeasure();
    PMU.enableBattVoltageMeasure();
    PMU.enableVbusVoltageMeasure();
    PMU.enableBattDetection();

    // Don't check for PWROK pin
    PMU.disablePwrOk();

    // TS measured as an input only, not gating the charge yet (ADR-009): TH1 read a constant because the old code
    // switched the measurement off, and a wrong value because the library set 60 uA and converted as 50 uA
    PMU.writeRegister(XPOWERS_AXP2101_TS_PIN_CTRL,
                      (PMU.readRegister(XPOWERS_AXP2101_TS_PIN_CTRL) & 0xE0) | PMIC_TS_PIN_CTRL_INPUT_ONLY_50UA);
    PMU.writeRegister(XPOWERS_AXP2101_ADC_CHANNEL_CTRL, PMU.readRegister(XPOWERS_AXP2101_ADC_CHANNEL_CTRL) | 0x02);

    // Hard cut-off, a backstop behind the clean power off at low battery (was 2.6 V, the lowest setting)
    PMU.setSysPowerDownVoltage(PMIC_POWER_DOWN_VOLTAGE_MV);
    ESP_LOGI(TAG, "Set VSYS power down voltage to %dmV", PMU.getSysPowerDownVoltage());

    // Set the minimum operating VBUS voltage 
    PMU.setVbusVoltageLimit(XPOWERS_AXP2101_VBUS_VOL_LIM_3V88);  // allow extremely low voltage input
    ESP_LOGI(TAG, "Set VBUS voltage limit to %d", PMU.getVbusVoltageLimit());


    // Configure LED behavior (by the charger)
    PMU.setChargingLedMode(XPOWERS_CHG_LED_CTRL_CHG);

    // Configure IRQ
    PMU.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    PMU.clearIrqStatus();
    PMU.enableIRQ(
        // XPOWERS_AXP2101_BAT_INSERT_IRQ | XPOWERS_AXP2101_BAT_REMOVE_IRQ |    // BATTERY
        XPOWERS_AXP2101_VBUS_INSERT_IRQ | XPOWERS_AXP2101_VBUS_REMOVE_IRQ |  // VBUS
        XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ |     // POWER KEY
        // XPOWERS_AXP2101_BAT_CHG_DONE_IRQ | XPOWERS_AXP2101_BAT_CHG_START_IRQ |// CHARGE
        XPOWERS_AXP2101_WARNING_LEVEL1_IRQ | XPOWERS_AXP2101_WARNING_LEVEL2_IRQ     //Low battery warning
    );

    // Set charge current limits
    PMU.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_100MA);
    ESP_LOGI(TAG, "Set battery pre-charge current to %d", PMU.getPrechargeCurr());

    // About a tenth of the Vapcell T9's 950 mAh (ADR-009). A higher termination does not charge faster; it stops
    // short of full. The input limit is what limits the speed
    PMU.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_100MA);
    ESP_LOGI(TAG, "Set battery charge termination current to %d", PMU.getChargerTerminationCurr());

    // The charger holds VSYS at this level by cutting its current (reg 0x14, datasheet p.33; the chip's default is
    // 4.7 V). USB arrives at 4.6 V through D2, below 4.7 V, so at the default the charger reports CC with next to no
    // current. 4.1 V, the lowest (ADR-016)
    PMU.setLinearChargerVsysDpm(XPOWERS_AXP2101_VSYS_VOL_4V1);
    ESP_LOGI(TAG, "Set linear charger VSYS DPM to %d", PMU.getLinearChargerVsysDpm());

    // VBUS current limit, charge current and charge voltage from the settings page
    axp2101_apply_charge_settings();

    // set low battery warning levels
    PMU.setLowBatWarnThreshold(PMIC_LOW_BATTERY_WARNING_PCT);
    ESP_LOGI(TAG, "Set low battery warning threshold to %d%%", PMU.getLowBatWarnThreshold());

    PMU.setLowBatShutdownThreshold(PMIC_LOW_BATTERY_SHUTDOWN_PCT);
    ESP_LOGI(TAG, "Set low battery shutdown threshold to %d%%", PMU.getLowBatShutdownThreshold());

    // Set PWROK output delay
    PMU.setPwrOkDelay(XPOWER_PWROK_DELAY_8MS);

    // Define button behavior
    // Configure threshold
    PMU.setPowerKeyPressOnTime(XPOWERS_POWERON_128MS);
    ESP_LOGI(TAG, "Set power on threshold to %d", PMU.getPowerKeyPressOnTime());

    PMU.setPowerKeyPressOffTime(XPOWERS_POWEROFF_10S);
    ESP_LOGI(TAG, "Set power off threshold to %d", PMU.getPowerKeyPressOffTime());

    PMU.setIrqLevelTime(XPOWERS_AXP2101_IRQ_TIME_1S);  // 1s
    ESP_LOGI(TAG, "Set IRQ level time to %d", PMU.getIrqLevelTime());

    PMU.enableLongPressShutdown();
    PMU.setLongPressRestart();
    PMU.disablePwrOkPinPullLow();  // do not reset on PWROK pulling low (this should never happen)
    PMU.disablePwronShutPMIC();     // Hard off on 16s press

    // Populate status
    pmic_read_status(&ctx->status);

    // The protection, cut-off and TS settings as the chip holds them (ADR-009)
    ESP_LOGI(TAG, "Read back: 0x23 uvp/ovp=0x%02x 0x24 voff=0x%02x 0x30 adc=0x%02x 0x50 ts=0x%02x 0x63 iterm=0x%02x",
             PMU.readRegister(0x23), PMU.readRegister(0x24), PMU.readRegister(XPOWERS_AXP2101_ADC_CHANNEL_CTRL),
             PMU.readRegister(XPOWERS_AXP2101_TS_PIN_CTRL), PMU.readRegister(0x63));

    // print power on reason
    xpower_power_on_source_t power_on_source = PMU.getPowerOnSource();
    ESP_LOGI(TAG, "Power On Source: %d", power_on_source);

    // print power off reason
    xpower_power_off_source_t power_off_source = PMU.getPowerOffSource();
    ESP_LOGI(TAG, "Power Off Source: %d", power_off_source);

    // From here on the monitor task is the only code that talks to the chip
    pmic_ctx = ctx;

    // Create task to handle PMIC interrupt
    BaseType_t rtos_return = xTaskCreateWithCaps(
        axp2101_monitor_task,
        "axp2101_monitor_task",
        AXP2101_MONITOR_TASK_STACK,
        (void *) ctx,
        AXP2101_MONITOR_TASK_PRIORITY,
        &ctx->pmic_monitor_task_handle,
        HEAPS_CAPS_ALLOC_DEFAULT_FLAGS
    );
    if (rtos_return != pdPASS) {
        ESP_LOGE(TAG, "Failed to create axp2101_monitor_task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "AXP2101 Initialization complete");

    return ESP_OK;
}


esp_err_t axp2101_deinit(axp2101_ctx_t *ctx) {
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Delete monitor task
    if (ctx->pmic_monitor_task_handle != NULL) {
        vTaskDeleteWithCaps(ctx->pmic_monitor_task_handle);
        ctx->pmic_monitor_task_handle = NULL;
    }
    ESP_LOGI(TAG, "Monitor task deleted");
    pmic_ctx = NULL;

    // Remove interrupt handler
    if (ctx->interrupt_pin != GPIO_NUM_NC) {
        ESP_ERROR_CHECK(gpio_isr_handler_remove(ctx->interrupt_pin));
    }
    ESP_LOGI(TAG, "Interrupt handler removed");

    // Delete event group
    if (ctx->pmic_event_control != NULL) {
        vEventGroupDelete(ctx->pmic_event_control);
        ctx->pmic_event_control = NULL;
    }
    ESP_LOGI(TAG, "Event group deleted");

    // FIXME: Don't free the CTX for now. 

    // Remove I2C device from master bus
    PMU.deinit();
    ESP_LOGI(TAG, "I2C device removed from master bus");

    return ESP_OK;
}



#ifdef __cplusplus
}
#endif
