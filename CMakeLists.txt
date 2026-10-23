# Iro.exe, its core library and the tests. Use build.ps1, which also fetches
# the build tools and builds the installer.
cmake_minimum_required(VERSION 3.21)
project(Iro CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: no Visual C++ redistributable needed.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)
add_compile_options(/W4 /WX /permissive- /sdl /utf-8 /guard:cf /external:anglebrackets /external:W0)
add_link_options(/guard:cf /CETCOMPAT /OPT:REF /OPT:ICF)

add_library(irocore STATIC
    src/core/clipboard.cpp
    src/core/color.cpp
    src/core/geometry.cpp
    src/core/hotkey.cpp
    src/core/image.cpp
    src/core/paths.cpp
    src/core/recents.cpp
    src/core/settings.cpp
    src/core/text.cpp
)
target_include_directories(irocore PUBLIC src)
target_link_libraries(irocore PUBLIC user32 advapi32 shell32 ole32)

add_executable(Iro WIN32
    src/app/colors_window.cpp
    src/app/main.cpp
    src/app/picker_overlay.cpp
    src/app/screen_capture.cpp
    src/app/settings_dialog.cpp
    src/app/theme.cpp
    src/app/toast.cpp
    src/app/tray_app.cpp
    src/app/Iro.rc
)
target_link_libraries(Iro PRIVATE irocore gdi32 comctl32 dwmapi uxtheme shcore)
# The manifest is embedded through Iro.rc.
target_link_options(Iro PRIVATE /MANIFEST:NO)

add_executable(IroTests tests/tests.cpp src/app/screen_capture.cpp src/app/picker_overlay.cpp src/app/theme.cpp)
target_link_libraries(IroTests PRIVATE irocore gdi32 comctl32 dwmapi uxtheme shcore)
