#ifndef AXP2101_PRIV_H
#define AXP2101_PRIV_H

#include "esp_err.h"

typedef enum {
    PMIC_INTERRUPT_EVENT_BIT = (1 << 0),
    PMIC_INIT_SUCCESS_EVENT_BIT = (1 << 1),
} pmic_event_control_bit_e;


#endif  // AXP2101_PRIV_H