#include "cloud/http_upload.h"

#include "esp_http_client.h"
#include "esp_log.h"

#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

static const char *TAG = "HTTP_UPLOAD";

#define HTTP_UPLOAD_MAX_ATTEMPTS 3

int http_upload_put_binary(const char *url, const char *content_type, const uint8_t *data, size_t len, int timeout_ms)
{
    if (!url || !content_type || !data || len == 0) {
        return -1;
    }

    esp_err_t last_err = ESP_FAIL;
    int last_status = -1;
    for (int attempt = 1; attempt <= HTTP_UPLOAD_MAX_ATTEMPTS; attempt++) {
        esp_http_client_config_t cfg = {
            .url = url,
            .method = HTTP_METHOD_PUT,
            .timeout_ms = timeout_ms > 0 ? timeout_ms : 20000,
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
            .crt_bundle_attach = esp_crt_bundle_attach,
#endif
            .buffer_size = 2048,
            .buffer_size_tx = 2048,
        };

        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) {
            ESP_LOGE(TAG, "PUT init failed (attempt %d/%d)", attempt, HTTP_UPLOAD_MAX_ATTEMPTS);
            continue;
        }

        esp_http_client_set_header(client, "Content-Type", content_type);
        esp_http_client_set_post_field(client, (const char *)data, (int)len);

        last_err = esp_http_client_perform(client);
        last_status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);

        if (last_err == ESP_OK && last_status >= 200 && last_status < 300) {
            ESP_LOGI(TAG, "PUT OK status=%d len=%u attempt=%d", last_status, (unsigned)len, attempt);
            return 0;
        }

        ESP_LOGW(TAG, "PUT retry attempt=%d/%d err=%s status=%d",
                 attempt, HTTP_UPLOAD_MAX_ATTEMPTS, esp_err_to_name(last_err), last_status);
    }

    if (last_err != ESP_OK) {
        ESP_LOGE(TAG, "PUT failed after retries: %s", esp_err_to_name(last_err));
        return -1;
    }
    ESP_LOGE(TAG, "PUT HTTP status=%d after retries", last_status);
    return -1;
}
