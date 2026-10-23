#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace iro {

struct Rgb {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    bool operator==(const Rgb&) const = default;
    COLORREF ToColorRef() const { return RGB(r, g, b); }
};

// Hue in degrees (0-359), the rest in percent (0-100), rounded the way
// design tools show them.
struct Hsl {
    int h, s, l;
};
struct Hsv {
    int h, s, v;
};
struct Cmyk {
    int c, m, y, k;
};

Hsl ToHsl(const Rgb& color);
Hsv ToHsv(const Rgb& color);
Cmyk ToCmyk(const Rgb& color);

// The ways a color can be copied. The order is the one shown in menus.
enum class ColorFormat { Hex, Rgb, Hsl, Hsv, Cmyk, Values };
inline constexpr std::array kColorFormats{ColorFormat::Hex, ColorFormat::Rgb, ColorFormat::Hsl,
                                          ColorFormat::Hsv, ColorFormat::Cmyk, ColorFormat::Values};

// "#1A2B3C", "rgb(26, 43, 60)", "hsl(210, 40%, 17%)", "hsv(210, 57%, 24%)",
// "cmyk(57%, 28%, 0%, 76%)" or "26, 43, 60".
std::wstring Format(const Rgb& color, ColorFormat format);
// "HEX", "RGB"... for menus and labels.
std::wstring FormatName(ColorFormat format);
// Stable names for the settings store; unknown names give nothing.
std::wstring FormatKey(ColorFormat format);
std::optional<ColorFormat> FormatFromKey(std::wstring_view key);

// Accepts "#1A2B3C", "1a2b3c" and the short "#abc".
std::optional<Rgb> ParseHex(std::wstring_view text);

// Whether dark text reads better than white on this color (WCAG luminance).
bool IsLight(const Rgb& color);

}  // namespace iro
