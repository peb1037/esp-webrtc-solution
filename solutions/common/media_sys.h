/* Media system

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_webrtc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Build media system
 *
 * @param[in]  rtc_handle  WebRTC handle
 *
 * @return
 *      - 0       On success
 *      - Others  Fail to build
 */
int media_sys_buildup(void);

/**
 * @brief  Get media provider
 * 
 * @param[out]  provider  Media provider to be returned
 *
 * @return
 *      - 0       On success
 *      - Others  Invalid argument
 */
int media_sys_get_provider(esp_webrtc_media_provider_t *provider);

/**
 * @brief  Play captured media directly
 *
 * @return
 *      - 0       On success
 *      - Others  Fail to capture or play
 */
int test_capture_to_player(void);

/**
 * @brief  Play music
 *
 * @param[in]  data      Music data to be played
 * @param[in]  size      Music data size
 * @param[in]  duration  Play duration, when duration over data duration will replay
 *
 * @return
 *      - 0       On success
 *      - Others  Fail to play
 */
int play_music(const uint8_t *data, int size, int duration);

/**
 * @brief  Stop music
 *
 * @return
 *      - 0       On success
 *      - Others  Fail to stop
 */
int stop_music(void);

/**
 * @brief  Capture a single JPEG frame from the camera
 *
 * @note  Requires that the capture system has been built with an MJPEG sink
 *        (the doorbell demo configures this during `media_sys_buildup()`).
 *
 * @param[out] out_jpeg      Allocated JPEG buffer (caller must free)
 * @param[out] out_jpeg_len  JPEG length
 * @param[in]  timeout_ms    Max time to wait for a frame
 *
 * @return
 *      - 0       On success
 *      - Others  On failure
 */
int media_sys_capture_photo_jpeg(uint8_t **out_jpeg, size_t *out_jpeg_len, int timeout_ms);

#ifdef __cplusplus
}
#endif