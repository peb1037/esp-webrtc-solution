# AI-assisted development plan (Lab 6)

## Stop point (2026-04-07 night)
- Goal status: signaling/connection succeeds, but browser still shows no video.
- Most likely current blocker: build system dependency state after local `esp_peer` replacement to 1.4.0.
- Last known failing symptoms during build regenerate:
   - repeated CMake re-run loops
   - missing `build/managed_components_list.temp.cmake`
   - component manager traceback ending in `KeyError: ComponentName(idf, espressif__eppp_link)`
- Important context:
   - `components/esp_peer` was replaced with 1.4.0 content.
   - backup component was moved out of `components` to `_component_backups/esp_peer_1_3_4`.
   - multiple runtime debug edits are present in `main` and `components/esp_webrtc`.

### First actions to resume tomorrow
1. Reset generated dependency/build state only (do not revert source edits).
2. Run one clean configure/build and confirm CMake loop is gone.
3. Flash and validate if video egress bitrate becomes non-zero.

### Suggested command sequence (from `solutions/doorbell_demo`)
```powershell
Remove-Item -Recurse -Force build, managed_components -ErrorAction SilentlyContinue
Remove-Item -Force dependencies.lock -ErrorAction SilentlyContinue
idf.py reconfigure
idf.py build
```

### Validation checks after successful build+flash
- Device log no longer shows invalid mids (audio/video mid should not be 255).
- Backend ingress list shows non-zero video bitrate and non-zero dimensions.
- Browser live video element advances with non-zero width/height.

## Step-by-step plan
1. Confirm target + peripherals
   - Target: ESP32-P4
   - Camera: OV5647 (MIPI CSI)
   - Audio codec: ES8311
2. Validate the capture pipeline in the project
   - Camera is initialized via `esp_video_init()` (CSI/DVP selection)
   - Frames are produced via `esp_capture` video source and audio device source
3. Validate the WebRTC pipeline
   - WebRTC signaling joins a room on the Espressif demo server
   - Media provider is set to the capture handle so encoded media is sent to the peer
4. Configure sensor/codec in Kconfig
   - Select OV5647 and a suitable MIPI output format
   - Ensure ISP pipeline support is enabled
   - Ensure ES8311 codec support is enabled
5. Build → flash → monitor
   - Build the project
   - Flash the image to the board
   - Confirm boot logs show successful Wi‑Fi + room join
6. Verify in browser
   - Open the DoorBell web page on the signaling server
   - Join the room printed by the board
   - Confirm live video and audio playback

## Flowchart (components + data flow)
```mermaid
flowchart LR
  CAM[OV5647 Camera\nMIPI CSI-2] --> CSI[ESP32-P4 CSI Receiver]
  CSI --> ISP[ISP Pipeline]
  ISP --> V4L2[/dev/video0\nV4L2 Device]
  V4L2 --> CAPV[esp_capture\nVideo Source]

  MIC[Mic / Line-In] --> ES8311[ES8311 Codec]
  ES8311 --> CAPA[esp_capture\nAudio Device Source]

  CAPV --> CAP[esp_capture\nCapture Handle]
  CAPA --> CAP

  CAP --> WEBRTC[esp_webrtc + esp_peer\nH.264 video + audio]
  WEBRTC --> NET[Wi‑Fi]
  NET --> SIG[Signaling Server\nwebrtc.espressif.com]
  NET --> BROWSER[Browser\nDoorBell Demo UI]

  SIG -. room join / signaling .- WEBRTC
  WEBRTC -. RTP/RTCP media .- BROWSER
```
