/* General settings

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Video resolution settings
 */
#if CONFIG_IDF_TARGET_ESP32P4
#define VIDEO_WIDTH  1280
#define VIDEO_HEIGHT 960
#define VIDEO_FPS    15
#else
#define VIDEO_WIDTH  320
#define VIDEO_HEIGHT 240
#define VIDEO_FPS    10
#endif

/**
 * @brief  Set for wifi ssid
 */
#define WIFI_SSID     "XXXX"

/**
 * @brief  Set for wifi password
 */
#define WIFI_PASSWORD "XXXX"

/**
 * @brief  AWS IoT Core MQTT endpoint (ATS)
 *
 * Example: "xxxxxxxxxxxxx-ats.iot.us-east-1.amazonaws.com"
 */
#define AWS_IOT_ENDPOINT "ai9n23epuqspd-ats.iot.us-east-1.amazonaws.com"

/**
 * @brief  AWS IoT MQTT client id (optional)
 */
#define AWS_IOT_CLIENT_ID ""

/**
 * @brief  MQTT topic for device commands (web -> device)
 */
#define AWS_IOT_TOPIC_CMD "birdfeeder/cmd"

/**
 * @brief  MQTT topic for device events (device -> web)
 */
#define AWS_IOT_TOPIC_EVT "birdfeeder/evt"

/**
 * @brief  Use LiveKit WHIP ingest instead of the default APPRTC demo signaling
 */
#define WEBRTC_USE_LIVEKIT_WHIP (1)

/**
 * @brief  LiveKit WHIP ingest URL
 */
#define LIVEKIT_WHIP_URL "https://livebirdfeeder-9z22w034.whip.livekit.cloud/w/ScLMk4xA4HLJ"

/**
 * @brief  LiveKit WHIP Bearer token
 */
#define LIVEKIT_WHIP_BEARER_TOKEN ""

/**
 * @brief  Whether enable data channel
 */
#define DATA_CHANNEL_ENABLED (false)

#if CONFIG_IDF_TARGET_ESP32P4
/**
 * @brief  GPIO for ring button
 *
 * @note  When use ESP32P4-Fuction-Ev-Board, GPIO35(boot button) is connected RMII_TXD1
 *        When enable `NETWORK_USE_ETHERNET` will cause socket error
 *        User must replace it to a unused GPIO instead (like GPIO27)
 */
#define DOOR_BELL_RING_BUTTON  35
#else
/**
 * @brief  GPIO for ring button
 *
 * @note  When use ESP32S3-KORVO-V3 Use ADC button as ring button
 */
#define DOOR_BELL_RING_BUTTON  5

#endif

#define WEBRTC_SUPPORT_OPUS

#ifdef __cplusplus
}
#endif
