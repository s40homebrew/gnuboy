# Gnuboy on S40

Native GB/GBC emulation for the Nokia 6303 classic, built against the shared
S40 homebrew SDK. The launcher includes the SDK's Nokia 6300 and C3 profiles;
those phones use the ordinary display backend unless a compatible fast driver
is available. The application uses native input, storage and rendering after
Java launches it.

## Install and play

Install `dist/Gnuboy.jar` and its matching JAD in internal Gallery. Copy
uncompressed `.gb` / `.gbc` ROMs into Gallery's **Received files** folder
(`C:/predefgallery/predeffilereceived/`). Choose **Select ROM** in the launcher,
open **Received files**, choose a file, then **Start Gnuboy**. File access may
request the phone's ordinary Java permission before listing games. On the
6303, Java's file picker cannot see ROM files placed directly at Gallery's root.

The native game list also reads `gnuboy.txt` in Gallery, with one ROM filename
per line; lines starting with `#` are comments. Relative filenames resolve to
`C:/predefgallery/`, so a Received files entry is
`predeffilereceived/game.gbc`. The freely distributed Tobu Tobu Girl, Tobu Tobu
Girl DX and µCity files named `tobu.gb`, `tobudx.gb` and `ucity.gbc` are discovered
in Received files if present; older copies at Gallery's root remain usable
through the native list. No ROMs are included in the JAR.

| Control | Action |
| --- | --- |
| D-pad | Move |
| 1 / 3 | A / B |
| Center | A |
| 5 / 0 | Start / Select |
| * or either softkey | Pause menu |
| # | Switch fit / original-size pixels |

QWERTY phones also accept Z / X for A / B, Space for A and Enter for Start.
Controls depend on the phone's physical simultaneous-key support.

The pause menu provides resume, save/load state, scaling, sound, another ROM,
a short benchmark and exit. Key 7 switches sound on or off; keys 2 / 8 raise
or lower its displayed volume by 10 percentage points. Sound settings take
effect when resuming. Right softkey saves battery memory and exits. Fit
mode scales 160×144 to 240×216 on the 6303; original-size mode uses crisp 1:1
pixels. Emulation runs at the GB frame rate, with picture skipping when the
phone display cannot present every guest frame.

## Saves and limitations

Battery RAM is saved on pause and normal exit beside its ROM, e.g.
`game.gbc.sav`. The save-state slot is `game.gbc.gns`; it belongs to this port's
state format. Save writes stage a temporary file and retain the previous file
as `.bak`. The SDK's final rename uses copying, so writes are not atomic across
power loss. Leave free internal storage for these extra copies.

Use the pause menu before switching applications or disconnecting power. The
port does not write to a memory card by default. RTC cartridges retain their
clock while closed but do not advance it by wall-clock time between sessions.

Sound uses the SDK's optional native PCM driver, with mono output at 11025 Hz.
It is enabled by default on supported firmware; the pause menu shows
**Sound unavailable** if no compatible driver opens. Muting output still runs
the emulated sound hardware. Ordinary MBC0, MBC1, MBC2, MBC3 and MBC5 cartridges
are supported; unsupported cartridge hardware is
rejected. Compressed ROMs and boot ROMs are not used. Large cartridges still
need enough free native memory. Gnuboy prioritizes speed over cycle accuracy,
so some games may have compatibility issues.

## Build

Keep this repository beside the SDK repository named `main`, or set
`S40_SDK_ROOT` to your SDK checkout. Follow the SDK's toolchain setup, then run:

```sh
bash s40/build.sh
```

The build writes `s40/dist/Gnuboy.jar` and `Gnuboy.jad`. Native code targets
ARMv5TE and uses the SDK's `s40/video.h`, `s40/input.h`, `s40/frame_clock.h`,
launch argument handoff, file picker and C runtime. Firmware-specific addresses
belong to SDK adapters, not the emulator.

Run compact core/port checks with generated ROMs and address/undefined-behavior
sanitizers:

```sh
python3 s40/tests/check.py
```

Pause-menu key 9 measures 120 emulated frames without presentation and another
120 with presentation, then restores the pre-benchmark state. The report is
`Gnuboy-bench.txt` in internal Gallery. Presentation time includes completion of
the final transfer. These are throughput measurements, not a gameplay test or
independent counts of visible panel refreshes. Benchmarks run with sound output
closed and restore the game before returning to the pause menu.

Pausing or exiting also writes `Gnuboy-session.txt`, containing the last play
interval's emulated/presented frame counts, input event counts and audio queue
results. These small reports stay in Gallery's root, separate from ROM saves.
