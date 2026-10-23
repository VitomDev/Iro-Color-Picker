#include "app/picker_overlay.h"

#include "app/theme.h"
#include "core/geometry.h"

#include <shellscalingapi.h>
#include <windowsx.h>

#include <algorithm>
#include <string>
#include <vector>

namespace iro {

namespace {

constexpr wchar_t kClassName[] = L"IroPickerOverlay";

// Layout in 96-DPI pixels, at the DPI of the monitor under the pointer.
constexpr int kLoupeSize = 150;
constexpr int kLoupeOffset = 24;
constexpr int kLabelHeight = 32;
constexpr int kLabelGap = 6;
// Screen pixels across the magnifier: odd so one sits in the middle.
constexpr int kZoomLevels[] = {7, 9, 11, 15, 19, 25};
constexpr int kDefaultZoom = 2;

struct Overlay {
    const ScreenSnapshot& snapshot;
    ColorFormat format;
    POINT point{};  // Pointer, in snapshot coordinates
    int zoom = kDefaultZoom;
    bool finished = false;
    std::optional<PickResult> result;
    Theme theme = Theme::Current();

    void Finish(std::optional<PickResult> value, HWND window) {
        result = value;
        finished = true;
        ShowWindow(window, SW_HIDE);
    }
};

UINT DpiAt(const Overlay& overlay, POINT point) {
    const POINT screen{point.x + overlay.snapshot.bounds().left, point.y + overlay.snapshot.bounds().top};
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(MonitorFromPoint(screen, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    return dpiX;
}

// The monitor under the pointer, in snapshot coordinates.
RECT MonitorAt(const Overlay& overlay, POINT point) {
    const RECT& bounds = overlay.snapshot.bounds();
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(MonitorFromPoint({point.x + bounds.left, point.y + bounds.top}, MONITOR_DEFAULTTONEAREST), &info);
    RECT monitor = info.rcMonitor;
    OffsetRect(&monitor, -bounds.left, -bounds.top);
    return monitor;
}

// Magnifier and label placement: below right of the pointer, flipped when
// that would leave the monitor.
struct Placement {
    RECT loupe;
    RECT label;
    RECT both;
};

Placement Place(const Overlay& overlay, POINT point) {
    const UINT dpi = DpiAt(overlay, point);
    const auto scale = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
    const int pixels = kZoomLevels[overlay.zoom];
    // A whole number of screen pixels per magnified cell keeps the grid even.
    const int size = std::max(1, scale(kLoupeSize) / pixels) * pixels;
    const int height = size + scale(kLabelGap + kLabelHeight);
    const RECT monitor = MonitorAt(overlay, point);
    int x = point.x + scale(kLoupeOffset);
    int y = point.y + scale(kLoupeOffset);
    if (x + size > monitor.right) x = point.x - scale(kLoupeOffset) - size;
    if (y + height > monitor.bottom) y = point.y - scale(kLoupeOffset) - height;
    Placement placement;
    placement.loupe = {x, y, x + size, y + size};
    placement.label = {x, y + size + scale(kLabelGap), x + size, y + height};
    placement.both = {x - 2, y - 2, x + size + 2, y + height + 2};
    return placement;
}

void DrawLoupe(HDC dc, const Overlay& overlay, const Placement& placement) {
    const UINT dpi = DpiAt(overlay, overlay.point);
    const auto scale = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
    const int pixels = kZoomLevels[overlay.zoom];
    const RECT& loupe = placement.loupe;
    const int size = loupe.right - loupe.left;
    const int cell = size / pixels;

    // The pixels around the pointer, black outside the screen.
    std::vector<uint32_t> area(static_cast<size_t>(pixels) * pixels, 0xFF000000);
    const int half = pixels / 2;
    const auto* source = reinterpret_cast<const uint32_t*>(overlay.snapshot.pixels());
    for (int y = 0; y < pixels; ++y) {
        const int sy = overlay.point.y - half + y;
        if (sy < 0 || sy >= overlay.snapshot.height()) continue;
        for (int x = 0; x < pixels; ++x) {
            const int sx = overlay.point.x - half + x;
            if (sx >= 0 && sx < overlay.snapshot.width()) area[static_cast<size_t>(y) * pixels + x] = source[static_cast<size_t>(sy) * overlay.snapshot.width() + sx];
        }
    }
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), pixels, -pixels, 1, 32, BI_RGB};
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, loupe.left, loupe.top, size, size, 0, 0, pixels, pixels, area.data(), &info, DIB_RGB_COLORS, SRCCOPY);

