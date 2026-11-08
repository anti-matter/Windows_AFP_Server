#include "stdafx.h"
#include "ui_controls.h"

#include <commctrl.h>
#include <windowsx.h>
#include <strsafe.h>
#include <iterator>

namespace
{
    constexpr auto toggle_class_name = L"PersonalAfpToggleSwitch";
    constexpr auto btn_style_prop = L"AfpFluentBtnStyle";
    constexpr auto btn_hot_prop = L"AfpFluentBtnHot";
    constexpr auto btn_surface_prop = L"AfpFluentBtnSurface";

    // Toggle state bits, kept in GWLP_USERDATA (our own window class).
    constexpr ULONG_PTR toggle_checked = 0x1;
    constexpr ULONG_PTR toggle_hot = 0x2;
    constexpr ULONG_PTR toggle_focus = 0x4;

    Gdiplus::Color gdi_color(const COLORREF c)
    {
        return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
    }

    void add_rounded_path(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& r, const REAL radius)
    {
        const REAL d = radius * 2.0f;
        path.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
        path.AddArc(r.GetRight() - d, r.Y, d, d, 270.0f, 90.0f);
        path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0.0f, 90.0f);
        path.AddArc(r.X, r.GetBottom() - d, d, d, 90.0f, 90.0f);
        path.CloseFigure();
    }
    void paint_toggle(const HWND hwnd, HDC dc)
    {
        auto& theme = ui_theme::instance();
        const auto& p = theme.colors();

        RECT rc{};
        GetClientRect(hwnd, &rc);
        const auto w = static_cast<REAL>(rc.right);
        const auto h = static_cast<REAL>(rc.bottom);
        if (w <= 0 || h <= 0)
            return;

        const ULONG_PTR state = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        const bool checked = (state & toggle_checked) != 0;
        const bool hot = (state & toggle_hot) != 0;
        const bool has_focus = (state & toggle_focus) != 0;
        const bool enabled = IsWindowEnabled(hwnd) != FALSE;

        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

        // Paint behind the control with the parent surface color so the
        // semi-transparent parts of the control blend with its card.
        COLORREF surface = p.card_bg;
        SendMessageW(GetParent(hwnd), WM_UI_GET_SURFACE, 0, reinterpret_cast<LPARAM>(&surface));
        {
            Gdiplus::SolidBrush bg(gdi_color(surface));
            graphics.FillRectangle(&bg, 0.0f, 0.0f, w, h);
        }

        const Gdiplus::RectF pill(1.0f, h * 0.10f, w - 2.0f, h * 0.80f);
        const REAL radius = pill.Height * 0.5f;

        Gdiplus::GraphicsPath path;
        add_rounded_path(path, pill, radius);

        if (checked)
        {
            COLORREF fill = p.accent;
            if (!enabled)
                fill = p.accent_disabled_bg;
            else if (hot)
                fill = p.accent_hover;

            const COLORREF stroke = enabled ? fill : p.text_disabled;
            const COLORREF knob_color = enabled ? p.accent_text : RGB(0xF0, 0xF0, 0xF0);

            Gdiplus::SolidBrush fill_brush(gdi_color(fill));
            graphics.FillPath(&fill_brush, &path);
            const Gdiplus::Pen pen(gdi_color(stroke), 1.0f);
            graphics.DrawPath(&pen, &path);

            const REAL knob_d = pill.Height - 2.0f * ui_scale(2);
            const REAL knob_x = pill.GetRight() - knob_d - ui_scale(2);
            Gdiplus::SolidBrush knob(gdi_color(knob_color));
            graphics.FillEllipse(&knob, knob_x, pill.Y + 1.0f, knob_d, knob_d);
        }
        else
        {
            COLORREF fill = p.toggle_off_fill;
            COLORREF stroke = p.toggle_off_stroke;
            COLORREF knob_color = p.toggle_off_knob;

            if (!enabled)
            {
                fill = p.control_disabled_bg;
                stroke = p.text_disabled;
                knob_color = p.text_disabled;
            }
            else if (hot)
            {
                fill = p.toggle_off_fill_hover;
                knob_color = p.toggle_off_knob_hover;
            }

            Gdiplus::SolidBrush fill_brush(gdi_color(fill));
            graphics.FillPath(&fill_brush, &path);
            const Gdiplus::Pen pen(gdi_color(stroke), 1.0f);
            graphics.DrawPath(&pen, &path);

            const REAL knob_d = pill.Height - 2.0f * ui_scale(5);
            const REAL knob_x = pill.X + ui_scale(4);
            Gdiplus::SolidBrush knob(gdi_color(knob_color));
            graphics.FillEllipse(&knob, knob_x, pill.Y + (pill.Height - knob_d) * 0.5f, knob_d, knob_d);
        }

        if (has_focus)
        {
            const Gdiplus::RectF focus_rect(0.0f, 0.0f, w, h);
            ui_draw::focus_ring(graphics, focus_rect, radius, surface);
        }
    }

    void toggle_set_state_bit(const HWND hwnd, const ULONG_PTR bit, const bool on)
    {
        ULONG_PTR state = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (on)
            state |= bit;
        else
            state &= ~bit;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, state);
    }

    bool toggle_is_checked(const HWND hwnd)
    {
        return (GetWindowLongPtrW(hwnd, GWLP_USERDATA) & toggle_checked) != 0;
    }

    void toggle_fire(const HWND hwnd)
    {
        SendMessageW(GetParent(hwnd), WM_COMMAND,
                     MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED),
                     reinterpret_cast<LPARAM>(hwnd));
    }

    LRESULT CALLBACK toggle_wnd_proc(
        IN const HWND hwnd,
        IN const UINT message,
        IN const WPARAM w_param,
        IN const LPARAM l_param)
    {
        switch (message)
        {
        case WM_PAINT:
            {
                PAINTSTRUCT ps;
                const HDC dc = BeginPaint(hwnd, &ps);

                // Double buffer the small control to avoid flicker.
                RECT rc{};
                GetClientRect(hwnd, &rc);
                if (const HDC mem_dc = CreateCompatibleDC(dc))
                {
                    if (HBITMAP mem_bm = CreateCompatibleBitmap(dc, rc.right, rc.bottom))
                    {
                        const HBITMAP old_bm = static_cast<HBITMAP>(SelectObject(mem_dc, mem_bm));
                        paint_toggle(hwnd, mem_dc);
                        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem_dc, 0, 0, SRCCOPY);
                        SelectObject(mem_dc, old_bm);
                    }
                    DeleteDC(mem_dc);
                }
                EndPaint(hwnd, &ps);
                return 0;
            }

        case WM_LBUTTONDOWN:
            SetCapture(hwnd);
            return 0;

        case WM_LBUTTONUP:
            {
                ReleaseCapture();
                const POINT pt{ GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param) };
                RECT rc{};
                GetClientRect(hwnd, &rc);
                if (PtInRect(&rc, pt) && IsWindowEnabled(hwnd))
                {
                    toggle_set_state_bit(hwnd, toggle_checked, !toggle_is_checked(hwnd));
                    InvalidateRect(hwnd, nullptr, FALSE);
                    toggle_fire(hwnd);
                }
                return 0;
            }

        case WM_KEYDOWN:
            if (w_param == VK_SPACE && IsWindowEnabled(hwnd))
            {
                toggle_set_state_bit(hwnd, toggle_checked, !toggle_is_checked(hwnd));
                InvalidateRect(hwnd, nullptr, FALSE);
                toggle_fire(hwnd);
                return 0;
            }
            break;

        case WM_CHAR:
            if (w_param == L' ')
                return 0; // handled in WM_KEYDOWN; swallow to avoid the bell
            break;

        case WM_GETDLGCODE:
            return DLGC_WANTCHARS | DLGC_BUTTON | DLGC_HASSETSEL;

        case WM_MOUSEMOVE:
            if ((GetWindowLongPtrW(hwnd, GWLP_USERDATA) & toggle_hot) == 0)
            {
                toggle_set_state_bit(hwnd, toggle_hot, true);
                InvalidateRect(hwnd, nullptr, FALSE);

                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
            }
            return 0;

        case WM_MOUSELEAVE:
            toggle_set_state_bit(hwnd, toggle_hot, false);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_SETFOCUS:
            toggle_set_state_bit(hwnd, toggle_focus, true);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_KILLFOCUS:
            toggle_set_state_bit(hwnd, toggle_focus, false);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_ENABLE:
            InvalidateRect(hwnd, nullptr, FALSE);
            break;

        case WM_UPDATEUISTATE:
        case WM_CHANGEUISTATE:
            InvalidateRect(hwnd, nullptr, FALSE);
            break;

        case WM_NCHITTEST:
            return HTCLIENT;

        default:
            break;
        }

        return DefWindowProcW(hwnd, message, w_param, l_param);
    }

    // ---- Fluent button ---------------------------------------------------

    LRESULT CALLBACK fluent_button_subclass(
        IN const HWND hwnd,
        IN const UINT message,
        IN const WPARAM w_param,
        IN const LPARAM l_param,
        IN const UINT_PTR,
        IN const DWORD_PTR)
    {
        switch (message)
        {
        case WM_MOUSEMOVE:
            if (GetPropW(hwnd, btn_hot_prop) == nullptr)
            {
                SetPropW(hwnd, btn_hot_prop, reinterpret_cast<HANDLE>(1));
                InvalidateRect(hwnd, nullptr, FALSE);

                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
            }
            break;

        case WM_MOUSELEAVE:
            if (GetPropW(hwnd, btn_hot_prop) != nullptr)
            {
                RemovePropW(hwnd, btn_hot_prop);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;

        case WM_ENABLE:
        case WM_UPDATEUISTATE:
        case WM_CHANGEUISTATE:
            InvalidateRect(hwnd, nullptr, FALSE);
            break;

        default:
            break;
        }

        return DefSubclassProc(hwnd, message, w_param, l_param);
    }

    // Draws label text with an optional leading icon glyph, centered.
    void draw_button_label(
        Gdiplus::Graphics& graphics,
        const RECT& content,
        const std::wstring& full_text,
        COLORREF color)
    {
        if (full_text.empty())
            return;

        auto& theme = ui_theme::instance();
        const auto first = full_text[0];
        const bool has_glyph = first >= 0xE000 && first <= 0xF8FF;

        Gdiplus::RectF cr(static_cast<REAL>(content.left), static_cast<REAL>(content.top),
                          static_cast<REAL>(content.right - content.left),
                          static_cast<REAL>(content.bottom - content.top));

        if (!has_glyph)
        {
            ui_draw::text_ellipsis(graphics, theme.font_button(), full_text, cr,
                                   DT_CENTER, DT_VCENTER, color);
            return;
        }

        const std::wstring glyph(1, first);
        std::wstring label = full_text.substr(1);
        while (!label.empty() && label.front() == L' ')
            label.erase(label.begin());

        const Gdiplus::RectF glyph_box = ui_draw::measure(graphics, theme.font_glyph(), glyph);
        const Gdiplus::RectF label_box = ui_draw::measure(graphics, theme.font_button(), label);
        const REAL gap = static_cast<REAL>(ui_scale(7));
        const REAL total = glyph_box.Width + (label.empty() ? 0.0f : gap + label_box.Width);

        REAL x = cr.X + (cr.Width - total) * 0.5f;
        if (x < cr.X)
            x = cr.X;

        {
            Gdiplus::RectF g(x, cr.Y, glyph_box.Width, cr.Height);
            ui_draw::text(graphics, theme.font_glyph(), glyph, g, DT_LEFT, DT_VCENTER, color);
            x += glyph_box.Width + gap;
        }
        if (!label.empty())
        {
            Gdiplus::RectF l(x, cr.Y, cr.GetRight() - x, cr.Height);
            ui_draw::text_ellipsis(graphics, theme.font_button(), label, l, DT_LEFT, DT_VCENTER, color);
        }
    }
}

