#include "kestrel_link.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "psa/crypto.h"

#define TAG "KestrelLink"

#define KL_FLAG 0x7e
#define KL_ESC  0x7d
#define KL_BLOCK 16

// ICD §5.1: the link key, and P0 = AES-ECB-decrypt(KEY, C0), the plaintext of the host proof's first block
static const uint8_t kl_key[16] = {
    0x98, 0xe4, 0x39, 0xab, 0x2d, 0x99, 0x9f, 0x58, 0xc1, 0xc5, 0x8a, 0x7e, 0x66, 0xc7, 0x90, 0x5d,
};
static const uint8_t kl_p0[16] = {
    0xab, 0x61, 0xe6, 0x98, 0xab, 0x5a, 0xde, 0x15, 0x07, 0xdc, 0x35, 0x1b, 0x5a, 0x89, 0x79, 0xa5,
};
static const uint8_t kl_zero_iv[16] = {0};

static psa_key_id_t kl_key_id = 0;


uint16_t kl_crc_x25(const uint8_t * data, size_t len) {
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : crc >> 1;
        }
    }
    return crc ^ 0xffff;
}


void kl_deframer_reset(kl_deframer_t * d) {
    d->len = 0;
    d->in_frame = false;
    d->escape = false;
}


// ICD §3: a 7e opens a frame outside one and closes it inside one; bytes outside a frame (00 padding) are skipped
bool kl_deframer_push(kl_deframer_t * d, uint8_t byte) {
    if (byte == KL_FLAG) {
        bool done = false;
        if (d->in_frame && d->len > 0) {
            if (d->len >= 2 && kl_crc_x25(d->body, d->len - 2) == kl_u16(&d->body[d->len - 2])) {
                d->len -= 2;
                done = true;
            }
            else {
                d->bad_crc++;
            }
            d->in_frame = false;
        }
        else {
            d->in_frame = true;
        }
        if (!done) {
            d->len = 0;
        }
        d->escape = false;
        return done;
    }
    if (!d->in_frame) {
        return false;
    }
    if (byte == KL_ESC) {
        d->escape = true;
        return false;
    }
    if (d->len >= sizeof(d->body)) {
        d->overflow++;
        kl_deframer_reset(d);
        return false;
    }
    d->body[d->len++] = d->escape ? byte ^ 0x20 : byte;
    d->escape = false;
    return false;
}


static esp_err_t kl_key_ready(void) {
    if (kl_key_id != 0) {
        return ESP_OK;
    }
    psa_status_t init = psa_crypto_init();
    if (init != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_crypto_init failed: %d", (int) init);
        return ESP_FAIL;
    }
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, 128);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_CBC_NO_PADDING);
    psa_status_t status = psa_import_key(&attributes, kl_key, sizeof(kl_key), &kl_key_id);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "key import failed: %d", (int) status);
        kl_key_id = 0;
        return ESP_FAIL;
    }
    return ESP_OK;
}


// AES-128-CBC with a zero IV over whole blocks (ICD §5.2); `in` and `out` must not overlap
static esp_err_t kl_cbc(bool encrypt, const uint8_t * in, size_t len, uint8_t * out) {
    if (len == 0 || len % KL_BLOCK != 0 || kl_key_ready() != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    psa_cipher_operation_t op = PSA_CIPHER_OPERATION_INIT;
    psa_status_t status = encrypt ? psa_cipher_encrypt_setup(&op, kl_key_id, PSA_ALG_CBC_NO_PADDING)
                                  : psa_cipher_decrypt_setup(&op, kl_key_id, PSA_ALG_CBC_NO_PADDING);
    size_t done = 0, last = 0;
    if (status == PSA_SUCCESS) {
        status = psa_cipher_set_iv(&op, kl_zero_iv, sizeof(kl_zero_iv));
    }
    if (status == PSA_SUCCESS) {
        status = psa_cipher_update(&op, in, len, out, len, &done);
    }
    if (status == PSA_SUCCESS) {
        status = psa_cipher_finish(&op, out + done, len - done, &last);
    }
    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "AES-CBC %s failed: %d", encrypt ? "encrypt" : "decrypt", (int) status);
        psa_cipher_abort(&op);
        return ESP_FAIL;
    }
    if (done + last != len) {
        ESP_LOGE(TAG, "AES-CBC %s: %u of %u bytes", encrypt ? "encrypt" : "decrypt", (unsigned) (done + last),
                 (unsigned) len);
        return ESP_FAIL;
    }
    return ESP_OK;
}


