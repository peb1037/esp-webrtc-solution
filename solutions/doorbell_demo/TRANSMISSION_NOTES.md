# Video & audio transmission notes (Lab 6)

This project streams media from ESP32‑P4 to a browser using WebRTC.

## Where capture happens
- `main/media_sys.c`
  - `media_sys_buildup()` registers default enc/dec and builds two subsystems:
    - `build_capture_system()`
      - Creates a video source (ESP32‑P4 path calls `esp_video_init()` and then opens a V4L2 source `"/dev/video0"`).
      - Creates an audio source backed by the board codec via `esp_capture_new_audio_dev_src()`.
      - Opens `esp_capture` with both sources and returns a single `capture_handle`.
    - `build_player_system()` creates local renderers (used for tones and optional local playback).

## Where WebRTC is configured and started
- `main/webrtc.c`
  - `start_webrtc(url)` configures the peer connection media:
    - Video codec: `ESP_PEER_VIDEO_CODEC_H264` with `VIDEO_WIDTH/VIDEO_HEIGHT/VIDEO_FPS`.
    - Audio codec: OPUS (or G711A if OPUS is disabled), send/recv.
  - It calls `media_sys_get_provider()` and then `esp_webrtc_set_media_provider(webrtc, &media_provider)`.
    - This binds the `esp_capture` handle as the media source for WebRTC.
  - Finally `esp_webrtc_start(webrtc)` establishes signaling and (when accepted) the peer connection.

## How the board joins a room
- `main/lab6main.c`
  - On Wi‑Fi connect, it generates a room id from the MAC and calls `start_webrtc()`.
  - The console also provides a `join <room>` command for manual room selection.

## Browser side
- The browser joins the same room using the DoorBell demo page hosted on the signaling server.
- Once connected, the browser receives the WebRTC media tracks (video/audio) and plays them live.
