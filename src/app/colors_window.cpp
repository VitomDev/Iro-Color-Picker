#include "app/colors_window.h"

#include "app/resource.h"
#include "core/clipboard.h"

#include <windowsx.h>

#include <algorithm>

namespace iro {

namespace {

constexpr wchar_t kClassName[] = L"IroColors";

enum ControlId {
    kPickButton = 100,
    kClearButton,
    kCopyFirst = 200,  // One per format, in kColorFormats order
};

constexpr UINT_PTR kCopiedTimer = 1;

// Layout in 96-DPI pixels.
constexpr int kWidth = 540;
constexpr int kPadding = 24;
constexpr int kPreviewSize = 176;
constexpr int kRowHeight = 32;
constexpr int kSwatchSize = 32;
constexpr int kSwatchGap = 8;
constexpr int kSwatchColumns = 12;
constexpr int kButtonHeight = 32;
constexpr int kRadius = 4;

}  // namespace

bool ColorsWindow::Create(HINSTANCE instance, RecentColors* recents) {
    instance_ = instance;
    recents_ = recents;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_IRO));
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    window_ = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_APPWINDOW, kClassName, L"Colors \u2013 Iro",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN, CW_USEDEFAULT,
                              CW_USEDEFAULT, 100, 100, nullptr, nullptr, instance, this);
    if (!window_) return false;
    std::vector<int> ids{kPickButton, kClearButton};
    for (size_t i = 0; i < kColorFormats.size(); ++i) ids.push_back(kCopyFirst + static_cast<int>(i));
    for (int id : ids) {
        HWND button = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, window_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        EnableHover(button);
    }
    return true;
}

void ColorsWindow::Destroy() {
    if (window_) DestroyWindow(window_);
    window_ = nullptr;
    for (HGDIOBJ object : {static_cast<HGDIOBJ>(backgroundBrush_), static_cast<HGDIOBJ>(titleFont_),
                           static_cast<HGDIOBJ>(captionFont_), static_cast<HGDIOBJ>(textFont_),
                           static_cast<HGDIOBJ>(valueFont_), static_cast<HGDIOBJ>(iconFont_)}) {
        if (object) DeleteObject(object);
    }
    backgroundBrush_ = nullptr;
    titleFont_ = captionFont_ = textFont_ = valueFont_ = iconFont_ = nullptr;
}

int ColorsWindow::Scale(int value) const { return MulDiv(value, GetDpiForWindow(window_), 96); }

void ColorsWindow::RefreshStyle() {
    theme_ = Theme::Current();
    if (backgroundBrush_) DeleteObject(backgroundBrush_);
    backgroundBrush_ = CreateSolidBrush(theme_.background);
    for (HFONT font : {titleFont_, captionFont_, textFont_, valueFont_, iconFont_}) {
        if (font) DeleteObject(font);
    }
    const UINT dpi = GetDpiForWindow(window_);
    titleFont_ = CreateUiFont(dpi, 16, FW_SEMIBOLD);
    captionFont_ = CreateUiFont(dpi, 10, FW_SEMIBOLD);
    textFont_ = CreateUiFont(dpi, 10);
    valueFont_ = CreateUiFont(dpi, 11);
    iconFont_ = CreateIconFont(dpi, 10);
    ApplyWindowStyle(window_, theme_);
}

RECT ColorsWindow::SwatchRect(size_t index) const {
    const int size = Scale(kSwatchSize);
    const int step = size + Scale(kSwatchGap);
    const int column = static_cast<int>(index) % kSwatchColumns;
    const int row = static_cast<int>(index) / kSwatchColumns;
    const int x = recentArea_.left + column * step;
    const int y = recentArea_.top + row * step;
    return {x, y, x + size, y + size};
}

