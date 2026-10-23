#pragma once

#include <windows.h>

#include <string>

namespace iro {

// A small pill next to the picked point ("#1A2B3C copied") that fades out on
// its own. It never takes focus and lets clicks pass through.
class Toast {
public:
    enum class Kind { Success, Info };

    bool Create(HINSTANCE instance);
    void Destroy();
    // With a `swatch`, a color sample replaces the icon.
    void Show(const RECT& anchor, const std::wstring& text, Kind kind, COLORREF swatch = CLR_INVALID);

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void Paint();

    HWND window_ = nullptr;
    std::wstring text_;
    Kind kind_ = Kind::Success;
    COLORREF swatch_ = CLR_INVALID;
    int alpha_ = 255;
};

}  // namespace iro
