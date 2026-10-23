#include "app/tray_app.h"

#include "app/picker_overlay.h"
#include "app/resource.h"
#include "app/screen_capture.h"
#include "app/settings_dialog.h"
#include "app/theme.h"
#include "core/clipboard.h"
#include "core/paths.h"

#include <commctrl.h>
#include <windowsx.h>

#include <vector>

namespace iro {

namespace {

constexpr UINT WM_APP_TRAY = WM_APP + 10;
constexpr UINT WM_APP_OPEN_SETTINGS = WM_APP + 14;

constexpr int kPickHotkeyId = 1;

enum MenuId {
    kMenuPick = 1,
    kMenuColors,
    kMenuSettings,
    kMenuAutostart,
    kMenuExit,
    kMenuFormatFirst = 100,  // One per format
    kMenuRecentFirst = 200,  // One per recent color
};

constexpr size_t kMenuRecentColors = 8;

// Gives memory used by a capture back to the system once Iro is idle again.
void TrimWorkingSet() { SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1)); }

// A small rounded swatch for a menu item.
HBITMAP CreateSwatchBitmap(const Rgb& color, int size) {
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB};
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) return nullptr;
    auto* pixels = static_cast<uint32_t*>(bits);
    // Premultiplied alpha, transparent corners and a darker 1 px edge.
    const auto pack = [](uint8_t r, uint8_t g, uint8_t b) { return 0xFF000000u | (r << 16) | (g << 8) | b; };
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool corner = (x == 0 || x == size - 1) && (y == 0 || y == size - 1);
            const bool edge = x == 0 || y == 0 || x == size - 1 || y == size - 1;
            pixels[y * size + x] = corner ? 0 : edge ? pack(color.r * 3 / 4, color.g * 3 / 4, color.b * 3 / 4) : pack(color.r, color.g, color.b);
        }
    }
    return bitmap;
}

}  // namespace

TrayApp::TrayApp() : recents_(RecentColorsPath()) {}

bool TrayApp::Initialize(HINSTANCE instance, bool startedAtSignIn) {
    instance_ = instance;
    settings_ = Settings::Load();

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kTrayWindowClass;
    if (!RegisterClassExW(&wc)) return false;
    // A hidden top-level window rather than a message-only one: it must
    // receive the TaskbarCreated broadcast, and the installer finds it by class.
    window_ = CreateWindowExW(0, kTrayWindowClass, L"Iro Color Picker", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (!window_) return false;

    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    AddTrayIcon();
    EnableDarkMenus();
    if (!toast_.Create(instance) || !colorsWindow_.Create(instance, &recents_)) return false;
    colorsWindow_.onPick = [this] { PickColor(); };

    const std::wstring conflict = RegisterHotkey(settings_);
    if (!conflict.empty()) {
        Notify(conflict + L" Choose another shortcut in Settings.", BalloonAction::OpenSettings);
    } else if (!startedAtSignIn) {
        Notify(L"Iro is running in the notification area. Press " + ToString(settings_.pickHotkey) +
               L" to pick a color from anywhere on your screen.");
    }
    // Very first launch: show Settings once the message loop runs.
    if (ConsumeFirstRun()) PostMessageW(window_, WM_APP_OPEN_SETTINGS, 0, 0);
    TrimWorkingSet();
    return true;
}

bool TrayApp::PreTranslate(MSG& message) {
    HWND window = colorsWindow_.window();
    if (window && IsWindowVisible(window) && (message.hwnd == window || IsChild(window, message.hwnd))) {
        return IsDialogMessageW(window, &message) != FALSE;
    }
    return false;
}

// --- Tray icon ---------------------------------------------------------------

void TrayApp::AddTrayIcon() {
    icon_ = {sizeof(icon_)};
    icon_.hWnd = window_;
    icon_.uID = 1;
    icon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    icon_.uCallbackMessage = WM_APP_TRAY;
    LoadIconMetric(instance_, MAKEINTRESOURCEW(IDI_IRO), LIM_SMALL, &icon_.hIcon);
    wcscpy_s(icon_.szTip, L"Iro Color Picker");
    Shell_NotifyIconW(NIM_ADD, &icon_);
    icon_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &icon_);
}

