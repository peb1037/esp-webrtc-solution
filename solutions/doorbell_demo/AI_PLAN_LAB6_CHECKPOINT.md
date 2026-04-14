# AI-assisted development plan (Lab 6) — checkpoint archive

This file preserves the original Lab 6 checkpoint/stopping point notes that existed in `AI_PLAN.md` before Lab 7 report updates.

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
