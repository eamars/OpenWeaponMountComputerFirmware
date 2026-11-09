#include "rc_net.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "mdns.h"

#include "app_cfg.h"
#include "remote_control.h"
#include "wifi.h"

#define TAG "RCNet"

// While a client is connected the task looks at the socket, the log and the switch this often. Only then: with no
// client, or no address, it blocks without a timeout, so it never wakes the chip out of light sleep (ADR-009)
#define RC_NET_POLL_MS 50
// Waiting for an address: one look an hour at most (pdMS_TO_TICKS overflows past about 71 minutes at 1 kHz)
#define RC_NET_ADDRESS_WAIT_MS (60 * 60 * 1000)
// A send that cannot go out in this time (the host stopped reading) ends the connection
#define RC_NET_SEND_TIMEOUT_MS 2000

// The log kept for the client: the newest lines, kept while no client is connected (what the unit logged during
// an idle is read after the wake). Lines are cut at RC_NET_LOG_LINE_MAX
#define RC_NET_LOG_BYTES (16 * 1024)
#define RC_NET_LOG_LINE_MAX 256
#define RC_NET_LOG_LOCK_WAIT_MS 10
#define RC_NET_LOG_CHUNK 1024

#define RC_NET_REPLY_SIZE 1024      // as the serial transport: room for `stats` with its stack report
#define RC_NET_READ_CHUNK 64


// ---- The log ring: written by any task that logs, read by the task --------------------------------------------

HEAPS_CAPS_ATTR static char log_ring[RC_NET_LOG_BYTES];
static size_t log_start = 0;        // the oldest byte
static size_t log_used = 0;         // whole lines only, each ending in '\n'
static uint32_t log_dropped = 0;    // lines lost (the oldest pushed out, or a line that could not be formatted)
static portMUX_TYPE log_lock = portMUX_INITIALIZER_UNLOCKED;

static vprintf_like_t console_vprintf = NULL;
static SemaphoreHandle_t log_format_lock = NULL;
HEAPS_CAPS_ATTR static char log_line[RC_NET_LOG_LINE_MAX];

static volatile bool rc_net_enabled = false;
static TaskHandle_t rc_net_task_handle = NULL;

// The task blocks in select() with no timeout while no client is connected. A datagram to this socket (loopback)
// wakes it when the switch or the network changes, so it closes the port and drops its mDNS service at once
static int wake_fd = -1;
static struct sockaddr_in wake_address;
// Counts the network's changes (an address got or lost, the station gone): serving ends when it moves, and starts
// again once the Wi-Fi service reports the connection (mDNS is new by then, so the service is announced again)
static volatile uint32_t net_generation = 0;
// The address, from the network's own events. The Wi-Fi service's "connected" bit only follows them from its task
// a moment later, so it says when mDNS is ready, not whether the address is still there
static volatile bool net_has_address = false;


// Make room by dropping the oldest lines, then append. Lock held
static void log_ring_put_locked(const char *line, size_t len) {
    while (RC_NET_LOG_BYTES - log_used < len) {
        size_t i = 0;
        while (i < log_used && log_ring[(log_start + i) % RC_NET_LOG_BYTES] != '\n') {
            i++;
        }
        i = i < log_used ? i + 1 : log_used;
        log_start = (log_start + i) % RC_NET_LOG_BYTES;
        log_used -= i;
        log_dropped++;
    }
    size_t at = (log_start + log_used) % RC_NET_LOG_BYTES;
    size_t first = len < RC_NET_LOG_BYTES - at ? len : RC_NET_LOG_BYTES - at;
    memcpy(&log_ring[at], line, first);
    memcpy(log_ring, line + first, len - first);
    log_used += len;
}


// Whole lines only, up to `max` bytes, so a reply sent after them never shares a line with the log
static size_t log_ring_take(char *out, size_t max) {
    portENTER_CRITICAL(&log_lock);
    size_t n = log_used < max ? log_used : max;
    for (size_t i = 0; i < n; i++) {
        out[i] = log_ring[(log_start + i) % RC_NET_LOG_BYTES];
    }
    while (n > 0 && out[n - 1] != '\n') {
        n--;
    }
    log_start = (log_start + n) % RC_NET_LOG_BYTES;
    log_used -= n;
    portEXIT_CRITICAL(&log_lock);
    return n;
}


