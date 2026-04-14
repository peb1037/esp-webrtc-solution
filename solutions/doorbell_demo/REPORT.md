# Lab 7 — Cloud service deployment for media data management

## A) Implementation explanation (how the Tasks were implemented)

### 1) Live viewing (LiveKit)

- **Device publishes A/V**: The ESP32-P4 firmware uses LiveKit **WHIP ingest** to publish H.264 video + Opus audio to LiveKit Cloud.
  - Firmware config lives in `main/settings.h` (`WEBRTC_USE_LIVEKIT_WHIP`, `LIVEKIT_WHIP_URL`, optional `LIVEKIT_WHIP_BEARER_TOKEN`).
  - WHIP signaling + WebRTC startup is implemented in `main/webrtc.c` (`start_webrtc()`).
  - Startup sequencing + Wi‑Fi and time-sync are handled in `main/main.c` (SNTP time sync before starting TLS services).

- **Webpage subscribes and plays**: The webpage uses LiveKit’s browser SDK to connect to the same room and subscribe to the remote tracks.
  - Backend generates a viewer token at `GET /api/livekit/token` in `web_demo/server.js`.
  - Frontend connects/subscribes in `web_demo/src/main.js`.

### 2) Recording start/stop (S3)

- Recording is implemented **cloud-side** using **LiveKit Egress** (RoomComposite) to encode and upload MP4 to S3.
  - Start/stop endpoints: `POST /api/record/start` and `POST /api/record/stop` in `web_demo/server.js`.
  - Output is configured as S3 upload using AWS keys from `web_demo/.env`.
  - The UI triggers these endpoints from `web_demo/src/main.js`.

### 3) MQTT control signals (AWS IoT Core)

- The web backend publishes MQTT control commands via AWS IoT Core Data Plane.
  - Photo capture command is sent in `POST /api/photo` in `web_demo/server.js` (publishes JSON to `AWS_IOT_TOPIC_CMD`).

- The device connects to AWS IoT Core using TLS mutual auth and subscribes to the command topic.
  - MQTT client implementation: `main/cloud/aws_iot.c`.
  - Command parsing + dispatch: `main/cloud/cloud_ctrl.c`.
  - IoT endpoint + topics: `main/settings.h`.
  - Client certificate/key embedded at build time: `main/aws_iot_client_cert.pem`, `main/aws_iot_client_key.pem`.

### 4) Photo capture + cloud storage (S3)

- Web backend generates a presigned S3 PUT URL for a new JPEG object and sends it to the device via MQTT.
  - Implemented in `POST /api/photo` in `web_demo/server.js`.

- Device captures a JPEG and uploads it with an HTTP PUT to the presigned URL.
  - Capture: `media_sys_capture_photo_jpeg()` in `main/media_sys.c`.
  - Upload: `http_upload_put_binary()` in `main/cloud/http_upload.c`.
  - Orchestration: `main/cloud/cloud_ctrl.c` (`cmd: "photo"`).

### 5) Photo preview + recorded video playback (S3)

- Backend lists recent S3 objects and returns signed GET URLs.
  - Photos: `GET /api/photos` in `web_demo/server.js`.
  - Videos: `GET /api/videos` in `web_demo/server.js`.

- Frontend displays photos and plays videos using those signed URLs.
  - Implemented in `web_demo/src/main.js`.

## B) AI-assisted development plan (Markdown)

See `AI_PLAN.md`.

## C) AI Q&A summary (Markdown)

See `AI_QA.md`.

## D) AI usage statement

See `AI_USAGE_STATEMENT.md`.