void TrayApp::ShowTrayMenu(POINT point) {
    HMENU menu = CreatePopupMenu();
    const std::wstring pick = L"Pick color\t" + ToString(settings_.pickHotkey);
    AppendMenuW(menu, MF_STRING, kMenuPick, pick.c_str());
    AppendMenuW(menu, MF_STRING, kMenuColors, L"Colors\u2026");

    // The last colors, a click copies one again.
    const std::vector<Rgb> recent = settings_.keepRecent ? recents_.Load() : std::vector<Rgb>{};
    std::vector<HBITMAP> swatches;
    if (!recent.empty()) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        const int size = GetSystemMetricsForDpi(SM_CYMENUCHECK, GetDpiForWindow(window_));
        for (size_t i = 0; i < recent.size() && i < kMenuRecentColors; ++i) {
            const std::wstring label = Format(recent[i], settings_.copyFormat);
            AppendMenuW(menu, MF_STRING, kMenuRecentFirst + i, label.c_str());
            if (HBITMAP swatch = CreateSwatchBitmap(recent[i], size)) {
                MENUITEMINFOW item{sizeof(item), MIIM_BITMAP};
                item.hbmpItem = swatch;
                SetMenuItemInfoW(menu, kMenuRecentFirst + static_cast<UINT>(i), FALSE, &item);
                swatches.push_back(swatch);
            }
        }
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    HMENU formats = CreatePopupMenu();
    for (size_t i = 0; i < kColorFormats.size(); ++i) {
        const std::wstring label = FormatName(kColorFormats[i]) + L"\t" + Format({26, 115, 232}, kColorFormats[i]);
        AppendMenuW(formats, MF_STRING, kMenuFormatFirst + i, label.c_str());
    }
    const UINT checked = kMenuFormatFirst + static_cast<UINT>(settings_.copyFormat);
    CheckMenuRadioItem(formats, kMenuFormatFirst, kMenuFormatFirst + static_cast<UINT>(kColorFormats.size()) - 1, checked, MF_BYCOMMAND);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(formats), L"Copy as");
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"Settings");
    AppendMenuW(menu, MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : 0), kMenuAutostart, L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit Iro");
    SetMenuDefaultItem(menu, kMenuPick, FALSE);

    // Required so the menu closes when clicking elsewhere.
    SetForegroundWindow(window_);
    const UINT command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, point.x, point.y, window_, nullptr);
    DestroyMenu(menu);
    for (HBITMAP swatch : swatches) DeleteObject(swatch);

    if (command >= kMenuRecentFirst && command < kMenuRecentFirst + recent.size()) {
        CopyColor(recent[command - kMenuRecentFirst], point);
        return;
    }
    if (command >= kMenuFormatFirst && command < kMenuFormatFirst + kColorFormats.size()) {
        settings_.copyFormat = kColorFormats[command - kMenuFormatFirst];
        settings_.Save();
        return;
    }
    switch (command) {
        case kMenuPick: PickColor(); break;
        case kMenuColors: colorsWindow_.Show(settings_.keepRecent); break;
        case kMenuSettings: OpenSettings(); break;
        case kMenuAutostart: ToggleAutostart(); break;
        case kMenuExit: Exit(); break;
    }
}

