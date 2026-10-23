#pragma once

#include "app/colors_window.h"
#include "app/toast.h"
#include "core/recents.h"
#include "core/settings.h"

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace iro {

inline constexpr wchar_t kTrayWindowClass[] = L"IroColorPickerTray";
inline constexpr UINT WM_APP_ALREADY_RUNNING = WM_APP + 12;

// The background application: tray icon, the global shortcut, picking a
// color from the screen and the Colors window.
class TrayApp {
public:
    TrayApp();
    bool Initialize(HINSTANCE instance, bool startedAtSignIn);
    // Lets the Colors window handle Tab, Enter and Escape. Returns true if consumed.
    bool PreTranslate(MSG& message);

private:
    enum class BalloonAction { None, OpenSettings };

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void AddTrayIcon();
    void ShowTrayMenu(POINT point);
    void Notify(const std::wstring& message, BalloonAction action = BalloonAction::None);

    std::wstring RegisterHotkey(const Settings& settings);
    bool ApplySettings(const Settings& settings, bool autostart, std::wstring& error);
    void OpenSettings();
    void ToggleAutostart();
    void Exit();

    void PickColor();
    // Copies in the chosen format and confirms with a toast next to `point`.
    void CopyColor(const Rgb& color, POINT point);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;
    NOTIFYICONDATAW icon_{};
    Settings settings_;
    BalloonAction balloonAction_ = BalloonAction::None;
    bool pickActive_ = false;
    bool exiting_ = false;

    RecentColors recents_;
    ColorsWindow colorsWindow_;
    Toast toast_;
};

}  // namespace iro
