#pragma once

#include "app/theme.h"
#include "core/color.h"
#include "core/recents.h"

#include <windows.h>

#include <functional>
#include <optional>
#include <vector>

namespace iro {

// The Colors window: the selected color large, in every format with a copy
// button each, and the recent colors below to choose from.
class ColorsWindow {
public:
    std::function<void()> onPick;      // "Pick color" button
    std::function<void()> onCleared;   // Recent colors were cleared here

    bool Create(HINSTANCE instance, RecentColors* recents);
    void Destroy();

    // `enabled` is the Remember recent colors setting, which only changes the
    // hint shown when there are none. `select` shows that color first.
    void Show(bool enabled, std::optional<Rgb> select = std::nullopt);
    void Refresh(std::optional<Rgb> select = std::nullopt);
    void Hide();

    HWND window() const { return window_; }
    bool IsVisible() const { return window_ && IsWindowVisible(window_); }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void RefreshStyle();
    void Layout();
    void Paint();
    void Draw(const DRAWITEMSTRUCT& item);
    void Copy(size_t formatIndex);
    RECT SwatchRect(size_t index) const;
    int Scale(int value) const;

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    RecentColors* recents_ = nullptr;
    std::vector<Rgb> colors_;
    std::optional<Rgb> selected_;
    bool enabled_ = true;
    int copiedIndex_ = -1;
    bool laidOut_ = false;

    Theme theme_{};
    HBRUSH backgroundBrush_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT captionFont_ = nullptr;
    HFONT textFont_ = nullptr;
    HFONT valueFont_ = nullptr;
    HFONT iconFont_ = nullptr;

    RECT titleRect_{};
    RECT preview_{};
    std::vector<RECT> formatRows_;
    RECT recentCaption_{};
    RECT recentArea_{};
};

}  // namespace iro