size_t kl_build_frame(uint16_t type, const uint8_t * payload, uint16_t len, bool encrypted,
                      uint8_t * out, size_t out_size) {
    static uint8_t plain[KL_PACKET_MAX + 2 * KL_BLOCK];
    static uint8_t body[KL_PACKET_MAX + 2 * KL_BLOCK + 2];

    if ((size_t) len + 4 > KL_PACKET_MAX) {
        return 0;
    }

    // The packet; encrypted, it follows 16 random bytes and is padded with zeros to whole blocks (ICD §5.2 E-1)
    size_t offset = encrypted ? KL_BLOCK : 0;
    if (encrypted) {
        esp_fill_random(plain, KL_BLOCK);
    }
    plain[offset + 0] = type & 0xff;
    plain[offset + 1] = type >> 8;
    plain[offset + 2] = len & 0xff;
    plain[offset + 3] = len >> 8;
    if (len > 0) {
        memcpy(&plain[offset + 4], payload, len);
    }
    size_t body_len = offset + 4 + len;
    if (encrypted) {
        size_t padded = (body_len + KL_BLOCK - 1) / KL_BLOCK * KL_BLOCK;
        memset(&plain[body_len], 0, padded - body_len);
        body_len = padded;
        if (kl_cbc(true, plain, body_len, body) != ESP_OK) {
            return 0;
        }
    }
    else {
        memcpy(body, plain, body_len);
    }

    uint16_t crc = kl_crc_x25(body, body_len);
    body[body_len++] = crc & 0xff;
    body[body_len++] = crc >> 8;

    size_t n = 0;
    if (out_size < 2) {
        return 0;
    }
    out[n++] = KL_FLAG;
    for (size_t i = 0; i < body_len; i++) {
        uint8_t b = body[i];
        if (b == KL_FLAG || b == KL_ESC) {
            if (n + 2 >= out_size) {
                return 0;
            }
            out[n++] = KL_ESC;
            out[n++] = b ^ 0x20;
        }
        else {
            if (n + 1 >= out_size) {
                return 0;
            }
            out[n++] = b;
        }
    }
    out[n++] = KL_FLAG;
    return n;
}


static esp_err_t kl_parse_packet(const uint8_t * p, size_t len, kl_packet_t * packet) {
    if (len < 4) {
        return ESP_ERR_INVALID_SIZE;
    }
    packet->type = kl_u16(&p[0]);
    packet->len = kl_u16(&p[2]);
    if ((size_t) packet->len + 4 > len) {
        return ESP_ERR_INVALID_SIZE;
    }
    packet->payload = &p[4];
    return ESP_OK;
}


esp_err_t kl_parse_body(uint8_t * body, size_t len, bool encrypted, kl_packet_t * packet) {
    static uint8_t plain[KL_FRAME_MAX];

    // After the authentication every frame is encrypted (ICD §5.2); a cleartext body is told by its length field
    // (ICD §3 L2-5)
    if (encrypted && len >= 2 * KL_BLOCK && len % KL_BLOCK == 0 && len <= sizeof(plain)) {
        if (kl_cbc(false, body, len, plain) == ESP_OK) {
            memcpy(body, plain, len);
            if (kl_parse_packet(&body[KL_BLOCK], len - KL_BLOCK, packet) == ESP_OK) {
                return ESP_OK;
            }
        }
    }
    if (len >= 4 && kl_u16(&body[2]) == len - 4) {
        return kl_parse_packet(body, len, packet);
    }
    return ESP_ERR_INVALID_RESPONSE;
}


esp_err_t kl_auth_response(const uint8_t nonce[16], uint8_t out[36]) {
    uint8_t plain[32];
    memcpy(plain, kl_p0, 16);
    memcpy(&plain[16], nonce, 16);
    esp_err_t ret = kl_cbc(true, plain, sizeof(plain), out);
    memset(&out[32], 0, 4);
    return ret;
}


bool kl_auth_verify(const uint8_t * reply, size_t len, const uint8_t nonce[16]) {
    uint8_t plain[32];
    if (len < 32 || kl_cbc(false, reply, 32, plain) != ESP_OK) {
        return false;
    }
    return memcmp(&plain[16], nonce, 16) == 0;
}


