idf_component_register(SRCS "webrtc.c"  "main.c" "board.c" "media_sys.c"
                       "cloud/aws_iot.c" "cloud/http_upload.c" "cloud/cloud_ctrl.c"
                   EMBED_TXTFILES "ring.aac" "open.aac" "join.aac"
                              "aws_iot_client_cert.pem" "aws_iot_client_key.pem"
                   INCLUDE_DIRS "."
                   PRIV_REQUIRES common esp_webrtc esp_peer codec_board av_render media_lib_sal webrtc_utils
                              espressif__esp_video espressif__esp_video_codec espressif__esp_cam_sensor espressif__esp_capture
                              esp_wifi esp_event esp_netif esp_timer nvs_flash console driver
                              mqtt esp_http_client json)