static uint32_t log_take_dropped(void) {
    portENTER_CRITICAL(&log_lock);
    uint32_t dropped = log_dropped;
    log_dropped = 0;
    portEXIT_CRITICAL(&log_lock);
    return dropped;
}


// The ESP-IDF log's output: the console as before, and a copy for the client. Never waits on the network
static int rc_net_log_vprintf(const char *fmt, va_list args) {
    va_list copy;
    va_copy(copy, args);
    int ret = console_vprintf(fmt, args);

    if (rc_net_enabled && !xPortInIsrContext() && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        if (xSemaphoreTake(log_format_lock, pdMS_TO_TICKS(RC_NET_LOG_LOCK_WAIT_MS)) == pdTRUE) {
            int n = vsnprintf(log_line, sizeof(log_line), fmt, copy);
            if (n > 0) {
                size_t len = (size_t) n < sizeof(log_line) ? (size_t) n : sizeof(log_line) - 1;
                if (log_line[len - 1] != '\n') {
                    if (len < sizeof(log_line) - 1) {
                        len++;
                    }
                    log_line[len - 1] = '\n';   // a cut line, or a piece of one, still ends its line
                }
                portENTER_CRITICAL(&log_lock);
                log_ring_put_locked(log_line, len);
                portEXIT_CRITICAL(&log_lock);
            }
            xSemaphoreGive(log_format_lock);
        }
        else {
            portENTER_CRITICAL(&log_lock);
            log_dropped++;
            portEXIT_CRITICAL(&log_lock);
        }
    }

    va_end(copy);
    return ret;
}


static void wake_task(void) {
    if (wake_fd >= 0) {
        sendto(wake_fd, "w", 1, 0, (struct sockaddr *) &wake_address, sizeof(wake_address));
    }
}


static void net_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    net_has_address = base == IP_EVENT && id == IP_EVENT_STA_GOT_IP;
    net_generation++;
    wake_task();
    if (rc_net_task_handle != NULL) {
        xTaskNotifyGive(rc_net_task_handle);
    }
}


static void wake_open(void) {
    wake_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (wake_fd < 0) {
        ESP_LOGE(TAG, "wake socket: errno %d", errno);
        return;
    }
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = 0,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    socklen_t len = sizeof(wake_address);
    if (bind(wake_fd, (struct sockaddr *) &address, sizeof(address)) != 0 ||
        getsockname(wake_fd, (struct sockaddr *) &wake_address, &len) != 0) {
        ESP_LOGE(TAG, "wake socket bind: errno %d", errno);
        close(wake_fd);
        wake_fd = -1;
    }
}


static void wake_drain(void) {
    char buf[8];
    while (recv(wake_fd, buf, sizeof(buf), MSG_DONTWAIT) > 0) {
    }
}


// ---- The connection: task only ----------------------------------------------------------------------------------

typedef struct {
    int fd;
    bool broken;    // a send failed: closed after the command
} rc_net_client_t;

HEAPS_CAPS_ATTR static char rc_net_line[REMOTE_CONTROL_LINE_MAX + 1];
HEAPS_CAPS_ATTR static char rc_net_reply[RC_NET_REPLY_SIZE];
HEAPS_CAPS_ATTR static char rc_net_tx[RC_NET_REPLY_SIZE + 2];
HEAPS_CAPS_ATTR static char rc_net_log_chunk[RC_NET_LOG_CHUNK];


static bool send_all(int fd, const char *data, size_t len) {
    while (len > 0) {
        int sent = send(fd, data, len, 0);
        if (sent <= 0) {
            return false;
        }
        data += sent;
        len -= (size_t) sent;
    }
    return true;
}


