# Rounded Corners

A tiny Windows app that gives your screen rounded corners. It draws a small black, antialiased corner in each corner of every monitor, which looks great on OLED panels and hides the sharp rectangle of the display.

- **Click-through.** The corners never take focus or block the mouse.
- **Always on top.** They stay above the taskbar, including an auto-hiding one.
- **Every monitor.** It rebuilds the corners when you plug in a display or change resolution.
- **Adjustable radius.** Choose 0 to 120 px with a slider (default 24 px). Your choice is saved in `%APPDATA%\RoundedCorners.cfg`.
- **Lives in the tray.** Closing the window hides it; right-click the tray icon to open Settings or exit.
- **Tiny.** One C# file, about 12 KB compiled, no installer and no dependencies beyond the .NET Framework that ships with Windows.

## Download

Get `RoundedCorners.exe` from the [latest release](../../releases/latest) and run it. To start it with Windows, put a shortcut to it in `shell:startup`.

## Build it yourself

Windows already includes the C# compiler from .NET Framework 4:

```bat
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe -nologo -target:winexe -optimize+ -out:RoundedCorners.exe RoundedCorners.cs
```

## How it works

Each corner is a separate layered window (`WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`) filled with a per-pixel-alpha bitmap through `UpdateLayeredWindow`. The app renders one top-left corner as an antialiased path and mirrors it for the other three, so all four corners are pixel-identical. A 250 ms timer puts the corners back on top whenever another topmost window, such as the taskbar, covers them.

## License

[MIT](LICENSE)
