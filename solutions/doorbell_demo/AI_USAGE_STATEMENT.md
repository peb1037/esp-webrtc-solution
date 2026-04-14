# AI usage statement (Lab 7)

AI tools were used to:

- Identify where LiveKit integration should live (WHIP ingest on-device, token/egress/ingress management in the web backend).
- Map each required lab feature (live view, recording, photo capture, media listing/playback) to the correct source files in the firmware and the `web_demo` app.
- Spot reliability issues (e.g., TLS services needing valid device time) and adjust the startup sequencing accordingly.
- Draft the step-by-step AI-assisted plan and data-flow diagram for the report.

All changes were reviewed by the developer and validated through building/flashing the firmware and exercising the webpage UI against LiveKit + AWS services.
