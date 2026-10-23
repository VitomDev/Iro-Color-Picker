#include "core/color.h"

#include <algorithm>
#include <cmath>

namespace iro {

namespace {

int Percent(double fraction) { return static_cast<int>(std::lround(fraction * 100)); }

// Hue shared by HSL and HSV, in degrees.
int Hue(double r, double g, double b, double max, double delta) {
    if (delta == 0) return 0;
    double hue;
    if (max == r) {
        hue = std::fmod((g - b) / delta, 6.0);
    } else if (max == g) {
        hue = (b - r) / delta + 2;
    } else {
        hue = (r - g) / delta + 4;
    }
    const int degrees = static_cast<int>(std::lround(hue * 60));
    return ((degrees % 360) + 360) % 360;
}

int HexDigit(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

double Linear(uint8_t channel) {
    const double c = channel / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

}  // namespace

Hsl ToHsl(const Rgb& color) {
    const double r = color.r / 255.0, g = color.g / 255.0, b = color.b / 255.0;
    const double max = std::max({r, g, b}), min = std::min({r, g, b});
    const double delta = max - min;
    const double l = (max + min) / 2;
    const double s = delta == 0 ? 0 : delta / (1 - std::abs(2 * l - 1));
    return {Hue(r, g, b, max, delta), Percent(s), Percent(l)};
}

Hsv ToHsv(const Rgb& color) {
    const double r = color.r / 255.0, g = color.g / 255.0, b = color.b / 255.0;
    const double max = std::max({r, g, b}), min = std::min({r, g, b});
    const double delta = max - min;
    return {Hue(r, g, b, max, delta), Percent(max == 0 ? 0 : delta / max), Percent(max)};
}

Cmyk ToCmyk(const Rgb& color) {
    const double r = color.r / 255.0, g = color.g / 255.0, b = color.b / 255.0;
    const double k = 1 - std::max({r, g, b});
    if (k >= 1) return {0, 0, 0, 100};
    return {Percent((1 - r - k) / (1 - k)), Percent((1 - g - k) / (1 - k)), Percent((1 - b - k) / (1 - k)), Percent(k)};
}

std::wstring Format(const Rgb& color, ColorFormat format) {
    wchar_t text[64];
    switch (format) {
        case ColorFormat::Hex:
            swprintf_s(text, L"#%02X%02X%02X", color.r, color.g, color.b);
            break;
        case ColorFormat::Rgb:
            swprintf_s(text, L"rgb(%u, %u, %u)", color.r, color.g, color.b);
            break;
        case ColorFormat::Hsl: {
            const Hsl hsl = ToHsl(color);
            swprintf_s(text, L"hsl(%d, %d%%, %d%%)", hsl.h, hsl.s, hsl.l);
            break;
        }
        case ColorFormat::Hsv: {
            const Hsv hsv = ToHsv(color);
            swprintf_s(text, L"hsv(%d, %d%%, %d%%)", hsv.h, hsv.s, hsv.v);
            break;
        }
        case ColorFormat::Cmyk: {
            const Cmyk cmyk = ToCmyk(color);
            swprintf_s(text, L"cmyk(%d%%, %d%%, %d%%, %d%%)", cmyk.c, cmyk.m, cmyk.y, cmyk.k);
            break;
        }
        case ColorFormat::Values:
            swprintf_s(text, L"%u, %u, %u", color.r, color.g, color.b);
            break;
    }
    return text;
}

std::wstring FormatName(ColorFormat format) {
    switch (format) {
        case ColorFormat::Hex: return L"HEX";
        case ColorFormat::Rgb: return L"RGB";
        case ColorFormat::Hsl: return L"HSL";
        case ColorFormat::Hsv: return L"HSV";
        case ColorFormat::Cmyk: return L"CMYK";
        case ColorFormat::Values: return L"R, G, B";
    }
    return {};
}

std::wstring FormatKey(ColorFormat format) {
    switch (format) {
        case ColorFormat::Hex: return L"hex";
        case ColorFormat::Rgb: return L"rgb";
        case ColorFormat::Hsl: return L"hsl";
        case ColorFormat::Hsv: return L"hsv";
        case ColorFormat::Cmyk: return L"cmyk";
        case ColorFormat::Values: return L"values";
    }
    return {};
}

std::optional<ColorFormat> FormatFromKey(std::wstring_view key) {
    for (ColorFormat format : kColorFormats) {
        if (FormatKey(format) == key) return format;
    }
    return std::nullopt;
}

std::optional<Rgb> ParseHex(std::wstring_view text) {
    if (!text.empty() && text.front() == L'#') text.remove_prefix(1);
    if (text.size() != 3 && text.size() != 6) return std::nullopt;
    int digits[6];
    for (size_t i = 0; i < text.size(); ++i) {
        digits[i] = HexDigit(text[i]);
        if (digits[i] < 0) return std::nullopt;
    }
    if (text.size() == 3) {
        // "#abc" is "#aabbcc".
        return Rgb{static_cast<uint8_t>(digits[0] * 17), static_cast<uint8_t>(digits[1] * 17), static_cast<uint8_t>(digits[2] * 17)};
    }
    return Rgb{static_cast<uint8_t>(digits[0] * 16 + digits[1]), static_cast<uint8_t>(digits[2] * 16 + digits[3]),
               static_cast<uint8_t>(digits[4] * 16 + digits[5])};
}

bool IsLight(const Rgb& color) {
    const double luminance = 0.2126 * Linear(color.r) + 0.7152 * Linear(color.g) + 0.0722 * Linear(color.b);
    // Black text has more contrast than white above this luminance.
    return luminance > 0.179;
}

}  // namespace iro