// One line and its line ending in one send
static void client_send_line(rc_net_client_t *client, const char *line) {
    if (client->fd < 0 || client->broken) {
        return;
    }
    size_t len = strnlen(line, sizeof(rc_net_tx) - 2);
    memcpy(rc_net_tx, line, len);
    rc_net_tx[len++] = '\n';
    if (!send_all(client->fd, rc_net_tx, len)) {
        client->broken = true;
    }
}


// remote_control_write_fn: the snap's data lines
static void rc_net_write_line(const char *line, void *ctx) {
    client_send_line((rc_net_client_t *) ctx, line);
}


static void client_close(rc_net_client_t *client) {
    if (client->fd >= 0) {
        close(client->fd);
        ESP_LOGI(TAG, "Client disconnected");
    }
    client->fd = -1;
    client->broken = false;
}


static void client_send_log(rc_net_client_t *client) {
    uint32_t dropped = log_take_dropped();
    if (dropped > 0) {
        char note[48];
        snprintf(note, sizeof(note), "@drop %" PRIu32 " log lines", dropped);
        client_send_line(client, note);
    }
    size_t n;
    while (!client->broken && (n = log_ring_take(rc_net_log_chunk, sizeof(rc_net_log_chunk))) > 0) {
        if (!send_all(client->fd, rc_net_log_chunk, n)) {
            client->broken = true;
        }
    }
}


static void client_accept(int listen_fd, rc_net_client_t *client) {
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    int fd = accept(listen_fd, (struct sockaddr *) &from, &from_len);
    if (fd < 0) {
        return;
    }
    if (client->fd >= 0) {
        ESP_LOGI(TAG, "A new client replaces the connected one");
        client_close(client);
    }

    struct timeval send_timeout = {.tv_sec = RC_NET_SEND_TIMEOUT_MS / 1000, .tv_usec = 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));    // a reply goes out at once
    client->fd = fd;
    client->broken = false;

    char address[16];
    inet_ntoa_r(from.sin_addr, address, sizeof(address));
    ESP_LOGI(TAG, "Client %s connected", address);
}


// Command lines as the serial transport reads them: CR, LF or CRLF ends one; an over-long line is answered
// `@err too_long` and dropped up to its line ending
static void client_read(rc_net_client_t *client, size_t *line_len, bool *discarding) {
    char chunk[RC_NET_READ_CHUNK];
    int received = recv(client->fd, chunk, sizeof(chunk), 0);
    if (received <= 0) {
        client->broken = true;
        return;
    }
    for (int i = 0; i < received && !client->broken; i++) {
        char c = chunk[i];
        if (c == '\r' || c == '\n') {
            if (!*discarding && *line_len > 0) {
                rc_net_line[*line_len] = '\0';
                // The log written so far goes out first, so lines arrive in the order they happened
                client_send_log(client);
                remote_control_execute(rc_net_line, rc_net_reply, sizeof(rc_net_reply), rc_net_write_line, client);
                client_send_line(client, rc_net_reply);
            }
            *line_len = 0;
            *discarding = false;
            continue;
        }
        if (*discarding) {
            continue;
        }
        if (*line_len == REMOTE_CONTROL_LINE_MAX) {
            *discarding = true;
            *line_len = 0;
            client_send_line(client, "@err too_long");
            continue;
        }
        rc_net_line[(*line_len)++] = c;
    }
}


static int listen_open(void) {
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        ESP_LOGE(TAG, "socket: errno %d", errno);
        return -1;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(RC_NET_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(fd, (struct sockaddr *) &address, sizeof(address)) != 0 || listen(fd, 1) != 0) {
        ESP_LOGE(TAG, "bind/listen on port %d: errno %d", RC_NET_PORT, errno);
        close(fd);
        return -1;
    }
    return fd;
}


