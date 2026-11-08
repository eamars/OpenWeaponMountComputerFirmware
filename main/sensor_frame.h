#ifndef SENSOR_FRAME_H
#define SENSOR_FRAME_H

#include <stdbool.h>

#include "esp_err.h"
#include "bno085.h"

// The unit's frame, from the factory calibration (ADR-011). The level's cant and pitch come from gravity in the
// BNO085's own frame (no tare): cant is gravity's angle about the screen's normal (the sensor's Z axis), counted from
// the calibrated "up" of the rotation 0 edge; pitch is gravity's tilt out of the screen's plane. Neither uses the game
// rotation vector's heading. Without a calibration, "up" is the sensor's +Y axis (the board's design).

typedef struct {
    float up[3];            // "up" in the sensor's frame on the rotation 0 edge, with the surface's tilt removed
    float offset_deg;       // the unit's own cant offset: the calibrated up's angle from the sensor's +Y axis
    float surface_deg;      // the bench's tilt seen during the calibration (removed)
    float repeat_deg;       // the angle between the first and the last placement
    bool calibrated;
} sensor_frame_config_t;

/** @brief Load the calibration and delete any tare persisted in the BNO085 by the old Tare page. After bno085 init */
esp_err_t sensor_frame_init(bno085_ctx_t * dev);

/** @brief "Up" (a unit vector) in the sensor's frame, from a game rotation vector quaternion (real, i, j, k) */
void sensor_frame_up_from_quaternion(const float quat[4], float up[3]);

/** @brief The level's raw cant and pitch in radians from "up" in the sensor's frame. Thread-safe to read */
void sensor_frame_cant_pitch(const float up[3], float * cant_rad, float * pitch_rad);

/** @brief The angle between two unit vectors, in degrees */
float sensor_frame_angle_deg(const float a[3], const float b[3]);

/** @brief The stored calibration (a copy) */
sensor_frame_config_t sensor_frame_get(void);

/** @brief Use and save a new calibration */
esp_err_t sensor_frame_save(const sensor_frame_config_t * config);

#endif  // SENSOR_FRAME_H
