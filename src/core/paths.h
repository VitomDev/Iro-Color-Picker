#pragma once

#include <filesystem>

namespace iro {

std::filesystem::path ExecutablePath();

// %LOCALAPPDATA%\Iro Color Picker: the recent colors file. Nothing else is
// stored on disk; settings live in the registry.
std::filesystem::path DataDirectory();
std::filesystem::path RecentColorsPath();

}  // namespace iro
