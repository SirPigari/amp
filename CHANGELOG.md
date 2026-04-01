# Changelog

## 1.2.0

- Added bookmarks
- Fixed scaling issue (moire patterns) by switching to LANCZOS from BILINEAR
- Added deinterlacing

## 1.1.0

- Added save data layout to recover from an old version save file
- Fixed bugs:
  - +5 returning to the same timestamp instead of moving 5 seconds forward
  - the subtitle bug: after changing subtitles it plays until the end of the current packet then frame freezes and audio plays
  - `vr_get_time` returning the wrong time
  - more little bugs like with `AVERROR(EAGAIN)`

## 1.0.0

- Initial version
