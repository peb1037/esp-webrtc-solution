# AI-assisted development plan (Lab 7)

## Goal
Deploy and demo a cloud-enabled “smart bird feeder” media pipeline:

- Live viewing of ESP32-P4 A/V on a webpage via LiveKit (WebRTC)
- Recording start/stop via LiveKit egress writing MP4 to AWS S3
- Photo capture triggered from webpage via MQTT (AWS IoT Core) and uploaded to S3 via presigned PUT
- Preview photos + playback recorded videos from S3 on the webpage

## Step-by-step plan
1. Configure LiveKit (live view + egress recording)
   - Create a LiveKit Cloud project.
   - Set `LIVEKIT_URL`, `LIVEKIT_API_KEY`, `LIVEKIT_API_SECRET`, and `LIVEKIT_ROOM` in `web_demo/.env`.
   - Use the web backend to create/reuse a WHIP ingress and obtain the `whipEndpoint` for the device.

2. Configure AWS (S3 + IoT Core MQTT)
   - Create an S3 bucket and pick prefixes (photos/recordings).
   - In AWS IoT Core: create a Thing/certificates + attach policy that allows connect/subscribe/publish.
   - In `main/settings.h`, set `AWS_IOT_ENDPOINT` and ensure `AWS_IOT_TOPIC_CMD` matches the web backend.
   - Paste the IoT client certificate + private key into `main/aws_iot_client_cert.pem` and `main/aws_iot_client_key.pem`.

3. Configure firmware (ESP32-P4)
   - Set `WIFI_SSID` / `WIFI_PASSWORD` in `main/settings.h` (or use `wifi` CLI).
   - Set `WEBRTC_USE_LIVEKIT_WHIP=1` and set `LIVEKIT_WHIP_URL` to the `whipEndpoint` (includes stream key).
   - Build/flash; validate boot logs show Wi‑Fi connected + MQTT started + WHIP ingest started.

4. Run the webpage UI + backend
   - In `web_demo/`, run `npm run dev`.
   - Open the UI at `http://localhost:5173`.
   - Confirm “Live” connects and shows inbound video dimensions and bitrate.

5. Demo required functions end-to-end
   - Live view: verify audio/video plays.
   - Recording: click start/stop; confirm an MP4 appears in the S3 recordings prefix and in the “Videos” list.
   - Photo capture: click capture; confirm a JPEG appears in the S3 photos prefix and in the “Photos” list.

## Validation checklist
- Device:
  - Wi‑Fi connects reliably; system time is set (SNTP) before TLS services.
  - AWS IoT MQTT shows CONNECTED and receives JSON commands.
  - WHIP ingest starts and stays connected.
- Web:
  - Live view subscribes to at least one remote video track and shows non-zero `vw`/`vh`.
  - Recording start/stop returns `egressId` and an MP4 shows up in S3.
  - Photo capture publishes MQTT command; device uploads; photo is visible in S3 and UI.

## Flowchart (components + data flow)
```mermaid
flowchart LR
  USER[Browser UI\nweb_demo (Vite)] -->|HTTP| WEB[Local backend\nExpress server.js]

  subgraph LiveKit[LiveKit Cloud]
    LK_ROOM[Room\nLive viewing] 
    LK_ING[WHIP Ingress\n(H.264 + Opus)]
    LK_EGR[Egress\nRoomComposite MP4]
  end

  subgraph AWS[AWS]
    IOT[AWS IoT Core\nMQTT Broker]
    S3[S3 Bucket\nphotos/ + recordings/]
  end

  DEV[ESP32-P4 Firmware\nDoorbell/Birdfeeder demo] -->|WHIP publish\nA/V| LK_ING
  LK_ING --> LK_ROOM
  USER -->|LiveKit token| WEB
  WEB -->|JWT token| USER
  USER -->|subscribe| LK_ROOM

  USER -->|Start/Stop recording| WEB
  WEB -->|Egress start/stop| LK_EGR
  LK_EGR -->|MP4 upload| S3

  USER -->|Capture photo| WEB
  WEB -->|presigned PUT url| DEV
  WEB -->|MQTT cmd JSON| IOT
  DEV -->|subscribe cmd| IOT
  DEV -->|HTTP PUT JPEG| S3
  WEB -->|list + signed GET| S3
  WEB -->|photo/video URLs| USER
```
