#include "stdafx.h"
#include "ui_theme.h"

#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

// DWMWA_USE_IMMERSIVE_DARK_MODE is 20 on Windows 10 2004+ and 19 before that.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1
#define DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 19
#endif

namespace
{
    constexpr COLORREF mix(const COLORREF a, const COLORREF b, const double t)
    {
        const int r = static_cast<int>(GetRValue(a) * (1.0 - t) + GetRValue(b) * t);
        const int g = static_cast<int>(GetGValue(a) * (1.0 - t) + GetGValue(b) * t);
        const int bl = static_cast<int>(GetBValue(a) * (1.0 - t) + GetBValue(b) * t);
        return RGB(r, g, bl);
    }

    double luminance(const COLORREF c)
    {
        return 0.299 * GetRValue(c) + 0.587 * GetGValue(c) + 0.114 * GetBValue(c);
    }

    // True when the color carries no real hue (a gray), meaning the system
    // did not give us a usable accent color.
    bool is_grayish(const COLORREF c)
    {
        const int r = GetRValue(c);
        const int g = GetGValue(c);
        const int b = GetBValue(c);
        const int max_c = max(max(r, g), b);
        const int min_c = min(min(r, g), b);
        return (max_c - min_c) < 24;
    }

    bool read_dark_setting()
    {
        bool dark = false;
        HKEY h_key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                          L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                          0, KEY_READ, &h_key) == ERROR_SUCCESS)
        {
            DWORD value = 1;
            DWORD cb = sizeof(value);
            DWORD type = 0;
            if (RegQueryValueExW(h_key, L"AppsUseLightTheme", nullptr, &type,
                                 reinterpret_cast<LPBYTE>(&value), &cb) == ERROR_SUCCESS &&
                type == REG_DWORD)
            {
                dark = value == 0;
            }
            RegCloseKey(h_key);
        }
        return dark;
    }
}

// Font set. Fonts are created as GDI HFONTs (so weights like Segoe UI
// Variable *Semibold*, which GDI+ cannot express, work) and wrapped with
// Gdiplus::Font for the custom-drawn chrome. Families fall back when the
// Segoe UI Variable / Segoe Fluent Icons faces are missing.
struct ui_theme::font_set
{
    struct entry
    {
        HFONT hfont = nullptr;
        Gdiplus::Font* font = nullptr;
    };

    entry title;
    entry section;
    entry body;
    entry body_bold;
    entry caption;
    entry button;
    entry glyph;
    entry glyph_small;
    entry glyph_large;

    ~font_set()
    {
        release_all();
    }

    void release_all()
    {
        for (entry* e : { &title, &section, &body, &body_bold, &caption, &button,
                          &glyph, &glyph_small, &glyph_large })
        {
            delete e->font;
            e->font = nullptr;
            if (e->hfont != nullptr)
                DeleteObject(e->hfont);
            e->hfont = nullptr;
        }
    }

    static bool face_exists(HDC dc, const WCHAR* name)
    {
        if (HFONT probe = CreateFontW(10, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      DEFAULT_QUALITY, FIXED_PITCH, name))
        {
            const HFONT old = static_cast<HFONT>(SelectObject(dc, probe));
            WCHAR actual[LF_FACESIZE]{};
            GetTextFaceW(dc, LF_FACESIZE, actual);
            SelectObject(dc, old);
            DeleteObject(probe);
            return lstrcmpiW(actual, name) == 0;
        }
        return false;
    }

    static entry create(HDC dc, std::initializer_list<const WCHAR*> faces,
                        const int weight, const int design_px, const UINT dpi)
    {
        const WCHAR* face = L"Segoe UI";
        for (const auto* f : faces)
        {
            if (face_exists(dc, f))
            {
                face = f;
                break;
            }
        }

        LOGFONTW lf{};
        lf.lfHeight = -MulDiv(design_px, static_cast<int>(dpi), 96);
        lf.lfWeight = weight;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfOutPrecision = OUT_TT_PRECIS;
        lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
        lf.lfQuality = CLEARTYPE_NATURAL_QUALITY;
        lf.lfPitchAndFamily = VARIABLE_PITCH | FF_DONTCARE;
        wcscpy_s(lf.lfFaceName, face);

        entry e;
        e.hfont = CreateFontIndirectW(&lf);
        if (e.hfont != nullptr)
            e.font = new Gdiplus::Font(dc, e.hfont);
        return e;
    }

