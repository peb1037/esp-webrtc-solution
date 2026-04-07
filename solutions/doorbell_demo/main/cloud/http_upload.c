#include "cloud/http_upload.h"

#include "esp_http_client.h"
#include "esp_log.h"

#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

static const char *TAG = "HTTP_UPLOAD";

int http_upload_put_binary(const char *url, const char *content_type, const uint8_t *data, size_t len, int timeout_ms)
{
    if (!url || !content_type || !data || len == 0) {
        return -1;
    }

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
        return -1;
    }

    esp_http_client_set_header(client, "Content-Type", content_type);
    esp_http_client_set_post_field(client, (const char *)data, (int)len);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PUT failed: %s", esp_err_to_name(err));
        return -1;
    }

    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "PUT HTTP status=%d", status);
        return -1;
    }

    ESP_LOGI(TAG, "PUT OK status=%d len=%u", status, (unsigned)len);
    return 0;
}
