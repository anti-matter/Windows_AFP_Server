#pragma once
// ui_controls - small set of custom-drawn Fluent-style controls and paint
// helpers used by the main window and the modernized dialogs.

#include "ui_theme.h"

#include <windows.h>

// The SDK defines REAL inside namespace Gdiplus; several helpers below use
// the short name like the GDI+ samples do.
using Gdiplus::REAL;

// Private message custom controls send to their parent to learn the color
// of the surface they sit on (lparam: COLORREF* out). Handled by the owner
// window; default (unhandled) is the palette card color.
#define WM_UI_GET_SURFACE (WM_APP + 420)

// Segoe Fluent Icons glyphs used across the UI.
namespace fluent_glyph
{
    constexpr WCHAR add[] = L"\uE710";        // Plus
    constexpr WCHAR folder[] = L"\uE8B7";     // Folder
    constexpr WCHAR delete_[] = L"\uE74D";    // Trash
    constexpr WCHAR settings[] = L"\uE713";   // Settings slider
    constexpr WCHAR chevron_down[] = L"\uE70D";
    constexpr WCHAR chevron_right[] = L"\uE76C";
    constexpr WCHAR send[] = L"\uE724";       // Send
    constexpr WCHAR power[] = L"\uE7E8";      // Power button
    constexpr WCHAR save[] = L"\uE74E";       // Save
    constexpr WCHAR info[] = L"\uE946";       // Info
    constexpr WCHAR history[] = L"\uE81C";    // History / hourglass (indexing)
    constexpr WCHAR check_mark[] = L"\uE73E"; // Check mark
    constexpr WCHAR contact[] = L"\uE77B";    // Person (connected clients)
    constexpr WCHAR open[] = L"\uE8A7";       // Open folder
}

enum class fluent_button_style
{
    standard,      // neutral fill + border
    accent,        // primary action, system accent fill
    subtle,        // transparent until hovered
    destructive,   // destructive action, transparent with red text
};

// Registers the custom toggle-switch window class. Call once at startup.
void register_toggle_class(HINSTANCE instance);

// Creates a toggle switch. Send/Query state with the functions below; the
// control notifies its parent with WM_COMMAND (BN_CLICKED) when toggled.
HWND create_toggle_switch(HWND parent, HINSTANCE instance, int id, int x, int y, int width, int height);
void toggle_set_checked(HWND hwnd, bool checked);
[[nodiscard]] bool toggle_get_checked(HWND hwnd);

// Turns a standard BUTTON into a Fluent-styled, owner-drawn button.
// The button must be created with BS_OWNERDRAW | WS_TABSTOP.
void make_fluent_button(HWND hwnd, fluent_button_style style);
[[nodiscard]] fluent_button_style fluent_button_get_style(HWND hwnd);

// Tells a Fluent button which background color it is drawn on (for subtle
// buttons whose rest state is transparent).
void fluent_button_set_surface(HWND hwnd, COLORREF surface);

// True while the mouse is over a Fluent button (used by owner-draw code
// that renders the button itself instead of via draw_fluent_button).
[[nodiscard]] bool fluent_button_is_hot(HWND hwnd);

// Paints an owner-drawn button described by dis using its Fluent style.
// Call from the owner window's WM_DRAWITEM handler.
void draw_fluent_button(const DRAWITEMSTRUCT* dis);

namespace ui_draw
{
    // Rounded rectangle with optional fill and 1px stroke.
    void round_rect(Gdiplus::Graphics& graphics, Gdiplus::RectF rect, REAL radius,
                    const Gdiplus::Color& fill, const Gdiplus::Color* stroke);

    // Overload taking the stroke color by value.
    void round_rect(Gdiplus::Graphics& graphics, Gdiplus::RectF rect, REAL radius,
                    const Gdiplus::Color& fill, const Gdiplus::Color& stroke);

    // Draws text inside rect with the given horizontal and vertical alignment.
    void text(Gdiplus::Graphics& graphics, Gdiplus::Font& font, const std::wstring& text,
              Gdiplus::RectF rect, UINT h_align, UINT v_align, COLORREF color);

    // Draws text with a trailing ellipsis when it does not fit.
    void text_ellipsis(Gdiplus::Graphics& graphics, Gdiplus::Font& font, const std::wstring& text,
                       Gdiplus::RectF rect, UINT h_align, UINT v_align, COLORREF color);

    // Measures the pixel extent of a string.
    Gdiplus::RectF measure(Gdiplus::Graphics& graphics, Gdiplus::Font& font, const std::wstring& text);

    // Word-wrapped text (no ellipsis); use for descriptive captions.
    void text_wrap(Gdiplus::Graphics& graphics, Gdiplus::Font& font, const std::wstring& text,
                   Gdiplus::RectF rect, UINT h_align, COLORREF color);

    // Horizontal separator line.
    void h_line(Gdiplus::Graphics& graphics, int x, int width, int y, COLORREF color);

    // Keyboard focus visual: rounded outline with a gap-colored ring so it
    // reads correctly on any surface.
    void focus_ring(Gdiplus::Graphics& graphics, Gdiplus::RectF rect, REAL radius, COLORREF gap_color);
}