    void build(const UINT dpi)
    {
        release_all();

        HDC dc = GetDC(nullptr);

        title = create(dc, { L"Segoe UI Variable Display", L"Segoe UI" }, FW_SEMIBOLD, 16, dpi);
        section = create(dc, { L"Segoe UI Variable Display", L"Segoe UI" }, FW_SEMIBOLD, 14, dpi);
        body = create(dc, { L"Segoe UI Variable Text", L"Segoe UI" }, FW_NORMAL, 13, dpi);
        body_bold = create(dc, { L"Segoe UI Variable Text", L"Segoe UI" }, FW_SEMIBOLD, 13, dpi);
        caption = create(dc, { L"Segoe UI Variable Text", L"Segoe UI" }, FW_NORMAL, 12, dpi);
        button = create(dc, { L"Segoe UI Variable Text", L"Segoe UI" }, FW_SEMIBOLD, 13, dpi);
        glyph = create(dc, { L"Segoe Fluent Icons", L"Segoe MDL2 Assets" }, FW_NORMAL, 13, dpi);
        glyph_small = create(dc, { L"Segoe Fluent Icons", L"Segoe MDL2 Assets" }, FW_NORMAL, 12, dpi);
        glyph_large = create(dc, { L"Segoe Fluent Icons", L"Segoe MDL2 Assets" }, FW_NORMAL, 28, dpi);

        ReleaseDC(nullptr, dc);
    }
};

ui_theme& ui_theme::instance()
{
    static ui_theme the_instance;
    return the_instance;
}

ui_theme::ui_theme() = default;

ui_theme::~ui_theme()
{
    destroy_fonts();
}

void ui_theme::initialize()
{
    dark_ = read_dark_setting();
    accent_ = query_accent_color();
    build_palette();
    rebuild_fonts();
}

bool ui_theme::refresh_if_changed()
{
    const bool new_dark = read_dark_setting();
    const COLORREF new_accent = query_accent_color();

    if (new_dark == dark_ && new_accent == accent_)
        return false;

    dark_ = new_dark;
    accent_ = new_accent;
    build_palette();
    return true;
}

bool ui_theme::set_dpi(const UINT dpi)
{
    if (dpi == dpi_ && fonts_ != nullptr)
        return false;

    dpi_ = dpi;
    rebuild_fonts();
    return true;
}

COLORREF ui_theme::query_accent_color()
{
    COLORREF color = 0;

    if (FAILED(DwmGetColorizationColor(&color, nullptr)))
        color = 0;

    color &= 0x00FFFFFF;

    if (is_grayish(color))
    {
        // DwmGetColorizationColor can return a gray when the desktop is set
        // to solid colors; read the accent straight from the registry.
        HKEY h_key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM",
                          0, KEY_READ, &h_key) == ERROR_SUCCESS)
        {
            DWORD value = 0;
            DWORD cb = sizeof(value);
            DWORD type = 0;
            if (RegQueryValueExW(h_key, L"ColorizationColor", nullptr, &type,
                                 reinterpret_cast<LPBYTE>(&value), &cb) == ERROR_SUCCESS &&
                type == REG_DWORD)
            {
                const COLORREF reg_color = RGB((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
                if (!is_grayish(reg_color))
                    color = reg_color;
            }
            RegCloseKey(h_key);
        }
    }

    if (is_grayish(color))
    {
        // Fall back to the stock Windows 11 accent blue.
        color = dark_ ? RGB(0x60, 0xCD, 0xFF) : RGB(0x00, 0x5F, 0xB8);
    }

    return color;
}

