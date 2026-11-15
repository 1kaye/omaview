# Omaview

Omaview is Omarchy's picture viewer and media player, Media Viewer in the launcher. Its one job is showing what someone opened: double-click a photo, a video or a song in the file manager, or `omaview IMG_2041.jpg` in a terminal, and look at it, with the keyboard doing everything a person reaches for while looking — next, zoom, turn, pause, seek, louder, subtitles, fullscreen — and nothing else on screen while they are not. It replaces two defaults at once: imv for pictures and mpv for video. It is not a photo library, not a streaming client, not an editor, and every feature has to justify itself against "would someone who just opened this file reach for this?"

It began as Omaplay, a video player, and became Omaview when it took on pictures: a phone's camera folder is photos and clips side by side, and stepping through it shouldn't mean two apps with two sets of keys.

It is built the way Omarchy's own apps are (Omacut, Monologue, Hype, Signed, Collage, Lore): C++ and Qt Quick, one window, keyboard first, following the Omarchy theme and language live, built with CMake into a static core library shared by the app and the tests, and installed as an Arch package so it is in the launcher.

## Why replace imv as the default

imv is fast and small, and like mpv a poor default for someone who did not choose it: a window with no visible way to do anything, its keys its own, blind to the theme and the language, configured in a text file. Omarchy's own config gave it the bindings people asked for — print (Ctrl+P), move to the Trash (Ctrl+X, Ctrl+Shift+X), rotate (Ctrl+R), edit in Omasnap (Ctrl+E) — and Omaview has each of those as a key of its own, with one change: turning a picture turns what's shown, not the file (see *Decisions*). Pictures and videos in one window also means one set of keys for both and a folder of photos and clips stepped through as one.

## Why replace mpv as the default

mpv is a superb engine and a poor default for someone who did not choose it. Opened from a file manager, it is a black window with no visible way to do anything: the on-screen controller appears only on hover, in mpv's own look, its keys are its own (`9` and `0` for volume, `j` for subtitles), it ignores the Omarchy theme and language, and its configuration is a text file. People who know mpv love it, and Omarchy keeps it installed for them and for the jobs it does well behind the scenes: the webcam overlay in screen recording, the yt-dlp host, the low-battery sound. What changes is only which app a video opens in.

## The engine: ffmpeg, through Qt

Omaview does not decode anything itself. Playback is Qt Multimedia's FFmpeg backend — `MediaPlayer`, `VideoOutput` and `AudioOutput` — which is ffmpeg's demuxers and decoders feeding Qt's scene graph, with hardware decoding through VA-API or Vulkan where the GPU has it and software where it does not. Everything else that needs to know about the file goes to the `ffmpeg` and `ffprobe` command-line tools through `QProcess`, as the skill and Omacut do, never by linking libav:

- `ffprobe` reads what Qt Multimedia does not expose: chapters, and each subtitle stream's codec, so a bitmap track (PGS, VobSub, DVB) is never handed to Qt's subtitle path, which handles only text. Streams are matched to Qt's per-type track indices by their order within their type; Qt's own metadata (title, language) names them, with `ffprobe`'s as the fallback.
- `ffmpeg` grabs the exact frame at a time for `omaview frame`. In the window, **Copy frame** and **Save frame** use the decoded frame on screen itself, latched when the key is pressed, so what is copied is what was seen, at the video's own resolution.
- `ffmpeg` makes the scrub previews: at most 100 small frames, each a fast seek of its own at low priority, started a few seconds after playback so it never competes with the first frames, cancelled when the file changes, and cached under `~/.cache/omaview/` by the file's path, size and modification time, with the cache pruned to 256 MB, oldest first. Only seekable files with a known duration get them.
- `ffmpeg` turns a sidecar subtitle file (`.srt`, `.vtt`, `.ass`) into WebVTT text, since Qt Multimedia plays only the subtitle tracks inside the file.

Creating any of Qt Multimedia's objects starts the backend, which probes the GPU's decoders on the GUI thread and costs a second or two on some machines, so the player lives in a `Loader` that turns on with the first file, as Hype's `videoLoader` does. The window is up first, the picture a moment later.

