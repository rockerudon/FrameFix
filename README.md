# FrameFix

An [Ashita v4](https://www.ashitaxi.com/) plugin for **Final Fantasy XI**.

FrameFix runs the client in 60 FPS mode and corrects the client-side timing that
normally feels wrong once the game is no longer locked to its original 30 FPS.
Its goal is to keep character animations, UI window animations, and chat
scrolling running at the correct speed even when the framerate dips under load.

It does **not** change movement speed, cooldowns, buff timers, or any server
state. It only adjusts client-side animation/frame pacing.

## Features

- Sets and holds the client in 60 FPS mode.
- Compensates animation timing so motion plays at the correct real-time speed
  instead of slowing down when the framerate drops below 60.
- Uses a smoothed frame-time average so animations stay stable instead of
  reacting to per-frame jitter.
- Keeps native chat-window scrolling and UI window animations in sync at 60 FPS.
- Includes light camera collision/jitter smoothing for 60 FPS mode.

## Install

1. Download `FrameFix.dll` from the [latest release](../../releases) (or the
   `dist/` folder).
2. Copy it into your Ashita plugins folder:

   ```text
   Ashita/plugins/FrameFix.dll
   ```

3. Load it in game:

   ```text
   /load framefix
   ```

The plugin enables itself automatically when it loads.

## Commands

| Command | Description |
| --- | --- |
| `/framefix on` | Enable the 60 FPS timing fixes. Prints `FrameFix enabled.` |
| `/framefix off` | Restore the original client timing. Prints `FrameFix disabled.` |
| `/framefix status` | Print the current state (`enabled` or `disabled`). |

## Building from source

Requires the **Visual Studio 2022 C++ x86 build tools** (the FFXI client is
32-bit, so the plugin must be built as x86).

1. Make sure the Ashita plugin SDK headers are available and update the `$sdk`
   path at the top of `build.ps1` to point at your `Ashita/plugins/sdk` folder.
2. Run:

   ```powershell
   powershell -ExecutionPolicy Bypass -File build.ps1
   ```

   This produces `dist/FrameFix.dll`.

## Notes

FrameFix fixes *timing*, not rendering performance. It does not lower draw
distance, hide players, remove effects, or skip rendering work to gain FPS.

If the game is genuinely rendering at a very low framerate, motion can still
look choppy simply because fewer frames are being drawn. FrameFix keeps the
timing correct, but it is not a renderer optimization plugin.

## License

Released under the MIT License. See [LICENSE](LICENSE).

## Credits

Created by **rockerudon**.
