# AI Q&A summary (Lab 7)

- Q: How does the device stream live A/V to a webpage using LiveKit?
  - A: The ESP32 publishes A/V to a LiveKit WHIP ingress (`LIVEKIT_WHIP_URL`) in `main/webrtc.c`; the webpage uses `/api/livekit/token` (backend) and `livekit-client` (frontend) to subscribe to the LiveKit room.

- Q: Where does the WHIP URL come from and how is it used?
  - A: The web backend exposes `GET /api/livekit/ingress` (in `web_demo/server.js`) which creates/reuses a WHIP ingress and returns `whipEndpoint` (includes stream key). That value is copied into `LIVEKIT_WHIP_URL` in `main/settings.h`.

- Q: How is recording implemented and why doesn’t the device “record” locally?
  - A: Recording is done cloud-side using LiveKit egress (`/api/record/start` and `/api/record/stop` in `web_demo/server.js`) which writes MP4 to S3; the device just streams live.

- Q: How does photo capture work end-to-end?
  - A: The webpage calls `POST /api/photo` which creates a presigned S3 PUT URL and publishes an MQTT JSON command to AWS IoT Core. The device receives the MQTT command (in `main/cloud/cloud_ctrl.c`), captures a JPEG (`media_sys_capture_photo_jpeg`) and uploads it via HTTP PUT to the presigned URL.

- Q: What’s required for AWS IoT Core to connect reliably from the ESP32?
  - A: Configure the ATS endpoint in `main/settings.h`, and paste the client certificate + private key into `main/aws_iot_client_cert.pem` and `main/aws_iot_client_key.pem`. Also ensure the device clock is set (SNTP) before starting TLS connections.