void TrayApp::Notify(const std::wstring& message, BalloonAction action) {
    balloonAction_ = action;
    NOTIFYICONDATAW data = icon_;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    wcscpy_s(data.szInfoTitle, L"Iro Color Picker");
    wcsncpy_s(data.szInfo, message.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

// --- Settings ----------------------------------------------------------------

std::wstring TrayApp::RegisterHotkey(const Settings& settings) {
    UnregisterHotKey(window_, kPickHotkeyId);
    if (!RegisterHotKey(window_, kPickHotkeyId, settings.pickHotkey.modifiers | MOD_NOREPEAT, settings.pickHotkey.key)) {
        return ToString(settings.pickHotkey) + L" is already used by another app.";
    }
    return {};
}

bool TrayApp::ApplySettings(const Settings& settings, bool autostart, std::wstring& error) {
    error = RegisterHotkey(settings);
    if (!error.empty()) {
        // Settings stays open: keep the shortcut paused; the old one comes back on close.
        UnregisterHotKey(window_, kPickHotkeyId);
        return false;
    }
    settings_ = settings;
    if (!settings_.Save()) {
        error = L"Could not save the settings.";
        return false;
    }
    if (autostart != IsAutostartEnabled() && !SetAutostartEnabled(autostart, ExecutablePath().wstring())) {
        error = L"Could not change Start with Windows.";
        return false;
    }
    return true;
}

void TrayApp::OpenSettings() {
    if (pickActive_) return;
    // Pause the shortcut so it can be recorded in Settings.
    UnregisterHotKey(window_, kPickHotkeyId);
    ShowSettingsDialog(
        instance_, window_, settings_,
        [this](const Settings& settings, bool autostart, std::wstring& error) { return ApplySettings(settings, autostart, error); },
        [this] {
            recents_.Clear();
            if (colorsWindow_.IsVisible()) colorsWindow_.Refresh();
        });
    if (!exiting_) RegisterHotkey(settings_);
    TrimWorkingSet();
}

void TrayApp::ToggleAutostart() {
    if (!SetAutostartEnabled(!IsAutostartEnabled(), ExecutablePath().wstring())) {
        Notify(L"Could not change Start with Windows.");
    }
}

void TrayApp::Exit() {
    exiting_ = true;
    toast_.Destroy();
    colorsWindow_.Destroy();
    UnregisterHotKey(window_, kPickHotkeyId);
    Shell_NotifyIconW(NIM_DELETE, &icon_);
    if (icon_.hIcon) DestroyIcon(icon_.hIcon);
    DestroyWindow(window_);
}

// --- Picking -----------------------------------------------------------------

void TrayApp::PickColor() {
    // One pick at a time; repeated shortcuts while picking are ignored.
    if (pickActive_ || exiting_) return;
    pickActive_ = true;
    // The Colors window must not end up in the capture.
    const bool colorsWasVisible = colorsWindow_.IsVisible();
    if (colorsWasVisible) {
        colorsWindow_.Hide();
        // Let the desktop repaint where the window was before the snapshot.
        Sleep(150);
    }

    auto snapshot = ScreenSnapshot::Capture();
    std::optional<PickResult> result;
    if (snapshot) {
        result = PickScreenColor(instance_, *snapshot, settings_.copyFormat);
    } else {
        Notify(L"Could not capture the screen.");
    }
    snapshot.reset();
    pickActive_ = false;

    if (result) {
        if (settings_.keepRecent) recents_.Add(result->color);
        CopyColor(result->color, result->screenPoint);
    }
    if (colorsWasVisible) {
        colorsWindow_.Show(settings_.keepRecent, result ? std::optional<Rgb>(result->color) : std::nullopt);
    }
    TrimWorkingSet();
}

void TrayApp::CopyColor(const Rgb& color, POINT point) {
    const std::wstring text = Format(color, settings_.copyFormat);
    const RECT anchor{point.x - 1, point.y - 1, point.x + 1, point.y + 1};
    if (SetClipboardText(window_, text)) {
        toast_.Show(anchor, text + L" copied", Toast::Kind::Success, color.ToColorRef());
    } else {
        toast_.Show(anchor, L"Couldn't access the clipboard. Try again.", Toast::Kind::Info);
    }
}

// --- Window procedure ----------------------------------------------------------

LRESULT CALLBACK TrayApp::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* app = static_cast<TrayApp*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        app->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    auto* app = reinterpret_cast<TrayApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return app ? app->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT TrayApp::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_ != 0) {
        AddTrayIcon();  // Explorer restarted.
        return 0;
    }
    switch (message) {
        case WM_HOTKEY:
            PickColor();
            return 0;
        case WM_APP_TRAY:
            switch (LOWORD(lParam)) {
                case NIN_SELECT:
                case NIN_KEYSELECT:
                    PickColor();
                    break;
                case WM_CONTEXTMENU:
                    ShowTrayMenu({GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)});
                    break;
                case NIN_BALLOONUSERCLICK:
                    if (balloonAction_ == BalloonAction::OpenSettings) OpenSettings();
                    break;
            }
            return 0;
        case WM_APP_OPEN_SETTINGS:
            OpenSettings();
            return 0;
        case WM_APP_ALREADY_RUNNING:
            Notify(L"Iro is already running. Press " + ToString(settings_.pickHotkey) + L" to pick a color.");
            return 0;
        case WM_CLOSE:  // Sent by the installer before upgrading or uninstalling.
        case WM_ENDSESSION:
            if (message == WM_CLOSE || wParam) Exit();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

}  // namespace iro
