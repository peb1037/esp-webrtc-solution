# AI usage statement (Lab 6)

AI tools were used to:
- Rapidly locate the correct source files responsible for capture (`main/media_sys.c`) and streaming (`main/webrtc.c`, `main/lab6main.c`).
- Identify the relevant ESP-IDF/Kconfig options for selecting the OV5647 sensor and a MIPI CSI default format.
- Troubleshoot ESP-IDF flashing/monitoring issues on Windows by identifying and terminating stuck monitor/flash processes holding the serial port.

All changes were reviewed by the developer and validated by rebuilding and flashing the firmware.
