#pragma once
#include <windows.h>

// Creates (but does not show) the main management window. The window is
// registered as a custom top-level window class; all chrome is custom
// drawn. Returns the window handle.
HWND main_window_create(HINSTANCE instance);

// The single main window instance (nullptr before creation).
[[nodiscard]] HWND main_window_handle();

// The registered window class name of the main window (used by the
// single-instance duplicate finder to locate a running instance).
[[nodiscard]] const wchar_t* main_window_class_name();

// Accelerator table for the main window (Ctrl+S, Ctrl+N, ...). Owned by
// the main window code; the message loop calls TranslateAccelerator.
[[nodiscard]] HACCEL main_window_accelerator();

// Shows / restores the main window and refreshes the volume list.
void main_window_show();

// Reloads the shared-volume list from the registry (used after external
// changes such as indexing completing).
void main_window_refresh_volumes();
