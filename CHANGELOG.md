# Changelog

## 1.6.0

- Added drawing mode, toggle with <kbd>P</kbd> ([drawing.c](source/drawing.c)) 
  - After entering you can draw on the current frame
  - Tools: Pen, Eraser, Marker (contrast), Line, Rectangle, Circle, Filled Rectangle, Filled Circle; to select any of these either use HMENU or press the key with the index + 1 of it (for example 1 for Pen, 3 for Marker)
  - Marker tool draws the contrast color to the pixel underneath (very slow, even with simd+O3)
  - After pressing <kbd>E</kbd> (or <kbd>F3</kbd>) it asks to export the current frame with he drawings
  - You can export *only* the drawing with using shift with the export key (<kbd>Shift+E</kbd>)
  - You can use undo/redo (<kbd>Ctrl+Z</kbd> <kbd>Ctrl+Y</kbd>), different history than the default playback undo/redo
  - Clear canvas with <kbd>Ctrl+N</kbd>
- Added screenshot (<kbd>F2</kbd>), you can export as: png, jpg, bmp and tga
- Replaced `mconsole` with `mwindows` and added `attach_console_if_present` ([stackoverflow question](https://stackoverflow.com/questions/78920322/redirect-standard-output-to-console-in-gui-application))
- Added [stb_image_write.h](thirdparty/stb_image_write.h) for writing the screenshot/draw export image
- Added pallete colors to config.h themes
- Fixed an issue when sometimes it would not focus the window after `tinyfd_openFileDialog`
- Added `distribute` mode, which creates a `dist.zip` file in root for it to distribute (windows only)

## 1.5.2

- Fixed bug with audio not playing on linux
- Fixed not compiling on linux

## 1.5.1

- Fixed a bug with next and prev media not working sometimes (path issues)
- Fixed a bug where the cursor would not hide while playing

## 1.5.0

- Added more encodings to the `subtitle_normalize_to_utf8` function
- Added <kbd>+</kbd> and <kbd>-</kbd> for shifting subtitles by 1 millisecond
- Added <kbd>Shift+Plus</kbd> and <kbd>Shift+Minus</kbd> for shifting subtitles by 100 milliseconds
- Fixed double free in `load_media_file`
- Fixed a bug where pausing a file with no audio stream would not do anything
- Added `Zoom` submenu to `View`
- Added <kbd>Ctrl+Plus</kbd> and <kbd>Ctrl+Minus</kbd> for zooming in and out
- Added <kbd>Ctrl+Alt+Z</kbd> to reset zoom
- Added <kbd>Ctrl+0</kbd> to reset zoom, aspect ratio and resolution

## 1.4.3

- Added flash text for unsupported file type when file is opened
- Fixed the old prev frame implementation and made it work repeatedly for any amount of frames

## 1.4.2

- Updated volume so `200%` sounds closer to 2x of `100%`
- Fixed subtitles not updating while the Windows HMENU is open
- Fixed a bunch of memory leaks

## 1.4.1

- Changed in Windows HMENU to show <kbd>Shift+Alt+A</kbd> and <kbd>Shift+Alt+R</kbd> for resetting aspect ratio and resolution to original/video values
- Added <kbd>Shift+Alt+S</kbd> for aspect ratio stretch to window
- Fixed AV logging errors when checking if a file is supported
- Moved `TextInput` from [main.c](source/main.c) to [text.c](source/text.c)
- Fixed `seek_and_preview_if_paused` to actually do what it says, was broken since [1.2.0](#120), because of the deinterlacing filter

## 1.4.0

- Added <kbd>[</kbd> and <kbd>]</kbd> for jumping to the previous and next bookmark/chapter.
- Added <kbd>Ctrl+R</kbd> to reload the current video file from disk
- Added logging for `vr_load` total time
- Added <kbd>Home</kbd> and <kbd>End</kbd> keys for jumping to the beginning and end of the video.
- Added `Aspect Ratio` submenu to `View` menu with options for aspect ratio override:
  - `Original` (default): use the original aspect ratio of the video
  - `Stretch to Window`: stretch the video to fill the window, ignoring the original aspect ratio
  - `Custom`: open a dialog to enter custom aspect ratio values
  - `1:1`, `4:3`, `16:9`, `21:9`: common aspect ratios
- Added `Custom` to the Resolution submenu
- Added <kbd>Alt+A</kbd> to open the custom aspect ratio dialog
- Added <kbd>Alt+R</kbd> to open the custom resolution dialog
- Added <kbd>Shift+Alt+A</kbd> to reset aspect ratio override to original
- Added <kbd>Shift+Alt+R</kbd> to reset desired resolution to the video resolution
- Fixed a bug where the HW Cache would be cleared on save
- Fixed a bug where loading a new file wouldnt load its saved settings and instead start from start with default settings

## 1.3.0

- Switched add bookmark from <kbd>m</kbd> to <kbd>b</kbd>
- Added mute toggle to <kbd>m</kbd>
- Extended history handling with bookmark changes and volume changes
- Rearanged Windows HMENU
  - Moved volume controls to their own submenu
  - Added menu items for setting volume to specific values (0%, 100%, 200%)
  - Added bookmark submenu
- Added <kbd>Shift+b</kbd> to delete the closest bookmark
- Added <kbd>Ctrl+1</kbd> for volume set to 100% and <kbd>Ctrl+2</kbd> for 200%
- Fixed many bugs with double free and unchecked `realloc`

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
