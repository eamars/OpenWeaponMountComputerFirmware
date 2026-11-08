#include "sensor_frame.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"

#include "common.h"

#define TAG "SensorFrame"
#define NVS_NAMESPACE "SFR"

// Without a calibration: the board's design, gravity along the sensor's +Y on the rotation 0 edge (measured on a unit,
// ADR-011 step 0) and the sensor's Z along the screen's normal
static const sensor_frame_config_t sensor_frame_default = {
    .up = {0.0f, 1.0f, 0.0f},
    .offset_deg = 0.0f,
    .surface_deg = 0.0f,
    .repeat_deg = 0.0f,
    .calibrated = false,
};

static sensor_frame_config_t sensor_frame;

// The basis the cant is counted in, from the calibration: e_y the calibrated up, b the screen's normal made
// perpendicular to it, e_x = e_y x b (the sensor's +X when uncalibrated). Written whole before use; read by the level
static volatile float basis_x[3];
static volatile float basis_y[3];
static volatile float basis_b[3];


static float dot3(const float a[3], const volatile float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}


static void normalise3(float v[3]) {
    const float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (n > 0) {
        v[0] /= n;
        v[1] /= n;
        v[2] /= n;
    }
}


static void build_basis(void) {
    float y[3] = {sensor_frame.up[0], sensor_frame.up[1], sensor_frame.up[2]};
    normalise3(y);
    // The screen's normal (sensor Z) without its part along up
    float b[3] = {-y[2] * y[0], -y[2] * y[1], 1.0f - y[2] * y[2]};
    normalise3(b);
    const float x[3] = {y[1] * b[2] - y[2] * b[1], y[2] * b[0] - y[0] * b[2], y[0] * b[1] - y[1] * b[0]};
    for (int i = 0; i < 3; i++) {
        basis_x[i] = x[i];
        basis_y[i] = y[i];
        basis_b[i] = b[i];
    }
}


void sensor_frame_up_from_quaternion(const float quat[4], float up[3]) {
    const float w = quat[0], x = quat[1], y = quat[2], z = quat[3];
    up[0] = 2.0f * (x * z - w * y);
    up[1] = 2.0f * (y * z + w * x);
    up[2] = 1.0f - 2.0f * (x * x + y * y);
    normalise3(up);
}


void sensor_frame_cant_pitch(const float up[3], float * cant_rad, float * pitch_rad) {
    if (cant_rad != NULL) {
        *cant_rad = atan2f(dot3(up, basis_x), dot3(up, basis_y));
    }
    if (pitch_rad != NULL) {
        float s = dot3(up, basis_b);
        s = s > 1.0f ? 1.0f : (s < -1.0f ? -1.0f : s);
        *pitch_rad = asinf(s);
    }
}


float sensor_frame_angle_deg(const float a[3], const float b[3]) {
    const float cross[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    const float s = sqrtf(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]);
    const float c = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    return atan2f(s, c) * 180.0f / (float) M_PI;
}


sensor_frame_config_t sensor_frame_get(void) {
    return sensor_frame;
}


esp_err_t sensor_frame_save(const sensor_frame_config_t * config) {
    sensor_frame = *config;
    build_basis();
    ESP_LOGI(TAG, "Calibration: up=%.4f,%.4f,%.4f offset=%.2f surface=%.2f repeat=%.3f", (double) sensor_frame.up[0],
             (double) sensor_frame.up[1], (double) sensor_frame.up[2], (double) sensor_frame.offset_deg,
             (double) sensor_frame.surface_deg, (double) sensor_frame.repeat_deg);
    return save_config(NVS_NAMESPACE, &sensor_frame, sizeof(sensor_frame));
}


esp_err_t sensor_frame_init(bno085_ctx_t * dev) {
    esp_err_t ret = load_config(NVS_NAMESPACE, &sensor_frame, &sensor_frame_default, sizeof(sensor_frame));
    if (ret != ESP_OK) {
        sensor_frame = sensor_frame_default;
    }
    build_basis();
    ESP_LOGI(TAG, "%s: offset %.2f deg", sensor_frame.calibrated ? "Calibrated" : "Not calibrated",
             (double) sensor_frame.offset_deg);

    // The old Tare page persisted a tare that reorients the game rotation vector; the frame above is the sensor's own
    bool cleared = false;
    esp_err_t tare_ret = bno085_clear_persisted_tare(dev, &cleared);
    if (cleared) {
        ESP_LOGW(TAG, "An old persisted tare was found and deleted");
    }
    return tare_ret;
}