void ui_theme::build_palette()
{
    ui_palette& p = palette_;

    const COLORREF accent = accent_;
    const bool dark = dark_;

    p.accent = accent;
    p.accent_text = luminance(accent) > 155.0 ? RGB(0, 0, 0) : RGB(255, 255, 255);

    if (dark)
    {
        p.window_bg = RGB(0x20, 0x20, 0x20);
        p.card_bg = RGB(0x2C, 0x2C, 0x2C);
        p.card_stroke = RGB(0x3A, 0x3A, 0x3A);
        p.card_stroke_strong = RGB(0x45, 0x45, 0x45);
        p.divider = RGB(0x3D, 0x3D, 0x3D);

        p.text_primary = RGB(0xFB, 0xFB, 0xFB);
        p.text_secondary = RGB(0xC5, 0xC5, 0xC5);
        p.text_tertiary = RGB(0x9A, 0x9A, 0x9A);
        p.text_disabled = RGB(0x70, 0x70, 0x70);

        p.accent_hover = mix(accent, RGB(0xFF, 0xFF, 0xFF), 0.22);
        p.accent_pressed = mix(accent, RGB(0x00, 0x00, 0x00), 0.25);
        p.accent_disabled_bg = mix(p.card_bg, accent, 0.22);
        p.accent_disabled_text = mix(p.text_disabled, p.text_primary, 0.15);

        p.control_bg = RGB(0x37, 0x37, 0x37);
        p.control_hover = RGB(0x40, 0x40, 0x40);
        p.control_pressed = RGB(0x2F, 0x2F, 0x2F);
        p.control_stroke = RGB(0x4A, 0x4A, 0x4A);
        p.control_disabled_bg = RGB(0x2D, 0x2D, 0x2D);
        p.control_disabled_text = RGB(0x70, 0x70, 0x70);

        p.destructive_text = RGB(0xFF, 0x99, 0xA4);
        p.destructive_hover_bg = RGB(0x4A, 0x2A, 0x28);
        p.destructive_pressed_bg = RGB(0x3D, 0x24, 0x22);

        p.list_hover = RGB(0x35, 0x35, 0x35);
        p.list_selected = RGB(0x3D, 0x3D, 0x3D);
        p.list_selected_hover = RGB(0x45, 0x45, 0x45);

        p.edit_bg = RGB(0x37, 0x37, 0x37);
        p.edit_stroke = RGB(0x8A, 0x8A, 0x8A);
        p.edit_stroke_hover = RGB(0xB0, 0xB0, 0xB0);

        p.toggle_off_stroke = RGB(0x9E, 0x9E, 0x9E);
        p.toggle_off_fill = RGB(0x1F, 0x1F, 0x1F);
        p.toggle_off_fill_hover = RGB(0x2B, 0x2B, 0x2B);
        p.toggle_off_knob = RGB(0xCF, 0xCF, 0xCF);
        p.toggle_off_knob_hover = RGB(0xFF, 0xFF, 0xFF);

        p.pill_ok_bg = RGB(0x12, 0x3A, 0x1D);
        p.pill_ok_text = RGB(0x6C, 0xCF, 0x84);

        p.warn_text = RGB(0xF9, 0xA8, 0x25);
        p.focus_outline = RGB(0xFB, 0xFB, 0xFB);
        p.toast_bg = RGB(0x2D, 0x2D, 0x2D);
        p.toast_stroke = RGB(0x4A, 0x4A, 0x4A);
    }
    else
    {
        p.window_bg = RGB(0xF3, 0xF3, 0xF3);
        p.card_bg = RGB(0xFF, 0xFF, 0xFF);
        p.card_stroke = RGB(0xE5, 0xE5, 0xE5);
        p.card_stroke_strong = RGB(0xD6, 0xD6, 0xD6);
        p.divider = RGB(0xE5, 0xE5, 0xE5);

        p.text_primary = RGB(0x1A, 0x1A, 0x1A);
        p.text_secondary = RGB(0x5F, 0x5F, 0x5F);
        p.text_tertiary = RGB(0x7A, 0x7A, 0x7A);
        p.text_disabled = RGB(0xA0, 0xA0, 0xA0);

        p.accent_hover = mix(accent, RGB(0xFF, 0xFF, 0xFF), 0.12);
        p.accent_pressed = mix(accent, RGB(0x00, 0x00, 0x00), 0.14);
        p.accent_disabled_bg = mix(RGB(0xE8, 0xE8, 0xE8), accent, 0.30);
        p.accent_disabled_text = RGB(0xB0, 0xB0, 0xB0);

        p.control_bg = RGB(0xFB, 0xFB, 0xFB);
        p.control_hover = RGB(0xF5, 0xF5, 0xF5);
        p.control_pressed = RGB(0xEA, 0xEA, 0xEA);
        p.control_stroke = RGB(0xD1, 0xD1, 0xD1);
        p.control_disabled_bg = RGB(0xF0, 0xF0, 0xF0);
        p.control_disabled_text = RGB(0xA0, 0xA0, 0xA0);

        p.destructive_text = RGB(0xC4, 0x2B, 0x1C);
        p.destructive_hover_bg = RGB(0xFD, 0xEB, 0xEB);
        p.destructive_pressed_bg = RGB(0xF9, 0xDD, 0xDC);

        p.list_hover = RGB(0xF5, 0xF5, 0xF5);
        p.list_selected = RGB(0xE9, 0xE9, 0xE9);
        p.list_selected_hover = RGB(0xE0, 0xE0, 0xE0);

        p.edit_bg = RGB(0xFB, 0xFB, 0xFB);
        p.edit_stroke = RGB(0x8F, 0x8F, 0x8F);
        p.edit_stroke_hover = RGB(0x5A, 0x5A, 0x5A);

        p.toggle_off_stroke = RGB(0x76, 0x76, 0x76);
        p.toggle_off_fill = RGB(0xF7, 0xF7, 0xF7);
        p.toggle_off_fill_hover = RGB(0xEC, 0xEC, 0xEC);
        p.toggle_off_knob = RGB(0x5D, 0x5D, 0x5D);
        p.toggle_off_knob_hover = RGB(0x3D, 0x3D, 0x3D);

        p.pill_ok_bg = RGB(0xDD, 0xF4, 0xDB);
        p.pill_ok_text = RGB(0x0F, 0x7B, 0x0F);

        p.warn_text = RGB(0x9D, 0x5D, 0x00);
        p.focus_outline = RGB(0x1A, 0x1A, 0x1A);
        p.toast_bg = RGB(0xFB, 0xFB, 0xFB);
        p.toast_stroke = RGB(0xD6, 0xD6, 0xD6);
    }
}

