# Lab 7 — Cloud service deployment for media data management

## A) Implementation explanation (how the Tasks were implemented)

### 1) Overall architecture used for the Task

The implementation uses a three-layer architecture:

- ESP32-P4 firmware (device side): captures camera/microphone media, publishes live stream to LiveKit via WHIP, receives AWS IoT MQTT commands, captures and uploads photos to S3 using presigned URLs.
- Local web backend (cloud bridge): provides REST APIs for the webpage, calls LiveKit server SDK and AWS SDK, publishes device commands to AWS IoT Core, and signs S3 URLs.
- Webpage frontend (user interface): triggers user actions and renders live stream, photos, and recorded videos.

This architecture maps directly to the Task requirement that webpage actions must trigger cloud and device operations and then display media results.

### 2) Firmware cloud connection design (reliable LiveKit + AWS)

#### 2.1 LiveKit live streaming from ESP32-P4

- LiveKit WHIP mode is enabled in `main/settings.h` with `WEBRTC_USE_LIVEKIT_WHIP`, `LIVEKIT_WHIP_URL`, and optional `LIVEKIT_WHIP_BEARER_TOKEN`.
- `start_webrtc()` in `main/webrtc.c` configures H.264 video and Opus audio, sets media direction to send-only for WHIP, binds media providers, and starts WebRTC.
- `join_room()` in `main/main.c` ensures network and time readiness before starting WHIP ingest.

Reliability mechanisms implemented:

- `cloud_ctrl.c` retries WebRTC startup with bounded retries (`VIDEO_START_MAX_ATTEMPTS`) and backoff delay.
- Start command checks guard against invalid conditions before starting (`network_is_connected`, non-empty WHIP URL, already-active session).

#### 2.2 AWS IoT Core MQTT connectivity

- `aws_iot_start()` in `main/cloud/aws_iot.c` creates an MQTT over TLS connection to AWS IoT Core using embedded client certificate and private key.
- On connect, the firmware subscribes to `AWS_IOT_TOPIC_CMD`; incoming data is forwarded to the registered callback.
- Topic names and endpoint are set in `main/settings.h` (`AWS_IOT_ENDPOINT`, `AWS_IOT_TOPIC_CMD`, `AWS_IOT_TOPIC_EVT`).
- Certificate and key are embedded through `EMBED_TXTFILES` in `main/CMakeLists.txt`.

Reliability mechanisms implemented:

- MQTT auto reconnect is enabled in the ESP MQTT config.
- `cloud_ctrl_on_network()` retries `aws_iot_start()` with bounded retries (`AWS_IOT_START_MAX_ATTEMPTS`) and delay.
- Event publishing logs failures for easier diagnosis when connectivity is degraded.

#### 2.3 Photo capture and upload path

- Device command handling in `main/cloud/cloud_ctrl.c` parses JSON command payloads.
- For `cmd=photo`, the firmware validates fields, captures JPEG with `media_sys_capture_photo_jpeg()`, uploads via `http_upload_put_binary()`, and publishes success/failure events.

### 3) Webpage implementation of required demo functions

#### 3.1 Live viewing

- Backend endpoint `GET /api/livekit/token` in `web_demo/server.js` issues a viewer token.
- Frontend `web_demo/src/main.js` connects to LiveKit room, subscribes to remote video/audio tracks, and attaches media to the page.

#### 3.2 Recording start/stop control

- Backend `POST /api/record/start` starts LiveKit Room Composite Egress and writes MP4 to S3.
- Backend `POST /api/record/stop` stops the active egress session.
- Frontend buttons call these endpoints and update UI status.

#### 3.3 Photo capture

- Frontend calls `POST /api/photo`.
- Backend generates presigned S3 PUT and signed GET URL, then publishes MQTT command with upload metadata to the device.
- Device captures and uploads the JPEG, then publishes completion event.

#### 3.4 Photo preview and recorded video playback

- Backend `GET /api/photos` and `GET /api/videos` list S3 objects and return signed URLs.
- Frontend renders image previews and video players directly from those signed URLs.

### 4) End-to-end command and data flow used in demo

1. User clicks a control on webpage.
2. Frontend calls backend API.
3. Backend sends command to cloud service (LiveKit or AWS IoT) and/or prepares S3 signed URLs.
4. Device receives MQTT command when required and executes media operation.
5. Media is delivered to LiveKit room (live) or stored in S3 (photo/recording).
6. Webpage retrieves and displays resulting media.

### 5) Demonstration checklist against success metric

The following checklist was used to verify the Task success metric:

- Stable cloud connectivity:
  - Firmware connects to Wi-Fi, synchronizes time, connects to AWS IoT, and starts LiveKit WHIP publish.
- Live viewing:
  - Webpage displays subscribed remote LiveKit track with valid video dimensions.
- Recording control:
  - Start and stop recording from webpage, then confirm MP4 appears in S3 and is playable from webpage.
- Photo capture and preview:
  - Trigger photo from webpage, confirm upload succeeds, then display photo via signed URL.
- Playback of cloud recordings:
  - List recorded videos from S3 and play them in webpage video element.

This demonstrates that the ESP32-P4 can connect stably to LiveKit and AWS services and that all required cloud-enabled functions are integrated and operable from the webpage UI.

## B) AI-assisted development plan (Markdown)

See `AI_PLAN.md`.

## C) AI Q&A summary (Markdown)

See `AI_QA.md`.

## D) AI usage statement

See `AI_USAGE_STATEMENT.md`.
