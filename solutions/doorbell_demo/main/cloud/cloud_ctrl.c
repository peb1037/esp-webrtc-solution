#include "cloud/cloud_ctrl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "cJSON.h"

#include "cloud/aws_iot.h"
#include "cloud/http_upload.h"
#include "media_sys.h"
#include "settings.h"
#include "common.h"

static const char *TAG = "CLOUD_CTRL";

static void publish_evt(const char *evt, const char *detail_json)
{
    if (AWS_IOT_TOPIC_EVT[0] == 0) {
        return;
    }

    char payload[512];
    if (detail_json && detail_json[0]) {
        snprintf(payload, sizeof(payload), "{\"evt\":\"%s\",%s}", evt, detail_json);
    } else {
        snprintf(payload, sizeof(payload), "{\"evt\":\"%s\"}", evt);
    }
    aws_iot_publish(AWS_IOT_TOPIC_EVT, payload, 0, 1, 0);
}

static void handle_photo_cmd(cJSON *root)
{
    const cJSON *upload_url = cJSON_GetObjectItemCaseSensitive(root, "upload_url");
    const cJSON *public_url = cJSON_GetObjectItemCaseSensitive(root, "public_url");
    const cJSON *content_type = cJSON_GetObjectItemCaseSensitive(root, "content_type");

    if (!cJSON_IsString(upload_url) || upload_url->valuestring[0] == 0) {
        ESP_LOGW(TAG, "photo cmd missing upload_url");
        publish_evt("photo_error", "\"reason\":\"missing_upload_url\"");
        return;
    }

    const char *ctype = (cJSON_IsString(content_type) && content_type->valuestring[0]) ? content_type->valuestring : "image/jpeg";

    uint8_t *jpeg = NULL;
    size_t jpeg_len = 0;
    int ret = media_sys_capture_photo_jpeg(&jpeg, &jpeg_len, 8000);
    if (ret != 0 || !jpeg || jpeg_len == 0) {
        ESP_LOGE(TAG, "capture photo failed");
        publish_evt("photo_error", "\"reason\":\"capture_failed\"");
        return;
    }

    ESP_LOGI(TAG, "Uploading photo (%u bytes)", (unsigned)jpeg_len);
    ret = http_upload_put_binary(upload_url->valuestring, ctype, jpeg, jpeg_len, 25000);
    free(jpeg);

    if (ret != 0) {
        publish_evt("photo_error", "\"reason\":\"upload_failed\"");
        return;
    }

    if (cJSON_IsString(public_url) && public_url->valuestring[0]) {
        char detail[420];
        snprintf(detail, sizeof(detail), "\"url\":\"%s\"", public_url->valuestring);
        publish_evt("photo_uploaded", detail);
    } else {
        publish_evt("photo_uploaded", NULL);
    }
}

static void handle_video_control_cmd(bool start)
{
    if (start) {
#if WEBRTC_USE_LIVEKIT_WHIP
        if (!network_is_connected()) {
            publish_evt("video_ack", "\"state\":\"error\",\"reason\":\"network_disconnected\"");
            return;
        }
        if (LIVEKIT_WHIP_URL[0] == 0) {
            publish_evt("video_ack", "\"state\":\"error\",\"reason\":\"empty_whip_url\"");
            return;
        }
        int ret = start_webrtc((char *)LIVEKIT_WHIP_URL);
        if (ret == 0) {
            publish_evt("video_ack", "\"state\":\"started\"");
        } else {
            char detail[96];
            snprintf(detail, sizeof(detail), "\"state\":\"error\",\"reason\":\"start_failed\",\"code\":%d", ret);
            publish_evt("video_ack", detail);
        }
#else
        publish_evt("video_ack", "\"state\":\"error\",\"reason\":\"unsupported_without_whip\"");
#endif
    } else {
        int ret = stop_webrtc();
        if (ret == 0) {
            publish_evt("video_ack", "\"state\":\"stopped\"");
        } else {
            char detail[96];
            snprintf(detail, sizeof(detail), "\"state\":\"error\",\"reason\":\"stop_failed\",\"code\":%d", ret);
            publish_evt("video_ack", detail);
        }
    }
}

static void on_mqtt_msg(const char *topic, int topic_len, const char *data, int data_len, void *ctx)
{
    (void)ctx;

    if (!data || data_len <= 0) {
        return;
    }

    // Payload may be delivered in chunks; for this demo we assume small JSON.
    char *json = (char *)calloc(1, (size_t)data_len + 1);
    if (!json) {
        return;
    }
    memcpy(json, data, (size_t)data_len);

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) {
        ESP_LOGW(TAG, "Invalid JSON cmd (topic=%.*s)", topic_len, topic);
        return;
    }

    const cJSON *cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    if (!cJSON_IsString(cmd)) {
        cJSON_Delete(root);
        return;
    }

    ESP_LOGI(TAG, "CMD: %s", cmd->valuestring);
    if (strcmp(cmd->valuestring, "photo") == 0) {
        handle_photo_cmd(root);
    } else if (strcmp(cmd->valuestring, "ping") == 0) {
        publish_evt("pong", NULL);
    } else if (strcmp(cmd->valuestring, "video_start") == 0) {
        handle_video_control_cmd(true);
    } else if (strcmp(cmd->valuestring, "video_stop") == 0) {
        handle_video_control_cmd(false);
    } else if (strcmp(cmd->valuestring, "record_start") == 0) {
        // Recording is typically implemented as LiveKit egress (cloud-side), but we ack here.
        publish_evt("record_ack", "\"state\":\"started\"");
    } else if (strcmp(cmd->valuestring, "record_stop") == 0) {
        publish_evt("record_ack", "\"state\":\"stopped\"");
    }

    cJSON_Delete(root);
}

int cloud_ctrl_init(void)
{
    aws_iot_set_message_callback(on_mqtt_msg, NULL);
    return 0;
}

void cloud_ctrl_on_network(bool connected)
{
    if (connected) {
        aws_iot_start();
    } else {
        aws_iot_stop();
    }
}
