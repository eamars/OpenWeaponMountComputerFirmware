#ifndef KESTREL_SERVICE_H_
#define KESTREL_SERVICE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "kestrel_link.h"

// The Kestrel pull (ADR-007): a stateless, one-off read of the Kestrel's Target Card over BLE. Bluetooth is off
// except during a pull: Pull starts the stack, the Kestrel connects (HUD mode: the unit advertises as the
// Kestrel's HUD), the unit authenticates and reads the targets, then disconnects and stops the stack. Its own task;
// no LVGL. Every phase is timed and logged.

#define KESTREL_SERVICE_TASK_STACK 6144
#define KESTREL_SERVICE_TASK_PRIORITY 3

typedef enum {
    KESTREL_MODE_OFF = 0,
    KESTREL_MODE_HUD = 1,       // the Kestrel in Bluetooth > Conct = Device, this unit paired as its HUD
    KESTREL_MODE_PHONE = 2,     // the Kestrel in its normal mode; this unit connects to it, as the phone app does
} kestrel_mode_t;

#define KESTREL_NAME_SIZE 24

typedef struct {
    uint8_t mode;               // kestrel_mode_t
    // Phone mode: the Kestrel picked on the Kestrel page (a firing line has many)
    uint8_t peer_addr_type;
    uint8_t peer_addr[6];
    char peer_name[KESTREL_NAME_SIZE];  // empty: none picked yet
} kestrel_settings_t;

extern kestrel_settings_t kestrel_settings;

esp_err_t load_kestrel_settings(void);
esp_err_t save_kestrel_settings(void);
/** @brief The defaults, in memory: Kestrel off, no Kestrel picked */
void kestrel_settings_reset(void);

#define KESTREL_FOUND_MAX 6

typedef enum {
    KESTREL_PULL_IDLE,
    KESTREL_PULL_RUNNING,
    KESTREL_PULL_DONE,
    KESTREL_PULL_FAILED,
} kestrel_pull_state_t;

// The phases of a pull, timed
typedef enum {
    KESTREL_PHASE_START,        // the BLE stack up
    KESTREL_PHASE_CONNECT,      // advertising until the Kestrel connects
    KESTREL_PHASE_SUBSCRIBE,    // until it subscribes to the LiNK channel
    KESTREL_PHASE_OPEN,         // its FreeToCommunicate
    KESTREL_PHASE_AUTH,         // the mutual authentication
    KESTREL_PHASE_INFO,         // the Target Card settings (designator)
    KESTREL_PHASE_TARGETS,      // the targets
    KESTREL_PHASE_STOP,         // disconnect and the BLE stack down
    KESTREL_PHASE_COUNT,
} kestrel_phase_t;

typedef struct {
    kestrel_pull_state_t state;
    uint32_t sequence;          // counts status changes, so a reader redraws only on a change
    char message[64];           // what is going on, or why the pull failed
    // The last pull that read all the targets it was asked for (the Kestrel fills unused targets with defaults, so
    // the user says how many: targets 0 to target_count - 1)
    bool have_result;
    uint32_t result_id;         // counts complete pulls, so the card takes each result once
    bool numbered;              // the Kestrel names its targets 1 to 10 (else A to J); can change between cards
    uint8_t target_count;
    kl_target_t targets[KL_TARGET_COUNT];
    // The last pull's timing, ms
    uint32_t phase_ms[KESTREL_PHASE_COUNT];
    uint32_t total_ms;
    uint32_t target_max_ms;     // the slowest single target
    // The last search for Kestrels (phone mode)
    uint32_t found_id;          // counts finished searches
    uint8_t found_count;
    struct {
        uint8_t addr_type;
        uint8_t addr[6];
        int8_t rssi;
        char name[KESTREL_NAME_SIZE];
    } found[KESTREL_FOUND_MAX];
} kestrel_status_t;

/** @brief Start the service's task. Needs NVS and the settings loaded */
esp_err_t kestrel_service_init(void);

/**
 * @brief Start a pull of the first `count` targets (1 to 10). Returns at once: ESP_OK, ESP_ERR_INVALID_ARG, or
 * ESP_ERR_INVALID_STATE when Kestrel is off, a pull is running, Wi-Fi is being set up (BLE and a phone on the
 * unit's access point are an unstable mix) or the unit is updating
 */
esp_err_t kestrel_request_pull(int count);

/** @brief Phone mode: search for Kestrels for a few seconds; the status lists what was found. Returns at once */
esp_err_t kestrel_request_search(void);

/** @brief Phone mode: pull from the `index`th Kestrel the last search found from now on; saves the choice */
esp_err_t kestrel_choose(int index);

/** @brief A copy of the status; callable from any task */
kestrel_status_t kestrel_get_status(void);

/** @brief The name this unit advertises as a HUD, "HUD-" and 7 digits from its Bluetooth address */
void kestrel_hud_name(char * buf, size_t size);

const char * kestrel_phase_name(kestrel_phase_t phase);

#endif  // KESTREL_SERVICE_H_