// While the switch is on and the unit has an address
static void serve(void) {
    int listen_fd = listen_open();
    if (listen_fd < 0) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        return;
    }
    // The Wi-Fi service starts mDNS before it reports the connection, so the service can be added now
    // The firmware version rides along, so `rc_run.py find` tells units apart by version as well as by name
    mdns_txt_item_t txt[] = {{"version", esp_app_get_description()->version}};
    esp_err_t ret = mdns_service_add(NULL, RC_NET_MDNS_SERVICE, RC_NET_MDNS_PROTO, RC_NET_PORT, txt,
                                     sizeof(txt) / sizeof(txt[0]));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "mDNS service not announced: %s", esp_err_to_name(ret));
    }
    ESP_LOGI(TAG, "Listening on port %d", RC_NET_PORT);

    rc_net_client_t client = {.fd = -1, .broken = false};
    size_t line_len = 0;
    bool discarding = false;
    const uint32_t generation = net_generation;

    while (rc_net_enabled && net_has_address && generation == net_generation) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(listen_fd, &read_set);
        int max_fd = listen_fd;
        if (wake_fd >= 0) {
            FD_SET(wake_fd, &read_set);
            max_fd = wake_fd > max_fd ? wake_fd : max_fd;
        }
        if (client.fd >= 0) {
            FD_SET(client.fd, &read_set);
            max_fd = client.fd > max_fd ? client.fd : max_fd;
        }
        // No client: block until one connects or the wake socket says something changed
        struct timeval poll = {.tv_sec = 0, .tv_usec = RC_NET_POLL_MS * 1000};
        int ready = select(max_fd + 1, &read_set, NULL, NULL, client.fd >= 0 ? &poll : NULL);
        if (ready < 0) {
            break;      // the network went away under the sockets
        }
        if (ready > 0 && wake_fd >= 0 && FD_ISSET(wake_fd, &read_set)) {
            wake_drain();
        }
        if (ready > 0 && FD_ISSET(listen_fd, &read_set)) {
            client_accept(listen_fd, &client);
            line_len = 0;
            discarding = false;
        }
        if (ready > 0 && client.fd >= 0 && FD_ISSET(client.fd, &read_set)) {
            client_read(&client, &line_len, &discarding);
        }
        if (client.fd >= 0) {
            client_send_log(&client);
        }
        if (client.broken) {
            client_close(&client);
        }
    }

    client_close(&client);
    close(listen_fd);
    mdns_service_remove(RC_NET_MDNS_SERVICE, RC_NET_MDNS_PROTO);   // may already be gone with mDNS itself
    ESP_LOGI(TAG, "Stopped listening");
}


static void rc_net_task(void *p) {
    wake_open();
    while (1) {
        // Off, or no address: wait for the switch or the network's next event
        if (!rc_net_enabled || !net_has_address) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        // An address: wait for the Wi-Fi service to have started mDNS on it
        if (wifi_wait_for_sta_connected(RC_NET_ADDRESS_WAIT_MS) != ESP_OK) {
            continue;
        }
        if (rc_net_enabled && net_has_address) {
            serve();
        }
    }
}


esp_err_t rc_net_init(void) {
    if (rc_net_task_handle != NULL) {
        return ESP_OK;
    }
    log_format_lock = xSemaphoreCreateMutex();
    if (log_format_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t created = xTaskCreateWithCaps(rc_net_task, "rc_net", RC_NET_TASK_STACK, NULL, RC_NET_TASK_PRIORITY,
                                             &rc_net_task_handle, HEAPS_CAPS_ALLOC_DEFAULT_FLAGS);
    if (created != pdPASS) {
        rc_net_task_handle = NULL;
        ESP_LOGE(TAG, "Failed to create the task");
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, net_event_handler, NULL));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_STOP, net_event_handler, NULL));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, net_event_handler, NULL));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, net_event_handler, NULL));

    net_has_address = wifi_is_sta_connected();  // an address got before the handlers were registered

    console_vprintf = esp_log_set_vprintf(rc_net_log_vprintf);
    return ESP_OK;
}


void rc_net_set_enabled(bool enabled) {
    if (enabled == rc_net_enabled) {
        return;
    }
    rc_net_enabled = enabled;
    ESP_LOGI(TAG, "Remote control over Wi-Fi %s", enabled ? "on" : "off");
    if (enabled && rc_net_task_handle != NULL) {
        xTaskNotifyGive(rc_net_task_handle);
    }
    wake_task();    // serving stops at once when switched off
}
