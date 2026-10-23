#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace iro {

// Puts Unicode text on the clipboard. Another application may hold the
// clipboard open for a moment, so opening is retried briefly.
bool SetClipboardText(HWND owner, std::wstring_view text);

std::optional<std::wstring> GetClipboardText(HWND owner);


}  // namespace iro