void register_toggle_class(HINSTANCE instance)
{
    UNREFERENCED_PARAMETER(instance);

    static bool registered = false;
    if (registered)
        return;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_GLOBALCLASS | CS_DBLCLKS;
    wc.lpfnWndProc = toggle_wnd_proc;
    wc.hInstance = nullptr; // CS_GLOBALCLASS
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = toggle_class_name;

    if (RegisterClassExW(&wc) != 0)
        registered = true;
}

HWND create_toggle_switch(HWND parent, HINSTANCE instance, int id, int x, int y, int width, int height)
{
    return CreateWindowExW(
        0,
        toggle_class_name,
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        x, y, width, height,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        instance,
        nullptr);
}

void toggle_set_checked(HWND hwnd, bool checked)
{
    toggle_set_state_bit(hwnd, toggle_checked, checked);
    InvalidateRect(hwnd, nullptr, FALSE);
}

bool toggle_get_checked(HWND hwnd)
{
    return toggle_is_checked(hwnd);
}

void make_fluent_button(HWND hwnd, fluent_button_style style)
{
    SetPropW(hwnd, btn_style_prop, reinterpret_cast<HANDLE>(static_cast<INT_PTR>(style)));
    SetPropW(hwnd, btn_surface_prop, reinterpret_cast<HANDLE>(static_cast<INT_PTR>(
        ui_theme::instance().colors().card_bg)));
    SetWindowSubclass(hwnd, fluent_button_subclass, 1, 0);
    InvalidateRect(hwnd, nullptr, FALSE);
}

