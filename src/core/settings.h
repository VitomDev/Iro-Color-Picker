#pragma once

#include "core/color.h"
#include "core/hotkey.h"

#include <windows.h>

#include <string>

namespace iro {

// Registry locations. The installer and the tests rely on these names.
inline constexpr wchar_t kSettingsKey[] = L"Software\\Iro Color Picker";
inline constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t kRunValue[] = L"Iro Color Picker";
inline constexpr wchar_t kStartupApprovedKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

struct Settings {
    Hotkey pickHotkey{MOD_CONTROL | MOD_ALT, 'C'};
    ColorFormat copyFormat = ColorFormat::Hex;
    bool keepRecent = true;

    // `keyPath` is relative to HKEY_CURRENT_USER; tests use a scratch key.
    static Settings Load(const wchar_t* keyPath = kSettingsKey);
    bool Save(const wchar_t* keyPath = kSettingsKey) const;
};

// True only the first time it is called for a settings key, so Iro can open
// Settings on its very first launch. Uninstalling removes the key.
bool ConsumeFirstRun(const wchar_t* keyPath = kSettingsKey);

// "Start with Windows" is the presence of Iro's value in the per-user Run
// key, so the menu always shows the real state.
bool IsAutostartEnabled(const wchar_t* valueName = kRunValue);
bool SetAutostartEnabled(bool enabled, const std::wstring& exePath, const wchar_t* valueName = kRunValue);

std::wstring AutostartCommand(const std::wstring& exePath);

}  // namespace iro