void ui_theme::destroy_fonts()
{
    delete fonts_;
    fonts_ = nullptr;

    if (hfont_body_ != nullptr)
    {
        DeleteObject(hfont_body_);
        hfont_body_ = nullptr;
    }
}

void ui_theme::rebuild_fonts()
{
    destroy_fonts();

    fonts_ = new font_set();
    fonts_->build(dpi_);

    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(13, dpi_, 96);
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = CLEARTYPE_NATURAL_QUALITY;
    lf.lfPitchAndFamily = VARIABLE_PITCH | FF_DONTCARE;
    wcscpy_s(lf.lfFaceName, L"Segoe UI Variable Text");
    hfont_body_ = CreateFontIndirectW(&lf);

    if (hfont_body_ == nullptr)
    {
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        hfont_body_ = CreateFontIndirectW(&lf);
    }
}

Gdiplus::Font& ui_theme::font_title() { return *fonts_->title.font; }
Gdiplus::Font& ui_theme::font_section() { return *fonts_->section.font; }
Gdiplus::Font& ui_theme::font_body() { return *fonts_->body.font; }
Gdiplus::Font& ui_theme::font_body_bold() { return *fonts_->body_bold.font; }
Gdiplus::Font& ui_theme::font_caption() { return *fonts_->caption.font; }
Gdiplus::Font& ui_theme::font_button() { return *fonts_->button.font; }
Gdiplus::Font& ui_theme::font_glyph() { return *fonts_->glyph.font; }
Gdiplus::Font& ui_theme::font_glyph_small() { return *fonts_->glyph_small.font; }
Gdiplus::Font& ui_theme::font_glyph_large() { return *fonts_->glyph_large.font; }

void ui_theme::apply_titlebar_theme(const HWND hwnd)
{
    if (hwnd == nullptr)
        return;

    const BOOL use_dark = ui_theme::instance().is_dark() ? TRUE : FALSE;

    if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &use_dark, sizeof(use_dark))))
    {
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1, &use_dark, sizeof(use_dark));
    }
}