void ColorsWindow::Layout() {
    const int width = Scale(kWidth);
    const int padding = Scale(kPadding);
    const ButtonFonts fonts{textFont_, iconFont_};
    const UINT dpi = GetDpiForWindow(window_);
    const auto place = [&](int id, int x, int y, int w, int h) {
        SetWindowPos(GetDlgItem(window_, id), nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    };

    int y = padding;
    titleRect_ = {padding, y, width / 2, y + Scale(32)};
    const int pickWidth = ButtonWidth(window_, fonts, icons::kColor, L"Pick color", dpi);
    place(kPickButton, width - padding - pickWidth, y, pickWidth, Scale(kButtonHeight));
    y += Scale(32) + Scale(18);

    // Preview on the left, the formats on the right.
    preview_ = {padding, y, padding + Scale(kPreviewSize), y + Scale(kPreviewSize)};
    const int rowsLeft = preview_.right + Scale(24);
    formatRows_.clear();
    const int rowHeight = (Scale(kPreviewSize) - Scale(4)) / static_cast<int>(kColorFormats.size());
    for (size_t i = 0; i < kColorFormats.size(); ++i) {
        const int top = y + static_cast<int>(i) * rowHeight;
        formatRows_.push_back({rowsLeft, top, width - padding, top + rowHeight});
        place(kCopyFirst + static_cast<int>(i), width - padding - Scale(kRowHeight), top + (rowHeight - Scale(kRowHeight)) / 2,
              Scale(kRowHeight), Scale(kRowHeight));
    }
    y = preview_.bottom + Scale(26);

    recentCaption_ = {padding, y, width - padding, y + Scale(24)};
    const int clearWidth = ButtonWidth(window_, fonts, nullptr, L"Clear", dpi);
    place(kClearButton, width - padding - clearWidth + Scale(8), y - Scale(4), clearWidth, Scale(kButtonHeight));
    y += Scale(24) + Scale(10);
    const int rows = 2;  // kMaxColors / kSwatchColumns
    recentArea_ = {padding, y, width - padding, y + rows * Scale(kSwatchSize) + (rows - 1) * Scale(kSwatchGap)};
    y = recentArea_.bottom + padding;

    RECT frame{0, 0, width, y};
    AdjustWindowRectExForDpi(&frame, GetWindowLongW(window_, GWL_STYLE), FALSE, GetWindowLongW(window_, GWL_EXSTYLE), dpi);
    SetWindowPos(window_, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    laidOut_ = true;
    InvalidateRect(window_, nullptr, TRUE);
}

void ColorsWindow::Paint() {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window_, &paint);
    RECT client;
    GetClientRect(window_, &client);
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HGDIOBJ previousBitmap = SelectObject(buffer, bitmap);
    FillRect(buffer, &client, backgroundBrush_);
    SetBkMode(buffer, TRANSPARENT);
    const int radius = Scale(kRadius);

    HGDIOBJ previousFont = SelectObject(buffer, titleFont_);
    SetTextColor(buffer, theme_.text);
    DrawTextW(buffer, L"Colors", -1, &titleRect_, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (selected_) {
        FillRounded(buffer, preview_, selected_->ToColorRef(), Scale(8));
        OutlineRounded(buffer, preview_, theme_.divider, Scale(8));
        // The HEX value on the swatch itself, in a color that reads on it.
        SelectObject(buffer, captionFont_);
        SetTextColor(buffer, IsLight(*selected_) ? RGB(0, 0, 0) : RGB(255, 255, 255));
        RECT label{preview_.left + Scale(12), preview_.bottom - Scale(34), preview_.right - Scale(12), preview_.bottom - Scale(8)};
        DrawTextW(buffer, Format(*selected_, ColorFormat::Hex).c_str(), -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        FillRounded(buffer, preview_, theme_.pressed, Scale(8));
        OutlineRounded(buffer, preview_, theme_.divider, Scale(8));
        HFONT big = CreateIconFont(GetDpiForWindow(window_), 28);
        SelectObject(buffer, big);
        SetTextColor(buffer, theme_.secondaryText);
        RECT icon = preview_;
        DrawTextW(buffer, icons::kColor, -1, &icon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(buffer, textFont_);
        DeleteObject(big);
    }

    for (size_t i = 0; i < formatRows_.size(); ++i) {
        RECT row = formatRows_[i];
        RECT name{row.left, row.top, row.left + Scale(72), row.bottom};
        SelectObject(buffer, textFont_);
        SetTextColor(buffer, theme_.secondaryText);
        DrawTextW(buffer, FormatName(kColorFormats[i]).c_str(), -1, &name, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT value{name.right, row.top, row.right - Scale(kRowHeight + 8), row.bottom};
        SelectObject(buffer, valueFont_);
        SetTextColor(buffer, selected_ ? theme_.text : theme_.secondaryText);
        const std::wstring text = selected_ ? Format(*selected_, kColorFormats[i]) : L"\u2013";
        DrawTextW(buffer, text.c_str(), -1, &value, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (i + 1 < formatRows_.size()) {
            RECT divider{row.left, row.bottom - 1, row.right, row.bottom};
            HBRUSH brush = CreateSolidBrush(theme_.divider);
            FillRect(buffer, &divider, brush);
            DeleteObject(brush);
        }
    }

    SelectObject(buffer, captionFont_);
    SetTextColor(buffer, theme_.text);
    DrawTextW(buffer, L"Recent colors", -1, &recentCaption_, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (colors_.empty()) {
        SelectObject(buffer, textFont_);
        SetTextColor(buffer, theme_.secondaryText);
        RECT hint = recentArea_;
        DrawTextW(buffer,
                  enabled_ ? L"Colors you pick will appear here." : L"Recent colors are turned off. Turn them on in Settings.", -1,
                  &hint, DT_LEFT | DT_TOP | DT_SINGLELINE);
    }
    for (size_t i = 0; i < colors_.size(); ++i) {
        RECT swatch = SwatchRect(i);
        const bool isSelected = selected_ && *selected_ == colors_[i];
        FillRounded(buffer, swatch, colors_[i].ToColorRef(), radius);
        if (isSelected) {
            RECT ring = swatch;
            InflateRect(&ring, Scale(3), Scale(3));
            OutlineRounded(buffer, ring, theme_.accent, radius + Scale(3), Scale(2));
        } else {
            OutlineRounded(buffer, swatch, theme_.divider, radius);
        }
    }

    BitBlt(dc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, previousFont);
    SelectObject(buffer, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(buffer);
    EndPaint(window_, &paint);
}

void ColorsWindow::Draw(const DRAWITEMSTRUCT& item) {
    const ButtonFonts fonts{textFont_, iconFont_};
    const int radius = Scale(kRadius);
    const int id = static_cast<int>(item.CtlID);
    if (id == kPickButton) {
        DrawButton(item, theme_, fonts, ButtonStyle::Primary, icons::kColor, L"Pick color", radius);
    } else if (id == kClearButton) {
        DrawButton(item, theme_, fonts, ButtonStyle::Subtle, nullptr, L"Clear", radius);
    } else if (id >= kCopyFirst) {
        const bool copied = id - kCopyFirst == copiedIndex_;
        DrawButton(item, theme_, fonts, ButtonStyle::Subtle, copied ? icons::kCheck : icons::kCopy, nullptr, radius,
                   copied ? theme_.accent : theme_.secondaryText);
    }
}

void ColorsWindow::Copy(size_t formatIndex) {
    if (!selected_ || formatIndex >= kColorFormats.size()) return;
    if (!SetClipboardText(window_, Format(*selected_, kColorFormats[formatIndex]))) return;
    const int previous = copiedIndex_;
    copiedIndex_ = static_cast<int>(formatIndex);
    if (previous >= 0) InvalidateRect(GetDlgItem(window_, kCopyFirst + previous), nullptr, FALSE);
    InvalidateRect(GetDlgItem(window_, kCopyFirst + copiedIndex_), nullptr, FALSE);
    SetTimer(window_, kCopiedTimer, 1500, nullptr);
}

void ColorsWindow::Refresh(std::optional<Rgb> select) {
    colors_ = recents_->Load();
    if (select) {
        selected_ = select;
    } else if (!selected_ || std::find(colors_.begin(), colors_.end(), *selected_) == colors_.end()) {
        selected_ = colors_.empty() ? std::nullopt : std::optional<Rgb>(colors_.front());
    }
    const bool hasColor = selected_.has_value();
    for (size_t i = 0; i < kColorFormats.size(); ++i) EnableWindow(GetDlgItem(window_, kCopyFirst + static_cast<int>(i)), hasColor);
    ShowWindow(GetDlgItem(window_, kClearButton), colors_.empty() ? SW_HIDE : SW_SHOW);
    InvalidateRect(window_, nullptr, FALSE);
}

void ColorsWindow::Show(bool enabled, std::optional<Rgb> select) {
    enabled_ = enabled;
    if (!laidOut_) {
        // Open on the monitor with the mouse, roughly centered.
        POINT cursor;
        GetCursorPos(&cursor);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
        SetWindowPos(window_, nullptr, monitor.rcWork.left, monitor.rcWork.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        RefreshStyle();
        Layout();
        RECT bounds;
        GetWindowRect(window_, &bounds);
        const RECT& work = monitor.rcWork;
        SetWindowPos(window_, nullptr, work.left + (work.right - work.left - (bounds.right - bounds.left)) / 2,
                     std::max<int>(work.top, work.top + (work.bottom - work.top - (bounds.bottom - bounds.top)) / 2), 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER);
    } else {
        RefreshStyle();
        Layout();
    }
    Refresh(select);
    ShowWindow(window_, IsIconic(window_) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(window_);
    SetFocus(GetDlgItem(window_, kPickButton));
    SendMessageW(window_, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
}

void ColorsWindow::Hide() {
    if (window_) ShowWindow(window_, SW_HIDE);
}

LRESULT CALLBACK ColorsWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* self = static_cast<ColorsWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<ColorsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return self ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT ColorsWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == kPickButton) {
                if (onPick) onPick();
            } else if (id == kClearButton) {
                if (MessageBoxW(window_, L"Remove all recent colors?", L"Iro Color Picker", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    recents_->Clear();
                    selected_.reset();
                    Refresh();
                    if (onCleared) onCleared();
                }
            } else if (id >= kCopyFirst && id < kCopyFirst + static_cast<int>(kColorFormats.size())) {
                Copy(static_cast<size_t>(id - kCopyFirst));
            } else if (id == IDCANCEL) {
                Hide();
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            for (size_t i = 0; i < colors_.size(); ++i) {
                const RECT swatch = SwatchRect(i);
                if (!PtInRect(&swatch, point)) continue;
                selected_ = colors_[i];
                InvalidateRect(window_, nullptr, FALSE);
                // Double-click copies in the first format, HEX.
                if (message == WM_LBUTTONDBLCLK) Copy(0);
                break;
            }
            return 0;
        }
        case WM_SETCURSOR: {
            POINT point;
            GetCursorPos(&point);
            ScreenToClient(window_, &point);
            for (size_t i = 0; i < colors_.size(); ++i) {
                const RECT swatch = SwatchRect(i);
                if (PtInRect(&swatch, point)) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
            }
            break;
        }
        case WM_DRAWITEM:
            Draw(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER: {
            KillTimer(window_, wParam);
            const int previous = copiedIndex_;
            copiedIndex_ = -1;
            if (previous >= 0) InvalidateRect(GetDlgItem(window_, kCopyFirst + previous), nullptr, FALSE);
            return 0;
        }
        case WM_SETTINGCHANGE:
            RefreshStyle();
            Layout();
            return 0;
        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(window_, nullptr, suggested->left, suggested->top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            RefreshStyle();
            Layout();
            return 0;
        }
        case WM_CLOSE:
            Hide();
            return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

}  // namespace iro
