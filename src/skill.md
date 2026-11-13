---
name: omaplay
description: Inspect a video or music file and grab exact frames with Omaplay, Omarchy's media player, without a display. Use when asked what's in a video (duration, codecs, tracks, languages, chapters), for a still from a video at a time, or to open something for the user to watch.
---

# Omaplay

Omaplay is Omarchy's media player. Its commands below need no display; each takes `--json`, exits 0 on success and 1 on failure with the reason on stderr, and prints English whatever the desktop's language.

## What's in a file

```bash
omaplay info movie.mkv --json
```

Gives `duration` (seconds), `format`, `size`, every stream (`type` video, audio or subtitle; `codec`; `language`; `title`; `width`, `height` and `frameRate` for video; `channels` for audio; `text` for subtitles, false for bitmap ones Omaplay can't show), `chapters` with `start`, `end` and `title`, and `resume`: the second Omaplay would resume at, or null.

## A frame at a time

```bash
omaplay frame movie.mkv --at 1:02:03.5 -o still.png --json
```

The exact frame at that time, at the video's own resolution, as a PNG. `--at` takes seconds (`90`), `mm:ss` or `hh:mm:ss.mmm`.

## Playing something for the user

```bash
omaplay open movie.mkv          # one file plays with its folder as the playlist
omaplay open a.mkv b.mkv        # several files are the playlist, in that order
omaplay open https://example.com/stream.m3u8
```

This opens a window on the user's desktop; only do it when they asked to watch something.

Anything else — converting, cutting, extracting audio, changing containers — is ffmpeg's job; use `ffmpeg` and `ffprobe` directly. Cutting a clip by hand is Omacut's (`omacut file`).