    // Faint grid between magnified pixels.
    HPEN grid = CreatePen(PS_SOLID, 1, RGB(128, 128, 128));
    HGDIOBJ previousPen = SelectObject(dc, grid);
    for (int i = 1; i < pixels; ++i) {
        MoveToEx(dc, loupe.left + i * cell, loupe.top, nullptr);
        LineTo(dc, loupe.left + i * cell, loupe.bottom);
        MoveToEx(dc, loupe.left, loupe.top + i * cell, nullptr);
        LineTo(dc, loupe.right, loupe.top + i * cell);
    }
    SelectObject(dc, previousPen);
    DeleteObject(grid);

    // The picked pixel: white inside black, visible on any color.
    RECT center{loupe.left + half * cell, loupe.top + half * cell, loupe.left + (half + 1) * cell, loupe.top + (half + 1) * cell};
    InflateRect(&center, 1, 1);
    FrameRect(dc, &center, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    InflateRect(&center, -1, -1);
    FrameRect(dc, &center, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

    RECT border = loupe;
    InflateRect(&border, 2, 2);
    FrameRect(dc, &border, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    InflateRect(&border, -1, -1);
    FrameRect(dc, &border, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

    // Label: swatch and the color as it will be copied.
    const Rgb color = PixelAt(overlay.snapshot, overlay.point);
    const Theme& theme = overlay.theme;
    const RECT& label = placement.label;
    FillRounded(dc, label, theme.background, scale(4));
    OutlineRounded(dc, label, theme.divider, scale(4));
    const int swatchSize = scale(16);
    RECT swatch{label.left + scale(8), (label.top + label.bottom - swatchSize) / 2, label.left + scale(8) + swatchSize,
                (label.top + label.bottom + swatchSize) / 2};
    FillRounded(dc, swatch, color.ToColorRef(), scale(3));
    OutlineRounded(dc, swatch, theme.divider, scale(3));
    HFONT font = CreateUiFont(dpi, 9, FW_SEMIBOLD);
    HGDIOBJ previousFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, theme.text);
    const std::wstring text = Format(color, overlay.format);
    RECT textRect{swatch.right + scale(8), label.top, label.right - scale(6), label.bottom};
    DrawTextW(dc, text.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, previousFont);
    DeleteObject(font);
}

void Paint(HWND window, Overlay& overlay) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);
    const RECT& area = paint.rcPaint;
    const int width = area.right - area.left;
    const int height = area.bottom - area.top;

    // Compose off screen and copy once to avoid flicker.
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bufferBitmap = CreateCompatibleBitmap(dc, width, height);
    HGDIOBJ previousBuffer = SelectObject(buffer, bufferBitmap);
    HDC source = CreateCompatibleDC(dc);
    HGDIOBJ previousSource = SelectObject(source, overlay.snapshot.bitmap());
    BitBlt(buffer, 0, 0, width, height, source, area.left, area.top, SRCCOPY);
    SelectObject(source, previousSource);
    DeleteDC(source);

    SetViewportOrgEx(buffer, -area.left, -area.top, nullptr);
    DrawLoupe(buffer, overlay, Place(overlay, overlay.point));
    SetViewportOrgEx(buffer, 0, 0, nullptr);

    BitBlt(dc, area.left, area.top, width, height, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, previousBuffer);
    DeleteObject(bufferBitmap);
    DeleteDC(buffer);
    EndPaint(window, &paint);
}

void MoveTo(HWND window, Overlay& overlay, POINT point) {
    point.x = std::clamp<int>(point.x, 0, overlay.snapshot.width() - 1);
    point.y = std::clamp<int>(point.y, 0, overlay.snapshot.height() - 1);
    RECT invalid = Place(overlay, overlay.point).both;
    overlay.point = point;
    const RECT updated = Place(overlay, overlay.point).both;
    UnionRect(&invalid, &invalid, &updated);
    InvalidateRect(window, &invalid, FALSE);
}

void Pick(HWND window, Overlay& overlay) {
    const RECT& bounds = overlay.snapshot.bounds();
    overlay.Finish(PickResult{PixelAt(overlay.snapshot, overlay.point), {overlay.point.x + bounds.left, overlay.point.y + bounds.top}},
                   window);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* overlay = reinterpret_cast<Overlay*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        overlay = static_cast<Overlay*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(overlay));
    }
    if (!overlay) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
        case WM_PAINT:
            Paint(window, *overlay);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_CROSS));
            return TRUE;
        case WM_MOUSEMOVE:
            MoveTo(window, *overlay, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;
        case WM_LBUTTONDOWN:
            MoveTo(window, *overlay, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            Pick(window, *overlay);
            return 0;
        case WM_RBUTTONUP:
            overlay->Finish(std::nullopt, window);
            return 0;
        case WM_MOUSEWHEEL: {
            // Wheel up zooms in: fewer screen pixels across the magnifier.
            const int step = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1;
            const RECT before = Place(*overlay, overlay->point).both;
            overlay->zoom = std::clamp(overlay->zoom + step, 0, static_cast<int>(std::size(kZoomLevels)) - 1);
            RECT invalid = Place(*overlay, overlay->point).both;
            UnionRect(&invalid, &invalid, &before);
            InvalidateRect(window, &invalid, FALSE);
            return 0;
        }
        case WM_KEYDOWN: {
            const int distance = GetKeyState(VK_SHIFT) < 0 ? 10 : 1;
            POINT target = overlay->point;
            switch (wParam) {
                case VK_ESCAPE: overlay->Finish(std::nullopt, window); return 0;
                case VK_RETURN:
                case VK_SPACE: Pick(window, *overlay); return 0;
                case VK_LEFT: target.x -= distance; break;
                case VK_RIGHT: target.x += distance; break;
                case VK_UP: target.y -= distance; break;
                case VK_DOWN: target.y += distance; break;
                default: return 0;
            }
            // Move the real pointer too, so the mouse continues from there.
            MoveTo(window, *overlay, target);
            const RECT& bounds = overlay->snapshot.bounds();
            SetCursorPos(overlay->point.x + bounds.left, overlay->point.y + bounds.top);
            return 0;
        }
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE && !overlay->finished) overlay->Finish(std::nullopt, window);
            return 0;
        case WM_DISPLAYCHANGE:
            // The frozen image no longer matches the monitors.
            overlay->Finish(std::nullopt, window);
            return 0;
        case WM_DPICHANGED:
            // The overlay spans all monitors; keep its size in physical pixels.
            return 0;
        case WM_CLOSE:
            overlay->Finish(std::nullopt, window);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

Rgb PixelAt(const ScreenSnapshot& snapshot, POINT point) {
    const int x = std::clamp<int>(point.x, 0, snapshot.width() - 1);
    const int y = std::clamp<int>(point.y, 0, snapshot.height() - 1);
    const uint8_t* pixel = snapshot.pixels() + (static_cast<size_t>(y) * snapshot.width() + x) * 4;
    return {pixel[2], pixel[1], pixel[0]};
}

std::optional<PickResult> PickScreenColor(HINSTANCE instance, const ScreenSnapshot& snapshot, ColorFormat format) {
    static const ATOM windowClass = [instance] {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        wc.lpszClassName = kClassName;
        return RegisterClassExW(&wc);
    }();
    if (!windowClass) return std::nullopt;

    Overlay overlay{snapshot, format};
    POINT cursor;
    GetCursorPos(&cursor);
    overlay.point = ScreenToSnapshot(cursor, snapshot.bounds());

    const RECT& bounds = snapshot.bounds();
    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClassName, L"Iro picker", WS_POPUP, bounds.left, bounds.top,
                                  snapshot.width(), snapshot.height(), nullptr, nullptr, instance, &overlay);
    if (!window) return std::nullopt;
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    SetForegroundWindow(window);
    SetFocus(window);

    MSG message;
    while (!overlay.finished) {
        const BOOL got = GetMessageW(&message, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    DestroyWindow(window);
    return overlay.result;
}

}  // namespace iro