## Pictures

A picture never starts the video engine. The backend reads its header in a worker (`src/picture.{h,cpp}`: the format, the size upright by its EXIF orientation, frames, transparency, and the camera's EXIF from `src/exif.{h,cpp}`), and the window decodes it through Qt Quick's `Image`, asynchronously, keeping the last picture on screen until the next is ready, so stepping through a folder never flashes black. Which files are pictures is whatever Qt's image plugins read on the machine, PDF aside, so installing `kimageformats` brings HEIC, AVIF and JPEG XL with no change here. A file whose header Qt can't read is never handed to the window; it says "Can't show" and why.

- **Fit, never enlarge.** A picture shows whole, shrunk to the window and never enlarged past its own pixels. **+ / −**, the wheel and a touchpad pinch zoom about the pointer, down to fitting again; **0** fits, **1** is actual size. Zoomed in, a drag or the arrows move it about.
- **Arrows step.** Fitted, **← →** (and **PgUp/PgDn**, **N/P**) go to the previous and next file, **Home/End** to the first and last. In a video the same arrows seek: the keys mean what fits what's shown.
- **R / Shift+R** turn it a quarter right or left, as it shows; the file is never changed, and the next picture comes upright.
- **Animated** GIF and WebP play; **Space** pauses them.
- What's see-through shows over a dark checkerboard, so a black logo doesn't vanish into the letterbox.
- **I** shows the format, dimensions and megapixels, size, frames, and what the camera wrote: when it was taken, the camera, the lens, and the exposure (shutter, aperture, ISO, focal length as 35 mm).
- **Ctrl+C** copies it as it shows, turned, as PNG through `wl-copy`. **Ctrl+P** prints it to the default printer through `lp`, as Omarchy's imv binding did. **E** opens it in Omasnap, and a picture changed on disk while it's shown, as an editor saves it, is shown again once the writing settles.
- **Delete** moves the file shown, picture or video, to the Trash (the freedesktop one, through Qt) and shows the next; **Ctrl+Z** puts it back where it was. It doesn't ask first: the Trash and Ctrl+Z are the safety, as Omarchy's imv binding had it.
- The window opens at the first picture's shape, as large as fits in 80% of the screen and never larger than the picture, and keeps its size as the next file comes.
- No idle inhibitor and no MPRIS for a picture: Omaview registers as a player only once a video or song has played, so a photo never takes the media keys from Spotify.

## The design: bare

Four directions were drawn (`plans/mockups/`, HTML source and 2880×1800 renders): overlay controls that fade in over the picture, Omarchy chrome framing it, a folder shelf beside it, and bare. David picked **direction 3, bare** (`3-bare.png`), with **Tab revealing direction 1's overlay** (`1-overlay.png`). Omaview always starts bare; the window is the picture and nothing else:

- **A thin accent progress line** along the bottom edge, three pixels, the only thing always on screen.
- **An on-screen display** answers every key and fades after two seconds: the play state, time and file name in the top corner, and what just changed (speed, volume, subtitles, audio track, chapter) large in the middle with its keys beneath. Seeking grows the progress line into a timeline with the time and chapter at the playhead for as long as the display shows.
- **The pointer brings in nothing**, with one exception: hovering the bottom edge grows the line into the same timeline, with a frame from the preview strip above the pointer, and dragging or clicking it seeks. That is the overlay direction's best idea, kept where it does not cost the picture anything. The pointer hides after a second of stillness.
- **Clicking** the picture plays and pauses, **double-clicking** toggles fullscreen, and the **wheel** changes volume.
- The first file opened after installing shows "Press ? for every key" once in the display, because a window with nothing on it has to say how to drive it once.

**Tab** brings in the overlay and takes it away again: the file's name, size and codec in a row across the top with **Copy frame** and **Trim in Omacut**, and the transport across the bottom — play, seek, volume, the time, the timeline with chapter ticks and hover previews, and speed, subtitles, audio track and fullscreen, each with its key — over gradients so they read on any picture. It stays until Tab again, rather than fading, because someone who asked for it wants to use it; Esc also puts it away. It is for the mouse and for discovering what the keys do, and every key works the same whether it shows or not. It is never remembered: each file opens bare.

The letterbox around the picture is black whatever the theme, as every player's is; the display's panels take the theme's background and accent, so they read on any picture and match the desktop.

## What it does

### Opening

- `omaview` with no file opens an empty window that says what to do: drop a file, or Ctrl+O. `omaview open <files…>` (and bare `omaview <files…>`, which launchers use) shows or plays the first, in one window.
- **Ctrl+O** opens files through the portal picker. **Drop** files onto the window to play them.
- A URL (`omaview https://…/stream.m3u8`) plays what Qt's FFmpeg backend opens, which is a narrower set of protocols than the `ffmpeg` command line. A live or unseekable stream has no timeline: seeking, tenths, chapters and previews are off, and the display says "Live". Buffering is shown, and a stream that fails says why. Sites that need yt-dlp stay with mpv, which Omarchy's yt-dlp host already uses.
- The window opens at the video's aspect ratio, as large as fits in 80% of the monitor, and never larger than the video at 1:1 unless the video is small. A song opens a smaller square window.
- **The playlist.** Several files given at once (a selection opened from the file manager, or `omaview a.mkv b.mkv`) are the playlist, in the order given. A single file brings its folder: the other files beside it, in natural order (`2` before `10`). Videos go with everything, but songs and pictures never mix: a photo's folder brings its pictures and clips but not the voice memos, and a song's brings its songs but not `cover.jpg`. **N** and **P** step through it. When a file ends, Omaview stops on its last frame (`VideoOutput.endOfStreamPolicy: KeepLastFrame`) and offers the next one (Enter plays it); it never starts the next one by itself, because a folder of unrelated videos is not a series.

### Watching

- **Space** (and **K**) plays and pauses; clicking the picture does too, and double-clicking it toggles fullscreen. The second click of a double-click undoes the first one's pause, so a double-click leaves playback as it was.
- **←/→** seek 5 seconds, **J/L** 10 seconds, **Shift+←/→** one second, **0–9** jump to that tenth of the way through, **Home** to the start. Every seek lands at the time asked for; Qt Multimedia has no faster, keyframe-only mode to offer.
- **,** and **.** step one frame back and forward, pausing first. Qt Multimedia has no frame-step call, so a step seeks to just past the end, or just before the start, of the frame on screen by its own timestamps, which is right for variable frame rates too.
- **[** and **]** change speed in steps of 0.25×, from 0.25× to 4×, with pitch kept; **Backspace** resets to 1×.
- **↑/↓** change volume by 5%, **M** mutes. Volume is remembered.
- **F** toggles fullscreen and **Esc** leaves it. In fullscreen the pointer hides when still.
- **C** cycles subtitles (off, then each track, then each sidecar file) and **A** cycles audio tracks. Each is named by its language and title, never "Track 2" unless the file names nothing. Bitmap subtitle tracks (PGS, VobSub) are listed in the info card as not supported and skipped by C, rather than crashing Qt's text-only subtitle path. Subtitles draw at the bottom of the picture, in the window's font, on a translucent band, at a size proportional to the picture's height. A track in the user's Omarchy language is chosen first for audio; subtitles start off unless the only audio is in another language and a subtitle track matches theirs.
- **I** shows a card with what `ffprobe` knows: container, every stream with its codec, resolution, frame rate, bitrate and language, and the chapters. Whether Qt chose a hardware decoder is not something it reports, so the card does not claim either way.
- Chapters, when the file has them, are ticks on the timeline, the current one is named while seeking, and **Page Up/Page Down** jump between them.

### Remembering

- **Where you stopped.** Closing a file longer than five minutes before its last 5% remembers the position, keyed by path, size and modification time, in `~/.local/state/omaview/positions.json`. Several windows can be open at once, so a write takes a lock file, rereads, changes only its own entry and replaces the file atomically; the file keeps the 500 most recent entries. Opening it again resumes a few seconds earlier, and says so in the status line with "Start over" beside it (**Home**). Finishing it forgets the position. The file holds paths and numbers, never anything from the video.
- **Volume, mute and the last subtitle and audio language picked**, so a person who always wants English subtitles gets them on the next film.

### Taking a picture out

- **Copy frame** (Ctrl+Shift+C) puts the frame on screen when the key was pressed, at the video's own resolution, on the clipboard as PNG through `wl-copy`, which keeps serving it after Omaview quits; without `wl-copy` it falls back to Qt's clipboard. Subtitles are not burned in.
- **Save frame** (Ctrl+S) does the same into a file through the portal's save dialog, suggesting `<name>-<time>.png` in `~/Pictures`. Written atomically; nothing beside the video is ever touched.
- **Trim in Omacut** (E) pauses and opens the file in Omacut, when Omacut is installed, and is absent when it is not. Omacut takes only a file today; opening at the current position needs a small `--at` in Omacut, proposed there rather than worked around here. Cutting is Omacut's job, and the two apps should hand a file between them rather than either growing the other's features.

### Behaving like a player on the desktop

- **Media keys and `playerctl`.** Each window is an MPRIS player on the session bus (`org.mpris.MediaPlayer2.omaview.instance<pid>`, as the specification asks of apps with several instances), so the keyboard's play, pause, next and previous keys, Omarchy's media bindings, and the bar's now-playing all work, as `mpv-mpris` gives mpv today.
- **The screen stays on while a video plays.** While playing a video (not a song), Omaview holds a Wayland idle inhibitor (`zwp_idle_inhibit_manager_v1`) on its window's surface, and destroys it on pause, end or close. Hyprland applies it, and Omarchy's shell idle monitor respects inhibitors, so the screensaver and lock wait exactly while something is being watched, rather than for as long as the window has focus, which is what a window rule could give.
- **One window per launch.** Opening another file from the file manager opens another window, as every Omarchy app does. The app id is `omaview`, and Omarchy floats it centred with a rule of its own rather than mpv's, whose fixed size would fight Omaview sizing its window to the video.

## Shortcuts

| Key | Action |
|---|---|
| Space / K | Play or pause |
| ← → / Shift+← → / J L | Seek 5 s / 1 s / 10 s |
| 0–9 / Home | Jump to that tenth / to the start |
| , . | Step one frame back, forward |
| Page Up / Page Down | Previous, next chapter |
| [ ] / Backspace | Slower, faster / normal speed |
| ↑ ↓ / M | Volume / mute |
| C / A | Cycle subtitles / audio tracks |
| Tab | Show or hide the overlay |
| F / Esc | Fullscreen / leave fullscreen or put the overlay away |
| N / P | Next, previous file in the folder |
| I | What's in the file |
| Ctrl+Shift+C / Ctrl+S | Copy frame / save frame |
| E | Trim in Omacut |
| Ctrl+O | Open files |
| ? | Every shortcut |
| Q / Ctrl+Q | Quit |

The keys follow what people already know from YouTube and VLC (Space, K, J, L, F, M, ←/→, ↑/↓, 0–9, `,` `.`), not mpv's. Buttons, the status line and the `?` overlay take their key labels from one table in QML, so they cannot drift apart.

## Architecture

```text
CMakeLists.txt
bin/build  bin/test  .github/workflows/test.yml
src/main.cpp              app setup and command dispatch
src/cli.{h,cpp}           headless commands
src/backend.{h,cpp}       what QML talks to: the player, the open file, the playlist, frames, Omacut
src/probe.{h,cpp}         ffprobe: streams, chapters, title, as plain structs
src/picture.{h,cpp}       a picture's header: format, size upright, frames, transparency
src/exif.{h,cpp}          the camera's EXIF from JPEG, PNG, WebP and TIFF
src/frames.{h,cpp}        ffmpeg: exact frames, the scrub strip and its cache
src/subtitles.{h,cpp}     sidecar discovery, conversion through ffmpeg, a cue list
src/playlist.{h,cpp}      the folder's playable files in natural order
src/positions.{h,cpp}     resume positions and remembered choices, saved atomically
src/mpris.{h,cpp}         the MPRIS adaptor
src/inhibit.{h,cpp}       the Wayland idle inhibitor, through wayland-scanner's code
src/filepicker.h  src/portalfilepicker.{h,cpp}   from Omacut
src/theme.{h,cpp}  src/translator.{h,cpp}        the shared templates
src/texts.h               translated text for code outside a QObject
src/Main.qml              the window: picture, display, keys
src/PictureView.qml       a picture: fit, zoom, pan, turn
src/ControlsOverlay.qml  src/ControlButton.qml  src/Timeline.qml  src/Icon.qml  src/Format.js
src/i18n/<code>.json  src/skill.md  src/resources.qrc
tests/omaview_tests.cpp  tests/ui_tests.cpp  tests/test_cli.py
share/omaview.desktop  share/omaview.svg
```

The backend owns the `QMediaPlayer` and creates it with the first file, so starting Omaview with nothing to play never starts the FFmpeg backend at all. The window's `VideoOutput` sits in a `Loader` that turns on at the same moment. The player draws into a sink of the backend's, sharing the window's GPU context, which hands each frame to the `VideoOutput` from the decoder's thread and keeps the subtitle text for the window to draw itself, so embedded and sidecar subtitles share one style and the frame on screen is always at hand for Copy frame. Owning the player in C++ is what lets MPRIS, resume, track choice and the tests drive the same object. Nothing slow happens on the GUI thread: the folder listing, ffprobe, subtitle extraction, previews and frame conversion run in workers that a new file cancels, and remembered positions and choices are written in order on a thread of their own. Test fixtures are a few seconds of generated video and audio (`ffmpeg -f lavfi -i testsrc2`, `sine`), with chapters and a subtitle track, made by the tests rather than committed.

## Agents can drive it

The GUI is `omaview open [files…]`. These need no display, take `--json`, and exit 0 or 1 with errors on stderr; their output stays English for scripts:

- `omaview info <file>` — duration, streams, chapters, and the resume position if one is remembered; for a picture, its format, size upright, frames, and the camera's EXIF.
- `omaview frame <file> --at <time> -o out.png` — the exact frame, through the same code as Save frame.
- `omaview skill install` — installs a short agent skill bundled in the binary.

Anything more (converting, cutting, extracting audio) is ffmpeg's, which agents already know.

## Omarchy conventions

All of the skill's conventions apply: the whole theme palette live through `Theme`, light themes included, though the picture's surround stays black whatever the theme, as every player's does; Hyprland's rounding as `theme.radius`; every string through `qsTr`, with `src/i18n/<code>.json` for Omarchy's 50 languages and right-to-left layout under the mirrored root, except the timeline, which runs left to right in every language as time does; portal dialogs only; `?` for the overlay; and problems reported in the window: a file ffmpeg cannot open, a codec without a decoder, a missing `ffprobe`, each says so in plain words rather than showing a black picture.

## Testing

`bin/test` builds and runs the Qt Test binaries offscreen and the command line's Python tests against the built binary. It covers: probing generated fixtures (streams, languages, chapters); frame extraction at an exact time against expected pixels; the scrub strip and its cache invalidation when the file changes; sidecar discovery and conversion; the playlist's natural order and which files count as playable; resume positions (the five-minute and last-5% rules, keys by size and time, atomic writes); track choice by language; the catalogs; the CLI's exit codes and JSON; and the real `Main.qml` driven with keys against a fake player, failing on any QML warning.

The offscreen platform proves layout and behaviour, not decoding: hardware decoding, audio output, A/V sync, MPRIS with real media keys, the idle inhibitor, fullscreen, and how the picture looks on a real compositor are proved on a worker built from the Omarchy ISO, never on the coordinator's desktop.

## Order of work

1. **Play**: the window, the `Loader`ed player, open, drop, the transport, every watching key, fullscreen, the on-screen display, errors in the window. Someone can open a video and watch it from the keyboard.
2. **Know the file**: probe, chapters, track names and choice, sidecar subtitles, the info card, the scrub strip.
3. **Live on the desktop**: MPRIS, the inhibit, resume, the folder playlist, copy and save frame, Trim in Omacut.
4. **Agents**: the CLI and its skill.
5. **Ship**: translations, README with a screenshot from the offscreen harness, icon, a `v0.1.0` release, and the package in `omacom/omarchy-pkgs`.

## Making it Omarchy's default viewer and player

Only after the package is published in `omarchy-pkgs`, as one Omarchy pull request, English only:

- `install/omarchy-base.packages` gains `omaview` and `kimageformats`; `mpv` and `mpv-mpris` stay (the webcam overlay, the yt-dlp host, the battery sound use mpv). `imv` goes, once nothing in Omarchy calls it: `bin/omarchy-plymouth-preview` opens its preview with `imv -f`, which becomes `omaview` (F for fullscreen).
- `default/applications/mimeapps.list`: the image types that name `imv.desktop` and the video types that name `mpv.desktop` name `omaview.desktop`. Audio is a separate, later change: Omarchy ships no audio defaults today, and taking them over is its own decision.
- `applications/imv.desktop` ("Image Viewer") and `applications/mpv.desktop` ("Media Player"), Omarchy's own overrides, go, so the launcher has one Media Viewer rather than three entries for two jobs. mpv's packaged entry is hidden with `NoDisplay` rather than removed, since mpv stays installed.
- `config/imv/config` goes with imv; its bindings are Omaview's keys (Ctrl+P, Delete, E), except rotating the file.
- `default/hypr/apps/system.lua` floats `imv` and `mpv`; `omaview` floats and centres with a rule of its own, without a fixed size, since it sizes itself to the picture.
- `manual/22-guis.md` gains an Omaview section.
- A migration installs `omaview` and changes only what Omarchy itself shipped: Omarchy's own `mimeapps.list` changes in the same pull request, and a user's `~/.config/mimeapps.list` is left alone, because an `imv.desktop` or `mpv.desktop` line there cannot be told apart from someone who chose them on purpose.
- `share/omaview.desktop` uses `Exec=omaview open %F` and lists the image, video and audio types, so Omaview appears under "Open with" for every one.

## Decisions taken without asking

David was away while this was built, so these were decided rather than asked, and are easy to revisit:

- **The name.** It was planned as Showtime, but Arch's `extra` already has GNOME's Showtime, which owns `/usr/bin/showtime`, and the AUR and sox claim the other obvious names (`play` is sox's). David named it `omaplay`, after `omacut` and `omawrite`, and then `omaview` when it took on pictures, for the package, the binary, the app id, the `.desktop` file and the icon, with **Media Viewer** as its name in the launcher, as Omawrite is Write. Nothing had shipped as `omaplay`, so nothing migrates.
- **Turning a picture doesn't change the file.** Omarchy's imv binding rotated the file with `mogrify`, which re-encodes a JPEG and loses quality each time, and every Omarchy app leaves the user's originals alone. EXIF orientation already turns most sideways phone photos upright on its own. Changing a picture is an editor's job, and E hands it to Omasnap.
- **Delete doesn't ask.** The Trash is recoverable, and Ctrl+Z brings the file straight back.
- **The arrows mean what fits.** Stepping through photos with the arrows is what every image viewer does, and seeking with them what every video player does; a folder of both gets both, with N/P stepping either way.
- **Songs and pictures don't mix in a folder**, so an album's cover isn't a track and a camera folder's voice memos aren't pictures.
- **mpv stays installed.** Omarchy uses it for the webcam overlay, the yt-dlp host and the battery sound, and people who chose it keep it.
- **Omaview starts bare every time**, and Tab is not remembered.
- **Resume is on by default**, past five minutes, and says so when it happens with a one-key way out.
- **The next file in the folder is offered, never started.**
- **Keys follow YouTube and VLC**, not mpv, because the people a default player is for know those.

## Out of scope

A photo library or albums, thumbnails of a folder, slideshows, editing a picture (that is Omasnap), a media library, playlist files, streaming services and yt-dlp, an equalizer, video filters and picture adjustments, A–B loops, casting, recording, editing (that is Omacut), visualisations for music, plugins, scripting, and a configuration file. Each is a reasonable feature of some player and a step away from this one being simple.
