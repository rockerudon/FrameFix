# Changelog

## 1.0.0

- Initial public release.
- Locks the FFXI client to 60 FPS mode.
- Animation timing compensation driven by a smoothed (200 ms) frame-time
  average, so animations play at the correct real-time speed without jittering
  from per-frame frame-time noise.
- Native chat-window scroll and UI window animation catch-up at 60 FPS.
- Light camera collision/jitter smoothing for 60 FPS mode.
- Commands: `/framefix on`, `/framefix off`, `/framefix status`.
