# Smart Bird Feeder Web Demo

Single-page webpage UI + small backend that provides:

- Live viewing (LiveKit WebRTC room)
- Recording start/stop (LiveKit egress to S3)
- Photo capture (web -> AWS IoT MQTT -> device; device uploads to S3 via presigned PUT)
- Photo preview + recorded video playback (signed S3 GET URLs)

## Setup

1) Create a `.env` file next to `package.json` (copy from `.env.example`).

2) Install dependencies:

```bash
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
npm install
```

3) Run UI + backend together:

```bash
npm run dev
```

- UI: http://localhost:5173
- Backend: http://localhost:5174

## Where do the `.env` values come from?

You get these from your LiveKit project and your AWS account. If your class/lab provided “given” credentials/endpoints, use those.

### LiveKit values (for Live View + Recording)

- `LIVEKIT_URL`
	- LiveKit Cloud: your project URL looks like `https://<your-project>.livekit.cloud`
	- Find it in the LiveKit Cloud dashboard for your project.
- `LIVEKIT_API_KEY` / `LIVEKIT_API_SECRET`
	- LiveKit Cloud dashboard → your project → create or view an API key/secret.
- `LIVEKIT_ROOM`
	- Any room name you want. Default `birdfeeder` is fine.

### AWS values (for photos + playback + device control)

- `AWS_REGION`
	- The AWS region you’re using, e.g. `us-east-1`.
- `S3_BUCKET`
	- The S3 bucket name where media will live.
	- AWS Console → S3 → create a bucket (or use an existing one).
- `AWS_IOT_DATA_ENDPOINT`
	- AWS Console → IoT Core → Settings → “Device data endpoint”.
	- Looks like `xxxxxxxxxxxxx-ats.iot.<region>.amazonaws.com`.
- `AWS_IOT_TOPIC_CMD`
	- Must match firmware command topic (see [main/settings.h](../main/settings.h)). Default is `birdfeeder/cmd`.

### AWS credentials (used by the local backend)

This demo backend needs AWS credentials to:
- presign S3 upload URLs for the device
- list S3 objects and generate signed playback links
- publish MQTT commands to IoT Core
- pass S3 upload credentials to LiveKit Egress (recording)

Set **either**:
- `AWS_ACCESS_KEY_ID` + `AWS_SECRET_ACCESS_KEY` in `.env` (simplest)

Or use AWS CLI credentials on your machine (still required), but note: LiveKit Egress S3 upload in this demo reads keys from `.env`.

## Minimal “get it working” checklist

1) Create a LiveKit Cloud project → copy `LIVEKIT_URL`, create an API key/secret.
2) In AWS: create an S3 bucket → set `AWS_REGION` + `S3_BUCKET`.
3) In AWS IoT Core: copy the “Device data endpoint” → set `AWS_IOT_DATA_ENDPOINT`.
4) Create AWS access keys (IAM user) that can publish to IoT and read/write that S3 bucket.
5) Fill `.env`, restart `npm run dev`.

## Connecting the device to LiveKit via WHIP

This backend can create/reuse a LiveKit WHIP ingress for the device:

- GET http://localhost:5174/api/livekit/ingress

Use the returned `whipEndpoint` in firmware as the WHIP URL (it already includes the stream key).

