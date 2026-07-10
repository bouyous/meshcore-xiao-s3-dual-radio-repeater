# Email draft to the MeshCore team

**Subject:** Experimental dual-SX1262 repeater for XIAO ESP32-S3

Hello MeshCore team,

I have been working on a personal experimental repeater for mountainous terrain, using one Seeed Studio XIAO ESP32-S3 and two Wio-SX1262 for XIAO radio boards.

The goal is to keep a single MeshCore repeater identity while using one directional antenna for an inter-summit backhaul and one omnidirectional antenna for local users in the valley. Both radios remain on the same MeshCore RF profile. The firmware receives on both ports, serializes all transmissions, forwards a packet only to the opposite RF port, and sends locally generated advertisements sequentially on both ports.

The prototype is based on MeshCore repeater v1.16.0 (`07a3ca9`). I have published the source, isolated patch, wiring tutorial, diagrams, prebuilt firmware and bench-test report here:

https://github.com/bouyous/meshcore-xiao-s3-dual-radio-repeater

The current version also adds persistent CLI controls for per-port TX power, enable state, guard time and statistics. The device still appears as one repeater during a MeshCore scan, which is intentional.

Bench testing has confirmed both radios on one shared SPI bus, bidirectional RF operation, persistent configuration and sequential dual-port transmission. The repository clearly lists what has not yet been validated, especially final antenna isolation, long-duration load and mountain field testing.

This was developed as a personal project in collaboration with OpenAI ChatGPT/Codex. I performed the physical assembly and hardware testing; ChatGPT/Codex assisted with code analysis, implementation, build/flash operation and documentation.

If the approach is useful to MeshCore, I would be very happy for the team to review it, suggest architectural changes, or adapt any part of it for the community. I am not presenting it as an official or production-ready MeshCore feature.

Thank you for your work on MeshCore and for taking a look.

Best regards,

bouyous

