# AI Q&A summary (Lab 6)

- Q: Where is video/audio capture implemented?
  - A: The capture system is built in `main/media_sys.c` using `esp_capture` video + audio sources.

- Q: Where is WebRTC started and how does the board join a room?
  - A: Room join logic is in `main/lab6main.c` (auto-join on network connect) and `main/webrtc.c` (`start_webrtc()` opens signaling and starts WebRTC).

- Q: How does media reach the browser?
  - A: `main/webrtc.c` sets an `esp_webrtc_media_provider_t` from `media_sys_get_provider()`, which connects the capture handle to the WebRTC peer connection.

- Q: What camera interface is used for OV5647 on ESP32-P4?
  - A: MIPI CSI is used; camera init is performed via `esp_video_init()` path in `main/media_sys.c`.

- Q: What configuration changes are needed to meet the OV5647 requirement?
  - A: Enable `CONFIG_CAMERA_OV5647` and select an OV5647 MIPI default format (e.g., RAW10 1920x1080) while disabling other sensor selections.
