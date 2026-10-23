#include "core/clipboard.h"

#include <cstring>

namespace iro {

namespace {

bool OpenClipboardWithRetry(HWND owner) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (OpenClipboard(owner)) return true;
        Sleep(25);
    }
    return false;
}

HGLOBAL CopyToGlobal(const void* data, size_t size) {
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) return nullptr;
    std::memcpy(GlobalLock(memory), data, size);
    GlobalUnlock(memory);
    return memory;
}

}  // namespace

bool SetClipboardText(HWND owner, std::wstring_view text) {
    std::wstring terminated(text);
    HGLOBAL memory = CopyToGlobal(terminated.c_str(), (terminated.size() + 1) * sizeof(wchar_t));
    if (!memory) return false;

    if (!OpenClipboardWithRetry(owner)) {
        GlobalFree(memory);
        return false;
    }
    EmptyClipboard();
    const bool stored = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
    CloseClipboard();
    // On success the clipboard owns the memory.
    if (!stored) GlobalFree(memory);
    return stored;
}

std::optional<std::wstring> GetClipboardText(HWND owner) {
    if (!OpenClipboardWithRetry(owner)) return std::nullopt;
    std::optional<std::wstring> text;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* buffer = static_cast<const wchar_t*>(GlobalLock(data))) {
            text = buffer;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

}  // namespace iro
