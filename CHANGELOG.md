# Changelog

## 1.2.3

- Fixed a bug where per-file saved volume wouldn't load and always defaulted to 100%.
- Fixed subtitle timing bug where subtitles appeared ~250 ms earlier than they should due to using `vr_get_time` instead of `vr_get_video_time`.

## 1.2.2

- Fixed an issue where no hardware decoder was selected when HEVC profile 2 (10-bit) was detected and the user chose `auto` or `accel`, even if other hardware options were available. Now only vaapi is skipped for HEVC profile 2 while other hardware options are still tried.
- Changed behavior of `auto` and `accel`:
  - `auto` now selects from all options.
  - `accel` only selects from hardware options; if none are available, a warning is logged and decoding falls back to software.
- Updated help message spacing and documentation for the new `hw` options.

## 1.2.1

- Fixed vaapi on Unix
- Fixed path issues in [libavfilter/](thirdparty/libavfilter)
- Updated links in [README.md](README.md) for `SDL2_ttf` and `Nob`

## 1.2.0

- Added bookmarks
- Fixed scaling issue (moire patterns) by switching to LANCZOS from BILINEAR
- Added deinterlacing

## 1.1.0

- Added save data layout to recover from an old version save file
- Fixed bugs:
  - `+5` returning to the same timestamp instead of moving 5 seconds forward
  - the subtitle bug: after changing subtitles it plays until the end of the current packet then frame freezes and audio plays
  - `vr_get_time` returning the wrong time
  - more little bugs like with `AVERROR(EAGAIN)`

## 1.0.0

- Initial release.
- Added basic playback for MKV and MP4 media files.
- Support for ASS and SRT subtitles.
- Added per-file save data for settings.
- Hardware and software decoding.
- Compile-time themes.
- Recent files list.
