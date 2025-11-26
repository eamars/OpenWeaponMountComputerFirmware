#include "axp2101.h"
#include "axp2101_priv.h"

#define TAG "AXP2101CPP"

#define XPOWERS_CHIP_AXP2101
#define CONFIG_XPOWERS_ESP_IDF_NEW_API
#include "XPowersLib.h"

static XPowersPMU PMU;

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


void axp2101_monitor_task(void * args) {
    axp2101_ctx_t *ctx = (axp2101_ctx_t *) args;

    ESP_LOGI(TAG, "AXP2101 monitor task started");

    while (1) {
        // Wait until the interrupt happens
        if (xEventGroupWaitBits(ctx->pmic_event_control, PMIC_INTERRUPT_EVENT_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(500)) == pdTRUE) {
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
                ESP_LOGI(TAG, "isVbusInsert");
            }
            if (PMU.isVbusRemoveIrq()) {
                ESP_LOGI(TAG, "isVbusRemove");
            }
            if (PMU.isBatInsertIrq()) {
                ESP_LOGI(TAG, "isBatInsert");
            }
            if (PMU.isBatRemoveIrq()) {
                ESP_LOGI(TAG, "isBatRemove");
            }
            if (PMU.isPekeyShortPressIrq()) {
                ESP_LOGI(TAG, "isPekeyShortPress");
            }
            if (PMU.isPekeyLongPressIrq()) {
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
    }
}


esp_err_t axp2101_init(axp2101_ctx_t *ctx, i2c_master_bus_handle_t i2c_bus_handle, gpio_num_t interrupt_pin) {
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

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

    // Configure I2C interface for PMIC
    // i2c_device_config_t dev_cfg = {
    //     .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    //     .device_address = AXP2101_I2C_SLAVE_ADDRESS,
    //     .scl_speed_hz = 100000,
    // };
    // ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &ctx->dev_handle));


    // Send an probe command to verify if the device is available
    ESP_ERROR_CHECK(i2c_master_probe(i2c_bus_handle, AXP2101_I2C_SLAVE_ADDRESS, AXP2101_I2C_WRITE_TIMEOUT_MS));

    // Add I2C slave to the master

    // Initialize PMU object
    if (PMU.begin(i2c_bus_handle, AXP2101_I2C_SLAVE_ADDRESS)) {
        ESP_LOGI(TAG, "AXP2101 PMU initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to initialize AXP2101 PMU");
        return ESP_FAIL;
    }

    // Read power off reason (to avoid boot loop)
    xpower_power_off_source_t power_off_source = PMU.getPowerOffSource();
    ESP_LOGI(TAG, "AXP2101 Power Off Source: %d", power_off_source);

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

    // Set the minimum operating VBUS voltage 
    PMU.setVbusVoltageLimit(XPOWERS_AXP2101_VBUS_VOL_LIM_3V88);  // allow extremely low voltage input

    // Set maximum current per VBUS input
    PMU.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_1500MA);

    // Set VSYS shutdown voltage
    PMU.setSysPowerDownVoltage(2600);

    // Configure LED behavior (by the charger)
    PMU.setChargingLedMode(XPOWERS_CHG_LED_BLINK_1HZ);

    // Configure IQR
    PMU.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    PMU.clearIrqStatus();
    PMU.enableIRQ(
        XPOWERS_AXP2101_BAT_INSERT_IRQ | XPOWERS_AXP2101_BAT_REMOVE_IRQ |    // BATTERY
        XPOWERS_AXP2101_VBUS_INSERT_IRQ | XPOWERS_AXP2101_VBUS_REMOVE_IRQ |  // VBUS
        XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ |     // POWER KEY
        XPOWERS_AXP2101_BAT_CHG_DONE_IRQ | XPOWERS_AXP2101_BAT_CHG_START_IRQ |// CHARGE
        XPOWERS_AXP2101_WARNING_LEVEL1_IRQ | XPOWERS_AXP2101_WARNING_LEVEL2_IRQ     //Low battery warning
    );

    // Set Precharge and stop charging current
    PMU.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_200MA);
    PMU.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_25MA);

    // Set constant charge current limit
    PMU.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA);

    // Set charge cut off voltage
    PMU.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V1);

    // set low battery warning levels
    PMU.setLowBatWarnThreshold(10);
    PMU.setLowBatShutdownThreshold(5);


    // Read basic system input sources
    ESP_LOGI(TAG, "VBUS Voltage: %d mV", PMU.getVbusVoltage());
    ESP_LOGI(TAG, "VBATT Voltage: %d mV", PMU.getBattVoltage());
    ESP_LOGI(TAG, "SYS Voltage: %d mV", PMU.getSystemVoltage());
    ESP_LOGI(TAG, "DC1 Voltage: %d mV", PMU.getDC1Voltage());


    // Perform a voltage measurement on DC1 (load circuit for main power supply)
    PMU.enableTSPinMeasure();
    float battery_temp = PMU.getTsTemperature();
    ESP_LOGI(TAG, "AXP2101 Battery Temperature: %.2f C", battery_temp);

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

    return ESP_OK;
}


#ifdef __cplusplus
}
#endif
