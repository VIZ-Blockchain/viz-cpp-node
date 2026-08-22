//go:build windows

// vizd-tray is a tiny system-tray launcher for the Windows vizd node. It runs
// vizd.exe sharing this process's console (so vizd's colored log output keeps
// working), shows a tray icon, and:
//   - hides the console window to the tray when it is closed (X) instead of
//     stopping the node;
//   - "Open window" brings the console back;
//   - "Close vizd" sends Ctrl+C so vizd shuts down cleanly (appbase SIGINT),
//     then the launcher exits;
//   - "Enable autostart" (checkbox) writes/removes a HKCU Run entry.
package main

import (
	_ "embed"
	"fmt"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"
	"unsafe"

	"fyne.io/systray"
	"golang.org/x/sys/windows/registry"
)

//go:embed icon.ico
var trayIcon []byte

var (
	kernel32 = syscall.NewLazyDLL("kernel32.dll")
	user32   = syscall.NewLazyDLL("user32.dll")

	procAllocConsole             = kernel32.NewProc("AllocConsole")
	procGetConsoleWindow         = kernel32.NewProc("GetConsoleWindow")
	procSetConsoleCtrlHandler    = kernel32.NewProc("SetConsoleCtrlHandler")
	procGenerateConsoleCtrlEvent = kernel32.NewProc("GenerateConsoleCtrlEvent")

	procShowWindow         = user32.NewProc("ShowWindow")
	procSetForegroundWindow = user32.NewProc("SetForegroundWindow")
	procMessageBox         = user32.NewProc("MessageBoxW")
)

const (
	ctrlCEvent     = 0
	ctrlBreakEvent = 1
	ctrlCloseEvent = 2

	swHide    = 0
	swRestore = 9

	runKeyPath = `Software\Microsoft\Windows\CurrentVersion\Run`
	runValue   = `vizd-tray`

	stopGrace = 10 * time.Second
)

// --- console window management -------------------------------------------

func allocConsole()    { procAllocConsole.Call() }
func consoleWindow() uintptr {
	hwnd, _, _ := procGetConsoleWindow.Call()
	return hwnd
}
func showConsole() {
	if hwnd := consoleWindow(); hwnd != 0 {
		procShowWindow.Call(hwnd, swRestore)
		procSetForegroundWindow.Call(hwnd)
	}
}
func hideConsole() {
	if hwnd := consoleWindow(); hwnd != 0 {
		procShowWindow.Call(hwnd, swHide)
	}
}

// consoleCtrlHandler is installed via SetConsoleCtrlHandler. The launcher owns
// the console window: closing it (X) hides it to the tray instead of exiting,
// and the launcher survives the Ctrl+C it sends to stop vizd (vizd receives its
// own copy and shuts down cleanly).
func consoleCtrlHandler(ctrlType uintptr) uintptr {
	switch ctrlType {
	case ctrlCloseEvent:
		hideConsole()
		return 1 // handled
	case ctrlCEvent:
		return 1 // swallow so the launcher survives
	}
	return 0
}

func installCtrlHandler() {
	cb := syscall.NewCallback(consoleCtrlHandler)
	procSetConsoleCtrlHandler.Call(cb, 1)
}

// --- vizd lifecycle -------------------------------------------------------

func bundleDir() (string, error) {
	self, err := os.Executable()
	if err != nil {
		return "", err
	}
	return filepath.Dir(self), nil
}

func spawnVizd() (*exec.Cmd, error) {
	dir, err := bundleDir()
	if err != nil {
		return nil, err
	}
	vizdExe := filepath.Join(dir, "vizd.exe")
	if _, err := os.Stat(vizdExe); err != nil {
		return nil, fmt.Errorf("vizd.exe not found next to the launcher: %v", err)
	}
	cmd := exec.Command(vizdExe, "-d", "data")
	cmd.Dir = dir
	// No CREATE_NEW_CONSOLE / CREATE_NO_WINDOW: vizd inherits this process's
	// console, so its SetConsoleTextAttribute coloring works and Ctrl+C can be
	// delivered to the shared console group.
	if err := cmd.Start(); err != nil {
		return nil, err
	}
	return cmd, nil
}

