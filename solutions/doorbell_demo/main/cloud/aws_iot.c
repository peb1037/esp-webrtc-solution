#include "cloud/aws_iot.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "mqtt_client.h"

#include "settings.h"

#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

static const char *TAG = "AWS_IOT";

// These symbols come from main/CMakeLists.txt EMBED_TXTFILES
extern const uint8_t aws_iot_client_cert_pem_start[] asm("_binary_aws_iot_client_cert_pem_start");
extern const uint8_t aws_iot_client_cert_pem_end[] asm("_binary_aws_iot_client_cert_pem_end");
extern const uint8_t aws_iot_client_key_pem_start[] asm("_binary_aws_iot_client_key_pem_start");
extern const uint8_t aws_iot_client_key_pem_end[] asm("_binary_aws_iot_client_key_pem_end");

static esp_mqtt_client_handle_t s_client;
static bool s_connected;
static aws_iot_msg_cb_t s_msg_cb;
static void *s_msg_ctx;

static bool bytes_contains(const uint8_t *haystack, size_t haystack_len, const char *needle)
{
    if (!haystack || !needle) {
        return false;
    }
    size_t needle_len = strlen(needle);
    if (needle_len == 0 || haystack_len < needle_len) {
        return false;
    }
    for (size_t i = 0; i + needle_len <= haystack_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            s_connected = true;
            ESP_LOGI(TAG, "Connected");
            // Subscribe here so it re-subscribes after reconnects.
            if (AWS_IOT_TOPIC_CMD[0]) {
                esp_mqtt_client_subscribe(event->client, AWS_IOT_TOPIC_CMD, 1);
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            ESP_LOGW(TAG, "Disconnected");
            break;
        case MQTT_EVENT_DATA:
            if (s_msg_cb && event->topic && event->data) {
                s_msg_cb(event->topic, event->topic_len, event->data, event->data_len, s_msg_ctx);
            }
            break;
        case MQTT_EVENT_ERROR:
            if (event->error_handle) {
                ESP_LOGE(TAG, "MQTT error: type=%d tls=0x%x stack=0x%x verify=0x%x errno=%d", (int)event->error_handle->error_type,
                         (unsigned)event->error_handle->esp_tls_last_esp_err,
                         (unsigned)event->error_handle->esp_tls_stack_err,
                         (unsigned)event->error_handle->esp_tls_cert_verify_flags,
                         event->error_handle->esp_transport_sock_errno);
            } else {
                ESP_LOGE(TAG, "MQTT error (no details)");
            }
            break;
        default:
            break;
    }
}

void aws_iot_set_message_callback(aws_iot_msg_cb_t cb, void *ctx)
{
    s_msg_cb = cb;
    s_msg_ctx = ctx;
}

bool aws_iot_is_connected(void)
{
    return s_connected;
}

      static const char *cert_ptr_or_null(const uint8_t *start, const uint8_t *end)
{
    if (!start || !end || end <= start) {
        return NULL;
    }
    // Treat placeholder files as "not configured".
    if (bytes_contains(start, (size_t)(end - start), "PASTE ")) {
        return NULL;
    }
    return (const char *)start;
}

int aws_iot_start(void)
{
    if (s_client) {
        return 0;
    }
    if (AWS_IOT_ENDPOINT[0] == 0 || strstr(AWS_IOT_ENDPOINT, "xxxx") != NULL) {
        ESP_LOGW(TAG, "AWS_IOT_ENDPOINT not configured (see settings.h)");
        return -1;
    }

    const char *client_cert = cert_ptr_or_null(aws_iot_client_cert_pem_start, aws_iot_client_cert_pem_end);
    const char *client_key = cert_ptr_or_null(aws_iot_client_key_pem_start, aws_iot_client_key_pem_end);
    if (!client_cert || !client_key) {
        ESP_LOGW(TAG, "AWS IoT client cert/key not configured (see aws_iot_client_cert.pem / aws_iot_client_key.pem)");
        return -1;
    }

    char uri[160];
    snprintf(uri, sizeof(uri), "mqtts://%s:8883", AWS_IOT_ENDPOINT);

    esp_mqtt_client_config_t cfg = {
        .broker = {
            .address = {
                .uri = uri,
            },
            .verification = {
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
                .crt_bundle_attach = esp_crt_bundle_attach,
#endif
            },
        },
        .credentials = {
            .client_id = AWS_IOT_CLIENT_ID[0] ? AWS_IOT_CLIENT_ID : NULL,
            .authentication = {
                .certificate = client_cert,
                .key = client_key,
            },
        },
        .session = {
            .keepalive = 60,
        },
        .network = {
            .reconnect_timeout_ms = 3000,
            .timeout_ms = 10000,
            .disable_auto_reconnect = false,
        },
        .task = {
            .priority = 5,
            .stack_size = 6 * 1024,
        },
        .buffer = {
            .size = 4096,
        },
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return -1;
    }

    esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start failed: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        return -1;
    }
    ESP_LOGI(TAG, "Starting MQTT to %s", AWS_IOT_ENDPOINT);
    return 0;
}

void aws_iot_stop(void)
{
    if (!s_client) {
        return;
    }
    esp_mqtt_client_stop(s_client);
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    s_connected = false;
}

int aws_iot_subscribe(const char *topic, int qos)
{
    if (!s_client || !topic) {
        return -1;
    }
    return esp_mqtt_client_subscribe(s_client, topic, qos);
}

int aws_iot_publish(const char *topic, const char *payload, int payload_len, int qos, int retain)
{
    if (!s_client || !topic || !payload) {
        return -1;
    }
    return esp_mqtt_client_publish(s_client, topic, payload, payload_len, qos, retain);
}
