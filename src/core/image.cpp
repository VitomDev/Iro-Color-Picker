#include "core/image.h"

#include <algorithm>

namespace iro {

BgraImage Crop(const uint8_t* pixels, int width, int height, const RECT& rect) {
    const int left = std::clamp<int>(rect.left, 0, width);
    const int top = std::clamp<int>(rect.top, 0, height);
    const int right = std::clamp<int>(rect.right, left, width);
    const int bottom = std::clamp<int>(rect.bottom, top, height);

    BgraImage result;
    result.width = right - left;
    result.height = bottom - top;
    result.pixels.resize(static_cast<size_t>(result.width) * result.height * 4);
    for (int y = 0; y < result.height; ++y) {
        const uint8_t* source = pixels + (static_cast<size_t>(top + y) * width + left) * 4;
        std::copy_n(source, static_cast<size_t>(result.width) * 4,
                    &result.pixels[static_cast<size_t>(y) * result.width * 4]);
    }
    return result;
}

void Wipe(std::vector<uint8_t>& pixels) {
    SecureZeroMemory(pixels.data(), pixels.size());
    pixels.clear();
    pixels.shrink_to_fit();
}

}  // namespace iro
