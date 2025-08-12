#ifndef APP_CFG_H
#define APP_CFG_H

// LCD related definitions
#define DISP_H_RES_PIXEL (172)
#define DISP_V_RES_PIXEL (320)
#define DISP_ROTATION (0) // 0: 0 degrees, 1: 90 degrees, 2: 180 degrees, 3: 270 degrees
#define DISP_PANEL_H_GAP (34) // Horizontal gap for display panel
#define DISP_PANEL_V_GAP (0)  // Vertical gap for display panel

#define SENSOR_EVENT_POLLER_TASK_STACK 2048
#define SENSOR_EVENT_POLLER_TASK_PRIORITY 5
#define LVGL_UNLOCK_WAIT_TIME_MS 1
#define BNO085_INT_PIN (GPIO_NUM_9)

#endif // APP_CFG_H
