# FrameFix

An [Ashita v4](https://www.ashitaxi.com/) plugin for **Final Fantasy XI**.

FrameFix runs the client in 60 FPS mode and corrects client-side UI timing that
normally feels wrong once the game is no longer locked to its original 30 FPS.
Its goal is to keep motion animations, UI window animations, and chat scrolling
running at the correct speed when the framerate dips under load.

It does **not** change movement speed, cooldowns, buff timers, or server state.
It only adjusts client-side frame pacing.

## Features

- Sets and holds the client in 60 FPS mode.
- Keeps character motion animations from playing in slow motion when FPS drops.
- Keeps native chat-window scrolling and UI window animations in sync at 60 FPS.
- Includes light camera collision/jitter smoothing for 60 FPS mode.
- Uses a scoped motion-frame hook for animation timing instead of replacing the
  global client timer used by sound, effects, camera, and UI systems.
- Uses a scoped animation blend/fade hook so motion transitions follow the same
  corrected visual timing without touching ambient sound or effect timers.

## Install

1. Download `FrameFix.dll` from the latest release or the `dist/` folder.
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
| `/framefix on` | Enable the 60 FPS timing fixes. |
| `/framefix off` | Restore the original client timing. |

## Building From Source

Requires the **Visual Studio 2022 C++ x86 build tools**. FFXI is a 32-bit
client, so the plugin must be built as x86.

Run:

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

This produces `dist/FrameFix.dll`.

## Notes

FrameFix fixes timing, not rendering performance. It does not lower draw
distance, hide players, remove effects, or skip rendering work to gain FPS.

If the game is genuinely rendering at a very low framerate, motion can still
look choppy because fewer frames are being drawn. FrameFix keeps timing sane,
but it is not a renderer optimization plugin.

A global timing hook is intentionally not included in the release build. A
review of the XIClient code shows that the same global client timing helper is
used by animation tracks, UI/input repeat, camera movement, sound elements,
volume fades, particles, and zone effects. Because of that, FrameFix does not
patch the global helper.

The normal motion fix patches the `AnimationTrack::UpdateFrame` frame advance
path and the `AnimationTrack::UpdateBlendState` blend/fade lifetime path. These
are scoped to animation tracks, so ambient sound and unrelated effect timers
should keep using the game's original timing.

## License

Released under the MIT License.