static float kl_f32(const uint8_t * p) {
    float f;
    memcpy(&f, p, sizeof(f));
    return f;
}


esp_err_t kl_decode_full_solution(const kl_packet_t * packet, kl_target_t * target) {
    // [opcode][record]; the record's offsets are ICD §7.9's (range u24 cm at 2, holds f32 at 11, 15, 19)
    if (packet->type != KL_TYPE_COMMAND || packet->len < 2 + 23 || kl_u16(packet->payload) != KL_OP_SET_FULL_SOLN) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    const uint8_t * rec = packet->payload + 2;
    target->range_cm = rec[2] | rec[3] << 8 | (uint32_t) rec[4] << 16;
    target->elevation_mil = kl_f32(&rec[11]);
    target->wind1_mil = kl_f32(&rec[15]);
    target->wind2_mil = kl_f32(&rec[19]);
    return ESP_OK;
}


esp_err_t kl_self_test(void) {
    int failed = 0;
#define KL_CHECK(name, cond) do { if (!(cond)) { ESP_LOGE(TAG, "self test: %s failed", name); failed++; } } while (0)

    // ICD §13.5: ":A+\r" as a cleartext COMMAND 0x005e; the packet is 10 bytes (4 header, 6 payload)
    static const uint8_t args[] = {0x5e, 0x00, ':', 'A', '+', '\r'};
    static const uint8_t expected_frame[] = {0x7e, 0x00, 0x00, 0x06, 0x00, 0x5e, 0x00, 0x3a, 0x41, 0x2b, 0x0d,
                                             0x82, 0xd7, 0x7e};
    uint8_t frame[32];
    size_t n = kl_build_frame(KL_TYPE_COMMAND, args, sizeof(args), false, frame, sizeof(frame));
    KL_CHECK("frame", n == sizeof(expected_frame) && memcmp(frame, expected_frame, n) == 0);

    static kl_deframer_t d;
    kl_deframer_reset(&d);
    bool got = false;
    for (size_t i = 0; i < n; i++) {
        got |= kl_deframer_push(&d, frame[i]);
    }
    KL_CHECK("deframe", got && d.len == 10 && memcmp(d.body, &expected_frame[1], 10) == 0);

    // A captured authentication: the proof's blocks are C0 and the response the Kestrel accepted
    static const uint8_t nonce[16] = {0xf1, 0xc1, 0x5e, 0xbd, 0x89, 0x78, 0xea, 0x27, 0x5c, 0x25, 0x20, 0x34,
                                      0x95, 0xa9, 0x03, 0x00};
    static const uint8_t proof[32] = {
        0xa7, 0xf1, 0xd9, 0x2a, 0x82, 0xc8, 0xd8, 0xfe, 0x43, 0x4d, 0x98, 0x55, 0x8c, 0xe2, 0xb3, 0x47,
        0x4b, 0x8b, 0xb2, 0x50, 0x08, 0xed, 0x76, 0x72, 0x90, 0x5f, 0xcd, 0xfe, 0xdd, 0x74, 0xff, 0x0f,
    };
    uint8_t out[36];
    KL_CHECK("auth response", kl_auth_response(nonce, out) == ESP_OK && memcmp(out, proof, 32) == 0);
    KL_CHECK("auth verify", kl_auth_verify(proof, sizeof(proof), nonce));

    // An encrypted frame comes back as the same packet
    static const uint8_t ack[] = {0x7f, 0x00};
    uint8_t enc[96];
    n = kl_build_frame(KL_TYPE_ACK, ack, sizeof(ack), true, enc, sizeof(enc));
    kl_deframer_reset(&d);
    got = false;
    for (size_t i = 0; i < n; i++) {
        got |= kl_deframer_push(&d, enc[i]);
    }
    kl_packet_t packet;
    KL_CHECK("encrypted round trip", got && kl_parse_body(d.body, d.len, true, &packet) == ESP_OK &&
             packet.type == KL_TYPE_ACK && packet.len == 2 && kl_u16(packet.payload) == 0x007f);
#undef KL_CHECK

    ESP_LOGI(TAG, "self test %s", failed == 0 ? "PASS" : "FAIL");
    return failed == 0 ? ESP_OK : ESP_FAIL;
}