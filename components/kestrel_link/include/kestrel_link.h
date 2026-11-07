#ifndef KESTREL_LINK_H_
#define KESTREL_LINK_H_

// The Kestrel 5700 LiNK protocol, transport-free (kestrel_5700_connect_library, docs/ICD.md rev 1.18): HDLC
// framing with a CRC-16/X.25, packets, AES-128 link encryption and the host side of the authentication, and the
// decoding of the records this firmware reads. No BLE, no tasks, no allocation: the caller moves the bytes.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Packet types (ICD §4)
#define KL_TYPE_COMMAND  0
#define KL_TYPE_RESPONSE 3
#define KL_TYPE_ACK      4
#define KL_TYPE_NACK     5

// Opcodes this firmware sends or answers (ICD §6.1, §6.3, §14.2); all reads, or the authentication
#define KL_OP_EOD                   0x0012
#define KL_OP_REQUEST_AUTH          0x007e
#define KL_OP_AUTH_DATA             0x007f
#define KL_OP_GET_PRIVACY_STATUS    0x0081  // the phone link's first command; not on a HUD link
#define KL_OP_GET_SETTINGS          0x0084
#define KL_OP_SET_FULL_SOLN         0x008a  // the Kestrel's reply to CALC_FULL_SOLN
#define KL_OP_CALC_FULL_SOLN        0x0093  // only with the Target Card arguments [t][t][ff][00] (SAFE)
#define KL_OP_FREE_TO_COMMUNICATE   0x00a8  // the Kestrel's first frame on a HUD link

#define KL_SETTINGS_TGT_INFO        2
#define KL_TARGET_COUNT             10      // targets per Target Card sector

#define KL_PACKET_MAX               256     // largest packet this firmware handles (payload + 4)
#define KL_FRAME_MAX                (2 * (KL_PACKET_MAX + 32 + 2) + 2)  // escaped worst case, encrypted

// A decoded packet. `payload` points into the buffer it was parsed from
typedef struct {
    uint16_t type;
    uint16_t len;
    const uint8_t * payload;
} kl_packet_t;

// Turns a byte stream (BLE writes or indications, any chunking) into frame bodies with a good CRC
typedef struct {
    uint8_t body[KL_FRAME_MAX];
    size_t len;
    bool in_frame;
    bool escape;
    uint32_t bad_crc;       // frames dropped for their CRC
    uint32_t overflow;      // frames dropped for their size
} kl_deframer_t;

uint16_t kl_crc_x25(const uint8_t * data, size_t len);

void kl_deframer_reset(kl_deframer_t * d);
/**
 * @brief Feed one byte. Returns true when a frame ended with a good CRC: its body (CRC removed) is in d->body,
 * d->len bytes, valid until the next call.
 */
bool kl_deframer_push(kl_deframer_t * d, uint8_t byte);

/**
 * @brief Build a frame: the packet [type][len][payload], encrypted when `encrypted`, then CRC, escaping and flags.
 * @return the frame's length, or 0 when it does not fit in `out_size`
 */
size_t kl_build_frame(uint16_t type, const uint8_t * payload, uint16_t len, bool encrypted,
                      uint8_t * out, size_t out_size);

/**
 * @brief Parse a frame body: a cleartext packet, or (when `encrypted`) an AES-128-CBC body, decrypted in place.
 * @return ESP_OK with `packet` filled, or ESP_ERR_INVALID_SIZE / ESP_ERR_INVALID_RESPONSE
 */
esp_err_t kl_parse_body(uint8_t * body, size_t len, bool encrypted, kl_packet_t * packet);

/** @brief The host's proof for the Kestrel's 16-byte nonce: 36 bytes (ICD §5.3 step 9) */
esp_err_t kl_auth_response(const uint8_t nonce[16], uint8_t out[36]);

/** @brief True when the Kestrel's reply (at least 32 bytes) proves the key on our nonce (ICD §5.3 step 15) */
bool kl_auth_verify(const uint8_t * reply, size_t len, const uint8_t nonce[16]);

static inline uint16_t kl_u16(const uint8_t * p) {
    return (uint16_t) (p[0] | p[1] << 8);
}

// One Target Card target, from a SetFullSoln record (ICD §7.9). Holds as the Kestrel sends them: mil, negative
// elevation = up, negative windage = right
typedef struct {
    uint32_t range_cm;
    float elevation_mil;
    float wind1_mil;        // at WS1, the average wind
    float wind2_mil;        // at WS2, the maximum wind
} kl_target_t;

/** @brief Decode a SetFullSoln COMMAND payload (opcode included) */
esp_err_t kl_decode_full_solution(const kl_packet_t * packet, kl_target_t * target);

/** @brief Check the framing, CRC and crypto against the ICD's test vectors; logs and returns the result */
esp_err_t kl_self_test(void);

#ifdef __cplusplus
}
#endif

#endif  // KESTREL_LINK_H_
