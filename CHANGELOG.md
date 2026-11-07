# Changelog

## 1.1.1 - 2026-11-08

### Changed
- Settings has a dark look: a black title bar on Windows 11, a black background with white text, and dark rounded buttons.
- The slider is a plain line with no tick marks, and its thumb is symmetric.
- No change in size or memory: the exe is still 17.9 KB. The dark-mode libraries load only while Settings is open, so memory in the tray is unchanged.

## 1.1.0 - 2026-11-08

Rewritten in plain C for a much smaller footprint, with the same look and settings.

### Changed
- Rewritten from C# WinForms to plain C on the Win32 API, with no .NET or C runtime. Memory use drops from 22.8 MB to 2.2 MB (private bytes) and the working set from 36 MB to 2.4 MB.
- No more polling. Version 1.0 put the corners back on top four times a second. Now they move back only when the foreground window changes or the taskbar shows, moves or is reordered, so idle CPU use is 0 ms per minute (was 281 ms).
- Antialiasing uses 256 samples per pixel. The curve is exactly symmetric, where GDI+ was off by up to 32 alpha levels between the two halves.
- The settings window is destroyed when hidden, so nothing but the corners and the tray icon stays in memory.

### Added
- An app icon, used in the tray, the settings window and Explorer.
- Per-monitor DPI awareness: the settings window scales on each monitor, and the corners follow scaling changes.
- Opening the program a second time brings up the Settings window of the copy that's already running, instead of starting a second copy.
- The tray icon comes back if Explorer restarts.
- Version information in the exe's Properties.

### Kept
- Radius 0 to 120 px (default 24), saved in `%APPDATA%\RoundedCorners.cfg` in the same format, so your setting carries over.

## 1.0.0 - 2026-11-08

First release: a C# WinForms tray app that draws antialiased black corners on every monitor.
