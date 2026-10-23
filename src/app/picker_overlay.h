#pragma once

#include "app/screen_capture.h"
#include "core/color.h"

#include <windows.h>

#include <optional>

namespace iro {

struct PickResult {
    Rgb color;
    POINT screenPoint;  // Where the color was picked, in screen coordinates
};

// Shows the frozen screen over all monitors with a magnifier that follows
// the pointer, and returns the color of the pixel the user clicks. Arrow
// keys move one pixel (ten with Shift), the mouse wheel zooms, Enter or a
// click picks, Escape or a right click cancels. The magnifier label shows
// the color in `format`. Runs a modal message loop on the UI thread.
std::optional<PickResult> PickScreenColor(HINSTANCE instance, const ScreenSnapshot& snapshot, ColorFormat format);

// The color of a snapshot pixel; coordinates outside the snapshot are clamped.
Rgb PixelAt(const ScreenSnapshot& snapshot, POINT point);

}  // namespace iro
