# vizd-tray

Tiny system-tray launcher for the Windows `vizd.exe` node (Go, static, GUI subsystem).

## What it does

- Runs `vizd.exe -d data` sharing this process's console, so vizd's colored log
  output (Windows `SetConsoleTextAttribute`) keeps working.
- Shows a tray icon with a context menu:
  - **Open window** — bring the node console back.
  - **Close vizd** — stop the node cleanly (Ctrl+C → appbase SIGINT), then exit.
  - **Enable autostart** — toggle a `HKCU\...\Run` entry (checked when enabled).
  - **Quit** — same as Close vizd.
- Closing the console window (X) hides it to the tray instead of stopping vizd —
  see the `SetConsoleCtrlHandler(CTRL_CLOSE_EVENT)` handler in `programs/vizd/main.cpp`.

## Build

```sh
cd contrib/windows/vizd-tray
CGO_ENABLED=0 GOOS=windows GOARCH=amd64 go build -trimpath \
  -ldflags "-s -w -H windowsgui" -o vizd-tray.exe .
```

The release workflow (`windows-release.yml`) builds it with the same flags and
ships `vizd-tray.exe` next to `vizd.exe`.

## Layout

- `main.go` — tray, console management, vizd lifecycle, autostart.
- `icon.ico` — VIZ logo tray icon (multi-resolution: 16-256 px).