func stopVizd(cmd *exec.Cmd) {
	if cmd == nil || cmd.Process == nil {
		return
	}
	// Ctrl+C to the whole shared console group (launcher + vizd).
	procGenerateConsoleCtrlEvent.Call(ctrlCEvent, 0)

	done := make(chan struct{})
	go func() {
		_, _ = cmd.Process.Wait()
		close(done)
	}()
	select {
	case <-done:
	case <-time.After(stopGrace):
		_ = cmd.Process.Kill()
	}
}

// --- autostart (HKCU Run) -------------------------------------------------

func autostartEnabled() bool {
	k, err := registry.OpenKey(registry.CURRENT_USER, runKeyPath, registry.QUERY_VALUE)
	if err != nil {
		return false
	}
	defer k.Close()
	_, _, err = k.GetStringValue(runValue)
	return err == nil
}

func setAutostart(enabled bool) error {
	exe, err := os.Executable()
	if err != nil {
		return err
	}
	if enabled {
		k, _, err := registry.CreateKey(registry.CURRENT_USER, runKeyPath, registry.SET_VALUE)
		if err != nil {
			return err
		}
		defer k.Close()
		return k.SetStringValue(runValue, fmt.Sprintf(`"%s"`, exe))
	}
	k, err := registry.OpenKey(registry.CURRENT_USER, runKeyPath, registry.SET_VALUE)
	if err != nil {
		return nil // key absent: nothing to remove
	}
	defer k.Close()
	_ = k.DeleteValue(runValue)
	return nil
}

func errBox(title, text string) {
	t, _ := syscall.UTF16PtrFromString(text)
	c, _ := syscall.UTF16PtrFromString(title)
	procMessageBox.Call(0, uintptr(unsafe.Pointer(t)), uintptr(unsafe.Pointer(c)), 0x10)
}

func toggleAutostart(item *systray.MenuItem) {
	enable := !item.Checked()
	if err := setAutostart(enable); err != nil {
		errBox("vizd-tray", "Could not update autostart: "+err.Error())
		return
	}
	if enable {
		item.Check()
	} else {
		item.Uncheck()
	}
}

// --- entry point ----------------------------------------------------------

func main() {
	// Allocate the console vizd will share, then install our ctrl handler so
	// closing that window (X) hides it to the tray instead of killing vizd.
	allocConsole()
	installCtrlHandler()
	signal.Ignore(os.Interrupt) // defensive: survive the Ctrl+C we send

	vizd, err := spawnVizd()
	if err != nil {
		errBox("vizd-tray", err.Error())
	}

	ready := make(chan struct{})

	// Follow vizd: when it exits (Close vizd, Ctrl+C in the window, crash) the
	// launcher has nothing left to do and exits as well.
	if vizd != nil {
		go func() {
			_ = vizd.Wait()
			<-ready
			systray.Quit()
		}()
	}

	onReady := func() {
		systray.SetIcon(trayIcon)
		systray.SetTitle("VIZ node")
		systray.SetTooltip("VIZ node (vizd)")

		mOpen := systray.AddMenuItem("Open window", "Show the node console")
		mClose := systray.AddMenuItem("Close vizd", "Stop the node")
		mAuto := systray.AddMenuItemCheckbox("Enable autostart", "Start the node at Windows login", autostartEnabled())
		systray.AddSeparator()
		mQuit := systray.AddMenuItem("Quit", "Stop vizd and exit")

		close(ready)

		go func() {
			for {
				select {
				case <-mOpen.ClickedCh:
					showConsole()
				case <-mClose.ClickedCh:
					stopVizd(vizd)
					systray.Quit()
				case <-mAuto.ClickedCh:
					toggleAutostart(mAuto)
				case <-mQuit.ClickedCh:
					stopVizd(vizd)
					systray.Quit()
				}
			}
		}()
	}
	onExit := func() {}

	systray.Run(onReady, onExit)
}
