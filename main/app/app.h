#ifndef APP_H_
#define APP_H_

#include "bno085.h"
#include "pmic_axp2101.h"

// The device handles app_main creates at start-up, before any task or view uses them
extern bno085_ctx_t * bno085_dev;
extern axp2101_ctx_t * axp2101_dev;

#endif  // APP_H_
