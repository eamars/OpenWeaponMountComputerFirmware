#include "pmic_axp2101.h"
#include "esp_err.h"
#include "esp_log.h"

// Configuration is included in sdkconfig
#include "app_cfg.h"
#include "sdkconfig.h"
#include "XPowersLib.h"

#include "config_view.h"
#include "lvgl_display.h"
#include "low_power_mode.h"

static XPowersPMU PMU;

#define TAG "AXP2101"


// C Function region
#ifdef __cplusplus
extern "C" {
#endif


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

void axp2101_monitor_task(void * args) {
    axp2101_ctx_t *ctx = (axp2101_ctx_t *) args;

    ESP_LOGI(TAG, "AXP2101 monitor task started");

    while (1) {
        // Wait until the interrupt happens
        if (xEventGroupWaitBits(ctx->pmic_event_control, PMIC_INTERRUPT_EVENT_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(2000)) == pdTRUE) {
            // Handle PMIC interrupt
            PMU.getIrqStatus();

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
                    wake_from_idle_mode();
                }
                ESP_LOGI(TAG, "isPekeyShortPress");
            }
            if (PMU.isPekeyLongPressIrq()) {
                update_low_power_mode_last_activity_event();
                if (is_idle_mode_activated()) {
                    wake_from_idle_mode();
                }

                if (lvgl_port_lock(0)) {
                    power_menu_make_visible();
                    lvgl_port_unlock();
                }
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
            // Clear PMU Interrupt Status Register
            PMU.clearIrqStatus();
        }

        // Read status
        ctx->status.charge_status = PMU.getChargerStatus();
        ctx->status.battery_percentage = PMU.getBatteryPercent();
        ctx->status.ts_temperature = PMU.getTsTemperature();

        ctx->status.vbus_voltage_mv = PMU.getVbusVoltage();
        ctx->status.vbatt_voltage_mv = PMU.getBattVoltage();
        ctx->status.vsys_voltage_mv = PMU.getSystemVoltage();

        ctx->status.is_usb_connected = PMU.isVbusIn();

        // Update LVGL
        if (lvgl_display_is_ready()) {
            if (ctx->status.is_usb_connected) {
                status_bar_update_battery_level(101);  // USB power
            }
            else {
                status_bar_update_battery_level(ctx->status.battery_percentage);
            }
        }

        // Update status
        power_management_view_update_status(ctx);

        if (ctx->status.is_usb_connected) {
            // Prevent entering sleep mode when USB is connected, to ensure the system is responsive for user interactions and avoid unexpected sleep when the device is plugged in
            // prevent_sleep_mode_enter(true);
        }

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
        ESP_LOGE(TAG, "Failed to create sensor_event_control");
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

    // Disable protection settings on unused power outputs
    PMU.disableDCHighVoltageTurnOff();
    PMU.disableDC5LowVoltageTurnOff();
    PMU.disableDC4LowVoltageTurnOff();
    PMU.disableDC3LowVoltageTurnOff();
    PMU.disableDC2LowVoltageTurnOff();
    PMU.disableDC1LowVoltageTurnOff();

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

    // FIXME: Disable battery temperature protection
    PMU.disableTSPinMeasure();

    // Ease VSYS shutdown voltage
    PMU.setSysPowerDownVoltage(2600);
    ESP_LOGI(TAG, "Set VSYS power down voltage to %dmV", PMU.getSysPowerDownVoltage());

    // Set the minimum operating VBUS voltage 
    PMU.setVbusVoltageLimit(XPOWERS_AXP2101_VBUS_VOL_LIM_3V88);  // allow extremely low voltage input
    ESP_LOGI(TAG, "Set VBUS voltage limit to %d", PMU.getVbusVoltageLimit());

    // Set maximum current per VBUS input
    PMU.setVbusCurrentLimit(pmic_vbus_current_limit_to_xpowers(power_management_config.vbus_current_limit));
    ESP_LOGI(TAG, "Set VBUS current limit to %d", PMU.getVbusCurrentLimit());

    // Configure LED behavior (by the charger)
    PMU.setChargingLedMode(XPOWERS_CHG_LED_CTRL_CHG);

    // Configure IQR
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

    PMU.setChargerConstantCurr(pmic_battery_charge_current_to_xpowers(power_management_config.battery_charge_current));
    ESP_LOGI(TAG, "Set battery charge current to %d", PMU.getChargerConstantCurr());

    PMU.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_200MA);
    ESP_LOGI(TAG, "Set battery charge termination current to %d", PMU.getChargerTerminationCurr());

    // Set charge voltage
    PMU.setChargeTargetVoltage(pmic_battery_charge_voltage_to_xpowers(power_management_config.battery_charge_voltage));
    ESP_LOGI(TAG, "Set battery charge voltage to %d", PMU.getChargeTargetVoltage());

    // set low battery warning levels
    PMU.setLowBatWarnThreshold(10);
    ESP_LOGI(TAG, "Set low battery warning threshold to %d%%", PMU.getLowBatWarnThreshold());

    PMU.setLowBatShutdownThreshold(5);
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
    ctx->status.charge_status = PMU.getChargerStatus();
    ctx->status.battery_percentage = PMU.getBatteryPercent();
    ctx->status.ts_temperature = PMU.getTsTemperature();

    ctx->status.vbus_voltage_mv = PMU.getVbusVoltage();
    ctx->status.vbatt_voltage_mv = PMU.getBattVoltage();
    ctx->status.vsys_voltage_mv = PMU.getSystemVoltage();

    ctx->status.is_usb_connected = PMU.isVbusIn();

    // print power on reason
    xpower_power_on_source_t power_on_source = PMU.getPowerOnSource();
    ESP_LOGI(TAG, "Power On Source: %d", power_on_source);

    // print power off reason
    xpower_power_off_source_t power_off_source = PMU.getPowerOffSource();
    ESP_LOGI(TAG, "Power Off Source: %d", power_off_source);

    // Create task to handle PMIC interrupt
    BaseType_t rtos_return = xTaskCreate(
        axp2101_monitor_task,
        "axp2101_monitor_task",
        AXP2101_MONITOR_TASK_STACK,
        (void *) ctx,
        AXP2101_MONITOR_TASK_PRIORITY,
        &ctx->pmic_monitor_task_handle
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
        vTaskDelete(ctx->pmic_monitor_task_handle);
        ctx->pmic_monitor_task_handle = NULL;
    }
    ESP_LOGI(TAG, "Monitor task deleted");

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
    // heap_caps_free(ctx);

    // Remove I2C device from master bus
    PMU.deinit();
    ESP_LOGI(TAG, "I2C device removed from master bus");

    return ESP_OK;
}


void pmic_power_off() {
    // Perform the shutdown
    ESP_LOGW(TAG, "System is powering off...");
    PMU.shutdown();
}


#ifdef __cplusplus
}
#endif
