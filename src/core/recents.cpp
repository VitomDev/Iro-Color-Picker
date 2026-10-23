#include "core/recents.h"

#include "core/text.h"

#include <windows.h>

#include <algorithm>
#include <fstream>

namespace iro {

std::vector<Rgb> RecentColors::Load() const {
    std::vector<Rgb> colors;
    std::ifstream file(path_, std::ios::binary);
    std::string line;
    while (colors.size() < kMaxColors && std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto color = ParseHex(FromUtf8(line));
        if (color && std::find(colors.begin(), colors.end(), *color) == colors.end()) colors.push_back(*color);
    }
    return colors;
}

bool RecentColors::Add(const Rgb& color) {
    std::vector<Rgb> colors = Load();
    colors.erase(std::remove(colors.begin(), colors.end(), color), colors.end());
    colors.insert(colors.begin(), color);
    if (colors.size() > kMaxColors) colors.resize(kMaxColors);

    std::error_code error;
    std::filesystem::create_directories(path_.parent_path(), error);
    // Write a temporary file first so a crash never leaves half a list.
    std::filesystem::path temporary = path_;
    temporary += L".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        for (const Rgb& c : colors) file << ToUtf8(Format(c, ColorFormat::Hex)) << '\n';
        if (!file.good()) return false;
    }
    return MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
}

bool RecentColors::Clear() {
    std::error_code error;
    std::filesystem::remove(path_, error);
    return !error;
}

}  // namespace iro
