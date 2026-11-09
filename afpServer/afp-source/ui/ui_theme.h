#pragma once
// ui_theme - central Fluent-style palette, fonts and window theming helpers.
//
// All colors are plain COLORREF values chosen to match the Windows 11 light
// and dark "Mica" look. The accent color is read from the system so the UI
// follows the user's personalization settings. Fonts are created as GDI+
// fonts (for the custom-drawn chrome) plus one GDI font for the standard
// edit controls. Everything is recreated when the system theme or the
// window DPI changes.

#include <windows.h>

#pragma warning(push)
#pragma warning(disable: 4464) // relative include paths inside the GDI+ headers
#include <objidl.h>
#include <gdiplus.h>
#pragma warning(pop)

// Color roles used throughout the UI.
struct ui_palette
{
    COLORREF window_bg;          // main window background
    COLORREF card_bg;            // card / content layer
    COLORREF card_stroke;        // card outline
    COLORREF card_stroke_strong; // outlines around interactive elements
    COLORREF divider;

    COLORREF text_primary;
    COLORREF text_secondary;
    COLORREF text_tertiary;      // column headers etc.
    COLORREF text_disabled;

    COLORREF accent;             // system accent color
    COLORREF accent_hover;
    COLORREF accent_pressed;
    COLORREF accent_text;        // readable text on accent fills
    COLORREF accent_disabled_bg;
    COLORREF accent_disabled_text;

    COLORREF control_bg;         // rest background of standard buttons
    COLORREF control_hover;
    COLORREF control_pressed;
    COLORREF control_stroke;
    COLORREF control_disabled_bg;
    COLORREF control_disabled_text;

    COLORREF destructive_text;   // subtle destructive action
    COLORREF destructive_hover_bg;
    COLORREF destructive_pressed_bg;

    COLORREF list_hover;
    COLORREF list_selected;
    COLORREF list_selected_hover;

    COLORREF edit_bg;
    COLORREF edit_stroke;
    COLORREF edit_stroke_hover;

    COLORREF toggle_off_stroke;
    COLORREF toggle_off_fill;
    COLORREF toggle_off_fill_hover;
    COLORREF toggle_off_knob;
    COLORREF toggle_off_knob_hover;

    COLORREF pill_ok_bg;         // "Running" status pill
    COLORREF pill_ok_text;

    COLORREF warn_text;          // "Indexing..." etc.
    COLORREF focus_outline;      // keyboard focus visual
    COLORREF toast_bg;
    COLORREF toast_stroke;
};

class ui_theme final
{
public:
    static ui_theme& instance();

    // Reads the current system settings (light/dark, accent color).
    void initialize();

    // Re-reads system settings; returns true when anything changed so the
    // caller can invalidate its windows.
    bool refresh_if_changed();

    // Releases the GDI+ font objects. Must be called while GDI+ is still
    // started (WinMain does this just before GdiplusShutdown); the destructor
    // runs during static teardown, after GDI+ is gone, and then only skips
    // the work because shutdown() already did it.
    void shutdown();

    // Sets the DPI used to build the font set. Returns true when the fonts
    // had to be recreated.
    bool set_dpi(const UINT dpi);

    [[nodiscard]] bool is_dark() const { return dark_; }
    [[nodiscard]] UINT dpi() const { return dpi_; }
    [[nodiscard]] const ui_palette& colors() const { return palette_; }
    [[nodiscard]] COLORREF accent() const { return accent_; }

    // GDI+ fonts (pixel sized, already DPI scaled).
    Gdiplus::Font& font_title();      // app title in the header
    Gdiplus::Font& font_section();    // card section headers
    Gdiplus::Font& font_body();       // regular text
    Gdiplus::Font& font_body_bold();  // emphasis (volume names...)
    Gdiplus::Font& font_caption();    // descriptions / secondary text
    Gdiplus::Font& font_button();     // button labels
    Gdiplus::Font& font_glyph();      // Segoe Fluent Icons, body size
    Gdiplus::Font& font_glyph_small(); // Segoe Fluent Icons, caption size
    Gdiplus::Font& font_glyph_large(); // Segoe Fluent Icons, empty-state size

    // GDI font for standard child controls (edit boxes).
    [[nodiscard]] HFONT gdi_font_body() const { return hfont_body_; }

    // Applies dark/light mode to the non-client area (title bar) of a
    // top-level window. Safe to call repeatedly.
    static void apply_titlebar_theme(const HWND hwnd);

private:
    ui_theme();
    ~ui_theme();
    ui_theme(const ui_theme&) = delete;
    ui_theme& operator=(const ui_theme&) = delete;

    void build_palette();
    void rebuild_fonts();
    void destroy_fonts();
    COLORREF query_accent_color();

    bool dark_ = false;
    UINT dpi_ = 96;
    COLORREF accent_ = 0;
    ui_palette palette_{};

    struct font_set;
    font_set* fonts_ = nullptr;
    HFONT hfont_body_ = nullptr;
};

// Convenience: scale a 96-DPI design value for the current UI DPI.
inline int ui_scale(const int value)
{
    return MulDiv(value, ui_theme::instance().dpi(), 96);
}
