#include "stdafx.h"
#include "aboutdlg.h"

#include <shellapi.h>

#include "afphelp.h"
#include "resource.h"
#include "version.h"
#include "ui_theme.h"
#include "ui_controls.h"

extern HINSTANCE gh_instance;
extern BOOL g_main_window_visible;

namespace
{
    Gdiplus::Color gdi_color(const COLORREF c)
    {
        return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
    }

    void paint_about(const HWND h_dlg, HDC const dest_dc)
    {
        RECT client{};
        GetClientRect(h_dlg, &client);
        const int client_w = client.right;
        const int client_h = client.bottom;
        if (client_w <= 0 || client_h <= 0)
            return;

        // Render into a 32-bit memory bitmap and blit: GDI+ grayscale AA
        // text degrades to 1-bit output when drawn straight onto a display
        // DC (same failure mode as owner-draw item DCs).
        HDC mem_dc = CreateCompatibleDC(dest_dc);
        if (mem_dc == nullptr)
            return;
        HBITMAP mem_bm = CreateCompatibleBitmap(dest_dc, client_w, client_h);
        if (mem_bm == nullptr)
        {
            DeleteDC(mem_dc);
            return;
        }
        HGDIOBJ old_bm = SelectObject(mem_dc, mem_bm);
        HDC dc = mem_dc;

        const auto& p = ui_theme::instance().colors();

        SetBkMode(dc, TRANSPARENT);
        Gdiplus::Graphics gfx(dc);
        gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

        {
            Gdiplus::SolidBrush bg(gdi_color(p.window_bg));
            gfx.FillRectangle(&bg, 0, 0, client.right, client.bottom);
        }

        const int icon_size = ui_scale(50);
        if (const HICON h_icon = LoadIconW(gh_instance, MAKEINTRESOURCE(IDI_TRAYICON)))
            DrawIconEx(dc, ui_scale(25), ui_scale(22), h_icon, icon_size, icon_size, 0, nullptr, DI_NORMAL);

        const int text_x = ui_scale(25) + icon_size + ui_scale(16);

        {
            Gdiplus::RectF rc(static_cast<REAL>(text_x), static_cast<REAL>(ui_scale(22)),
                              static_cast<REAL>(client.right - text_x - ui_scale(16)), static_cast<REAL>(ui_scale(28)));
            ui_draw::text(gfx, ui_theme::instance().font_title(),
                          L"MacFile", rc, DT_LEFT, DT_TOP, p.text_primary);
        }
        {
            const std::wstring version = string_to_wide_string(VER_FILE_SHORT_VERSION_STR, CP_UTF8);
            Gdiplus::RectF rc(static_cast<REAL>(text_x), static_cast<REAL>(ui_scale(52)),
                              static_cast<REAL>(client.right - text_x - ui_scale(16)), static_cast<REAL>(ui_scale(18)));
            ui_draw::text(gfx, ui_theme::instance().font_caption(),
                          L"Version " + version, rc, DT_LEFT, DT_TOP, p.text_secondary);
        }
        {
            Gdiplus::RectF rc(static_cast<REAL>(ui_scale(25)), static_cast<REAL>(ui_scale(92)),
                              static_cast<REAL>(client.right - ui_scale(50)), static_cast<REAL>(ui_scale(20)));
            ui_draw::text(gfx, ui_theme::instance().font_body(),
                          L"A personal Apple Filing Protocol (AFP 3.2) server for Windows.",
                          rc, DT_LEFT, DT_TOP, p.text_primary);
        }
        {
            Gdiplus::RectF rc(static_cast<REAL>(ui_scale(25)), static_cast<REAL>(ui_scale(118)),
                              static_cast<REAL>(client.right - ui_scale(50)), static_cast<REAL>(ui_scale(18)));
            ui_draw::text(gfx, ui_theme::instance().font_caption(),
                          L"Copyright \u00a9 2026, Michael J. Conrad. MIT licensed.",
                          rc, DT_LEFT, DT_TOP, p.text_secondary);
        }
        {
            Gdiplus::RectF rc(static_cast<REAL>(ui_scale(25)), static_cast<REAL>(ui_scale(138)),
                              static_cast<REAL>(client.right - ui_scale(50)), static_cast<REAL>(ui_scale(18)));
            ui_draw::text(gfx, ui_theme::instance().font_caption(),
                          L"github.com/anti-matter/MacFile",
                          rc, DT_LEFT, DT_TOP, p.text_tertiary);
        }

        gfx.Flush();
        BitBlt(dest_dc, 0, 0, client_w, client_h, mem_dc, 0, 0, SRCCOPY);
        SelectObject(mem_dc, old_bm);
        DeleteObject(mem_bm);
        DeleteDC(mem_dc);
    }
}

LRESULT CALLBACK about_dlg_proc(
    IN const HWND h_dlg,
    IN const DWORD message,
    IN const WPARAM w_param,
    IN const LPARAM l_param
)
{
    switch (message)
    {
    case WM_INITDIALOG:
        {
            ui_theme::apply_titlebar_theme(h_dlg);

            HWND ok = GetDlgItem(h_dlg, IDOK);
            make_fluent_button(ok, fluent_button_style::accent);
            fluent_button_set_surface(ok, ui_theme::instance().colors().window_bg);

            // Position the button from the client rect so it is DPI safe.
            RECT client{};
            GetClientRect(h_dlg, &client);
            const int w = ui_scale(88);
            const int h = ui_scale(32);
            MoveWindow(ok, client.right - w - ui_scale(16), client.bottom - h - ui_scale(14), w, h, TRUE);

            SetFocus(ok);
            return TRUE;
        }

    case WM_ERASEBKGND:
        paint_about(h_dlg, reinterpret_cast<HDC>(w_param));
        return TRUE;

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            const HDC dc = BeginPaint(h_dlg, &ps);
            paint_about(h_dlg, dc);
            EndPaint(h_dlg, &ps);
            return TRUE;
        }

    case WM_DRAWITEM:
        draw_fluent_button(reinterpret_cast<const DRAWITEMSTRUCT*>(l_param));
        return TRUE;

    case WM_CTLCOLORSTATIC:
        {
            const auto dc = reinterpret_cast<HDC>(w_param);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, ui_theme::instance().colors().text_primary);
            return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
        }

    case WM_COMMAND:
        switch (LOWORD(w_param))
        {
        case IDOK:
        case IDCANCEL:
            EndDialog(h_dlg, LOWORD(w_param));
            return TRUE;
        default:
            break;
        }
        break;

    default:
        break;
    }

    return FALSE;
}

void about_dialog(const HWND h_parent)
{
    if (g_main_window_visible)
    {
        DialogBox(
            gh_instance,
            MAKEINTRESOURCE(IDD_ABOUT),
            h_parent,
            reinterpret_cast<DLGPROC>(about_dlg_proc));
    }
}