fluent_button_style fluent_button_get_style(HWND hwnd)
{
    return static_cast<fluent_button_style>(
        reinterpret_cast<INT_PTR>(GetPropW(hwnd, btn_style_prop)));
}

bool fluent_button_is_hot(HWND hwnd)
{
    return GetPropW(hwnd, btn_hot_prop) != nullptr;
}

void fluent_button_set_surface(HWND hwnd, COLORREF surface)
{
    SetPropW(hwnd, btn_surface_prop, reinterpret_cast<HANDLE>(static_cast<INT_PTR>(surface)));
}

void draw_fluent_button(const DRAWITEMSTRUCT* dis)
{
    auto& theme = ui_theme::instance();
    const auto& p = theme.colors();

    const HWND hwnd = dis->hwndItem;
    const auto style = fluent_button_get_style(hwnd);
    const bool hot = GetPropW(hwnd, btn_hot_prop) != nullptr;
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool focused = (dis->itemState & ODS_FOCUS) != 0;

    COLORREF surface = static_cast<COLORREF>(
        reinterpret_cast<UINT_PTR>(GetPropW(hwnd, btn_surface_prop)));
    if (surface == 0)
        surface = p.card_bg;

    Gdiplus::Graphics graphics(dis->hDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // Clear the item area with the parent surface first.
    {
        Gdiplus::SolidBrush surface_brush(gdi_color(surface));
        graphics.FillRectangle(&surface_brush,
                               0, 0,
                               dis->rcItem.right - dis->rcItem.left,
                               dis->rcItem.bottom - dis->rcItem.top);
    }

    const Gdiplus::RectF rect(
        static_cast<REAL>(dis->rcItem.left) + 0.5f,
        static_cast<REAL>(dis->rcItem.top) + 0.5f,
        static_cast<REAL>(dis->rcItem.right - dis->rcItem.left) - 1.0f,
        static_cast<REAL>(dis->rcItem.bottom - dis->rcItem.top) - 1.0f);
    const REAL radius = static_cast<REAL>(ui_scale(4));

    COLORREF fill = surface;
    COLORREF stroke = surface;
    bool draw_stroke = false;
    COLORREF text_color = p.text_primary;

    switch (style)
    {
    case fluent_button_style::standard:
        fill = disabled ? p.control_disabled_bg : (pressed ? p.control_pressed : (hot ? p.control_hover : p.control_bg));
        stroke = p.control_stroke;
        draw_stroke = true;
        text_color = disabled ? p.control_disabled_text : p.text_primary;
        break;

    case fluent_button_style::accent:
        fill = disabled ? p.accent_disabled_bg : (pressed ? p.accent_pressed : (hot ? p.accent_hover : p.accent));
        text_color = disabled ? p.accent_disabled_text : p.accent_text;
        break;

    case fluent_button_style::subtle:
        fill = pressed ? p.control_pressed : (hot ? p.control_hover : surface);
        text_color = disabled ? p.control_disabled_text : p.text_primary;
        break;

    case fluent_button_style::destructive:
        fill = pressed ? p.destructive_pressed_bg : (hot ? p.destructive_hover_bg : surface);
        text_color = disabled ? p.control_disabled_text : p.destructive_text;
        break;

    default:
        break;
    }

    Gdiplus::GraphicsPath path;
    add_rounded_path(path, rect, radius);
    Gdiplus::SolidBrush fill_brush(gdi_color(fill));
    graphics.FillPath(&fill_brush, &path);
    if (draw_stroke)
    {
        const Gdiplus::Pen pen(gdi_color(stroke), 1.0f);
        graphics.DrawPath(&pen, &path);
    }

    // Label text.
    WCHAR text[256]{};
    GetWindowTextW(hwnd, text, static_cast<int>(std::size(text)));

    RECT content = dis->rcItem;
    const int pad = ui_scale(11);
    content.left += pad;
    content.right -= pad;

    draw_button_label(graphics, content, text, text_color);

    if (focused)
        ui_draw::focus_ring(graphics, rect, radius, surface);
}

// ---- ui_draw helpers -----------------------------------------------------

namespace ui_draw
{
    namespace
    {
        void apply_format(Gdiplus::StringFormat& format,
                          const UINT h_align, const UINT v_align, const bool ellipsis)
        {
            format.SetAlignment(h_align == DT_RIGHT
                                    ? Gdiplus::StringAlignmentFar
                                    : (h_align == DT_CENTER
                                           ? Gdiplus::StringAlignmentCenter
                                           : Gdiplus::StringAlignmentNear));
            format.SetLineAlignment(v_align == DT_VCENTER
                                        ? Gdiplus::StringAlignmentCenter
                                        : (v_align == DT_BOTTOM
                                               ? Gdiplus::StringAlignmentFar
                                               : Gdiplus::StringAlignmentNear));
            format.SetTrimming(ellipsis ? Gdiplus::StringTrimmingEllipsisCharacter
                                        : Gdiplus::StringTrimmingNone);
            format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        }
    }

    void round_rect(
        Gdiplus::Graphics& graphics,
        Gdiplus::RectF rect,
        REAL radius,
        const Gdiplus::Color& fill,
        const Gdiplus::Color* stroke)
    {
        Gdiplus::GraphicsPath path;
        add_rounded_path(path, rect, radius);

        Gdiplus::SolidBrush fill_brush(fill);
        graphics.FillPath(&fill_brush, &path);

        if (stroke != nullptr)
        {
            const Gdiplus::Pen pen(*stroke, 1.0f);
            graphics.DrawPath(&pen, &path);
        }
    }

    void round_rect(
        Gdiplus::Graphics& graphics,
        Gdiplus::RectF rect,
        REAL radius,
        const Gdiplus::Color& fill,
        const Gdiplus::Color& stroke)
    {
        round_rect(graphics, rect, radius, fill, &stroke);
    }

    void text(
        Gdiplus::Graphics& graphics,
        Gdiplus::Font& font,
        const std::wstring& text,
        Gdiplus::RectF rect,
        UINT h_align,
        UINT v_align,
        COLORREF color)
    {
        if (text.empty())
            return;

        Gdiplus::StringFormat format(static_cast<Gdiplus::StringFormatFlags>(0));
        apply_format(format, h_align, v_align, false);
        Gdiplus::SolidBrush brush(gdi_color(color));
        graphics.DrawString(text.c_str(), -1, &font, rect, &format, &brush);
    }

    void text_ellipsis(
        Gdiplus::Graphics& graphics,
        Gdiplus::Font& font,
        const std::wstring& text,
        Gdiplus::RectF rect,
        UINT h_align,
        UINT v_align,
        COLORREF color)
    {
        if (text.empty())
            return;

        Gdiplus::StringFormat format(static_cast<Gdiplus::StringFormatFlags>(0));
        apply_format(format, h_align, v_align, true);
        Gdiplus::SolidBrush brush(gdi_color(color));
        graphics.DrawString(text.c_str(), -1, &font, rect, &format, &brush);
    }

    void text_wrap(
        Gdiplus::Graphics& graphics,
        Gdiplus::Font& font,
        const std::wstring& text,
        Gdiplus::RectF rect,
        UINT h_align,
        COLORREF color)
    {
        if (text.empty())
            return;

        // Unlike text()/text_ellipsis() this variant does not set
        // StringFormatFlagsNoWrap, so GDI+ wraps on word boundaries.
        Gdiplus::StringFormat format(static_cast<Gdiplus::StringFormatFlags>(0));
        format.SetAlignment(h_align == DT_RIGHT
                                ? Gdiplus::StringAlignmentFar
                                : (h_align == DT_CENTER
                                       ? Gdiplus::StringAlignmentCenter
                                       : Gdiplus::StringAlignmentNear));
        format.SetLineAlignment(Gdiplus::StringAlignmentNear);
        format.SetTrimming(Gdiplus::StringTrimmingWord);
        Gdiplus::SolidBrush brush(gdi_color(color));
        graphics.DrawString(text.c_str(), -1, &font, rect, &format, &brush);
    }

    Gdiplus::RectF measure(Gdiplus::Graphics& graphics, Gdiplus::Font& font, const std::wstring& text)
    {
        Gdiplus::StringFormat format(static_cast<Gdiplus::StringFormatFlags>(0));
        apply_format(format, DT_LEFT, DT_TOP, false);
        Gdiplus::RectF bounds(0, 0, 10000, 10000);
        graphics.MeasureString(text.c_str(), static_cast<INT>(text.size()), &font, bounds, &format, &bounds);
        return bounds;
    }

    void h_line(Gdiplus::Graphics& graphics, int x, int width, int y, COLORREF color)
    {
        const Gdiplus::Pen pen(gdi_color(color), 1.0f);
        graphics.DrawLine(&pen, x + 0.5f, y + 0.5f, x + width + 0.5f, y + 0.5f);
    }

    void focus_ring(Gdiplus::Graphics& graphics, Gdiplus::RectF rect, REAL radius, COLORREF gap_color)
    {
        const REAL gap = 2.0f;
        const REAL inset = 1.0f;

        // Gap ring (surface colored) then the visible outline just outside it.
        Gdiplus::RectF gap_rect(rect.X - inset, rect.Y - inset,
                                rect.Width + inset * 2, rect.Height + inset * 2);
        {
            Gdiplus::GraphicsPath path;
            add_rounded_path(path, gap_rect, radius + inset);
            const Gdiplus::Pen pen(gdi_color(gap_color), gap);
            graphics.DrawPath(&pen, &path);
        }

        Gdiplus::RectF out_rect(rect.X - inset - gap, rect.Y - inset - gap,
                                rect.Width + (inset + gap) * 2, rect.Height + (inset + gap) * 2);
        {
            Gdiplus::GraphicsPath path;
            add_rounded_path(path, out_rect, radius + inset + gap);
            const Gdiplus::Pen pen(gdi_color(ui_theme::instance().colors().focus_outline), 1.0f);
            graphics.DrawPath(&pen, &path);
        }
    }
}
