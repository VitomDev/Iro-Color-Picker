#pragma once

#include "core/color.h"

#include <filesystem>
#include <vector>

namespace iro {

// The last picked colors, newest first, one "#RRGGBB" per line in a small
// file that stays on this PC. Picking a color again moves it to the front.
class RecentColors {
public:
    static constexpr size_t kMaxColors = 24;

    explicit RecentColors(std::filesystem::path path) : path_(std::move(path)) {}

    std::vector<Rgb> Load() const;
    bool Add(const Rgb& color);
    // Deletes the file: nothing is left on disk.
    bool Clear();

private:
    std::filesystem::path path_;
};

}  // namespace iro
