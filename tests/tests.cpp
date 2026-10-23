// Iro's automated tests: a minimal runner without dependencies.
//
//   IroTests.exe <build dir>
//
// The build dir holds Iro.exe, used by the lifecycle test. Tests that need
// something unavailable report SKIP instead of passing.

#include "app/picker_overlay.h"
#include "app/screen_capture.h"
#include "core/clipboard.h"
#include "core/color.h"
#include "core/geometry.h"
#include "core/hotkey.h"
#include "core/image.h"
#include "core/recents.h"
#include "core/settings.h"
#include "core/text.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace iro;
namespace fs = std::filesystem;

namespace {

struct TestCase {
    const char* name;
    std::function<void()> run;
};

std::vector<TestCase>& Tests() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> run) { Tests().push_back({name, std::move(run)}); }
};

struct Failure {
    std::string message;
};
struct Skip {
    std::string reason;
};

#define TEST(name)                                     \
    static void name();                                \
    static Registrar name##_registrar(#name, name);    \
    static void name()

#define CHECK(expression)                                                                                  \
    do {                                                                                                   \
        if (!(expression)) throw Failure{std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #expression}; \
    } while (false)

#define SKIP(reason) throw Skip{reason}

fs::path buildDir;

constexpr wchar_t kTestSettingsKey[] = L"Software\\Iro Color Picker Tests";
constexpr wchar_t kTestRunValue[] = L"Iro Color Picker Test";

fs::path TemporaryFile(const wchar_t* name) {
    wchar_t folder[MAX_PATH];
    GetTempPathW(MAX_PATH, folder);
    return fs::path(folder) / L"IroTests" / name;
}

}  // namespace

// --- Geometry and capture coordinates -----------------------------------------

TEST(SelectionWorksInEveryDragDirection) {
    const RECT bounds{0, 0, 1000, 800};
    const RECT forward = SelectionRect({10, 20}, {110, 70}, bounds);
    const RECT backward = SelectionRect({110, 70}, {10, 20}, bounds);
    CHECK(EqualRect(&forward, &backward));
    CHECK(forward.left == 10 && forward.top == 20);
    // The end pixel is included: 10..110 is 101 pixels wide.
    CHECK(forward.right - forward.left == 101 && forward.bottom - forward.top == 51);
}

TEST(SelectionIsClampedToTheScreen) {
    const RECT bounds{0, 0, 1000, 800};
    const RECT rect = SelectionRect({-50, -50}, {2000, 2000}, bounds);
    CHECK(rect.left == 0 && rect.top == 0 && rect.right == 1000 && rect.bottom == 800);
}

TEST(TinyOrEmptySelectionsAreRejected) {
    const RECT bounds{0, 0, 1000, 800};
    CHECK(!IsUsableSelection(SelectionRect({5, 5}, {5, 5}, bounds)));
    CHECK(!IsUsableSelection(SelectionRect({5, 5}, {300, 6}, bounds)));
    CHECK(IsUsableSelection(SelectionRect({5, 5}, {40, 30}, bounds)));
}

TEST(NegativeVirtualScreenCoordinatesMapToTheSnapshot) {
    // A monitor left of and above the primary one: the virtual screen starts at (-1920, -400).
    const RECT virtualScreen{-1920, -400, 2560, 1440};
    const POINT primary = ScreenToSnapshot({0, 0}, virtualScreen);
    CHECK(primary.x == 1920 && primary.y == 400);
    const RECT onScreen = SnapshotToScreen(RECT{100, 50, 300, 120}, virtualScreen);
    CHECK(onScreen.left == -1820 && onScreen.top == -350 && onScreen.right == -1620 && onScreen.bottom == -280);
}

TEST(CropCopiesTheRightPixelsAndClamps) {
    BgraImage image;
    image.width = 4;
    image.height = 3;
    image.pixels.resize(4 * 3 * 4);
    for (int i = 0; i < 12; ++i) image.pixels[i * 4] = static_cast<uint8_t>(i);
    const BgraImage crop = Crop(image, RECT{1, 1, 3, 3});
    CHECK(crop.width == 2 && crop.height == 2);
    CHECK(crop.pixels[0] == 5 && crop.pixels[4] == 6 && crop.pixels[8] == 9 && crop.pixels[12] == 10);
    const BgraImage clamped = Crop(image, RECT{-5, -5, 50, 50});
    CHECK(clamped.width == 4 && clamped.height == 3);
}

// --- Shortcuts -------------------------------------------------------------------

TEST(HotkeysRoundTrip) {
    const Hotkey hotkey{MOD_CONTROL | MOD_ALT, 'C'};
    CHECK(Unpack(Pack(hotkey)) == hotkey);
    CHECK(FromHotkeyControl(ToHotkeyControl(hotkey)) == hotkey);
}

TEST(HotkeysNeedAModifier) {
    CHECK((Hotkey{MOD_CONTROL | MOD_ALT, 'C'}.IsValid()));
    CHECK(!(Hotkey{0, 'Q'}.IsValid()));
    CHECK(!(Hotkey{MOD_CONTROL, 0}.IsValid()));
}

TEST(HotkeysHaveReadableNames) {
    CHECK(ToString({MOD_CONTROL | MOD_ALT, 'C'}) == L"Ctrl + Alt + C");
    CHECK(ToString({MOD_ALT, VK_F2}) == L"Alt + F2");
}

TEST(HotkeyRegistrationAndConflicts) {
    // Registering the same shortcut twice must fail the second time, which is
    // how Iro detects a shortcut taken by another application.
    const Hotkey hotkey{MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F11};
    if (!RegisterHotKey(nullptr, 0x1B1, hotkey.modifiers, hotkey.key)) SKIP("test shortcut already in use");
    bool second = false;
    std::thread other([&] {
        second = RegisterHotKey(nullptr, 0x1B2, hotkey.modifiers, hotkey.key) != FALSE;
        if (second) UnregisterHotKey(nullptr, 0x1B2);
    });
    other.join();
    CHECK(!second);
    CHECK(UnregisterHotKey(nullptr, 0x1B1));
}

// --- Colors and formats ----------------------------------------------------------------

TEST(FormatsMatchDesignTools) {
    const Rgb color{26, 115, 232};  // A typical blue
    CHECK(Format(color, ColorFormat::Hex) == L"#1A73E8");
    CHECK(Format(color, ColorFormat::Rgb) == L"rgb(26, 115, 232)");
    CHECK(Format(color, ColorFormat::Hsl) == L"hsl(214, 82%, 51%)");
    CHECK(Format(color, ColorFormat::Hsv) == L"hsv(214, 89%, 91%)");
    CHECK(Format(color, ColorFormat::Cmyk) == L"cmyk(89%, 50%, 0%, 9%)");
    CHECK(Format(color, ColorFormat::Values) == L"26, 115, 232");
}

TEST(PrimaryAndGrayColorsConvertExactly) {
    CHECK(Format({255, 0, 0}, ColorFormat::Hsl) == L"hsl(0, 100%, 50%)");
    CHECK(Format({0, 255, 0}, ColorFormat::Hsv) == L"hsv(120, 100%, 100%)");
    CHECK(Format({0, 0, 255}, ColorFormat::Hsl) == L"hsl(240, 100%, 50%)");
    CHECK(Format({255, 0, 255}, ColorFormat::Hsl) == L"hsl(300, 100%, 50%)");
    // Grays have no hue or saturation.
    CHECK(Format({128, 128, 128}, ColorFormat::Hsl) == L"hsl(0, 0%, 50%)");
    CHECK(Format({0, 0, 0}, ColorFormat::Cmyk) == L"cmyk(0%, 0%, 0%, 100%)");
    CHECK(Format({255, 255, 255}, ColorFormat::Cmyk) == L"cmyk(0%, 0%, 0%, 0%)");
    CHECK(Format({255, 255, 255}, ColorFormat::Hex) == L"#FFFFFF");
    CHECK(Format({0, 0, 0}, ColorFormat::Hex) == L"#000000");
}

TEST(HuesNearRedStayInRange) {
    // Slightly blue red: the hue must wrap to 359, never -1 or 360.
    const Hsl hsl = ToHsl({255, 0, 4});
    CHECK(hsl.h >= 0 && hsl.h < 360);
    CHECK(hsl.h == 359);
}

TEST(HexIsParsedInEveryCommonForm) {
    CHECK(ParseHex(L"#1A73E8") == (Rgb{26, 115, 232}));
    CHECK(ParseHex(L"1a73e8") == (Rgb{26, 115, 232}));
    CHECK(ParseHex(L"#abc") == (Rgb{0xAA, 0xBB, 0xCC}));
    CHECK(!ParseHex(L"#12345"));
    CHECK(!ParseHex(L"#GGGGGG"));
    CHECK(!ParseHex(L""));
}

TEST(FormatKeysRoundTrip) {
    for (ColorFormat format : kColorFormats) {
        CHECK(FormatFromKey(FormatKey(format)) == format);
        CHECK(!FormatName(format).empty());
    }
    CHECK(!FormatFromKey(L"nonsense"));
}

TEST(TextColorReadsOnEverySwatch) {
    CHECK(IsLight({255, 255, 255}));
    CHECK(IsLight({255, 235, 59}));   // Yellow
    CHECK(!IsLight({0, 0, 0}));
    CHECK(!IsLight({0, 51, 153}));    // Navy
    CHECK(!IsLight({198, 40, 40}));   // Red
}

// --- Recent colors ----------------------------------------------------------------------

TEST(RecentColorsKeepNewestFirst) {
    RecentColors recents(TemporaryFile(L"recent.txt"));
    recents.Clear();
    CHECK(recents.Load().empty());
    CHECK(recents.Add({1, 2, 3}));
    CHECK(recents.Add({4, 5, 6}));
    CHECK(recents.Add({1, 2, 3}));  // Picked again: moves to the front, no duplicate.
    const auto colors = recents.Load();
    CHECK(colors.size() == 2 && colors[0] == (Rgb{1, 2, 3}) && colors[1] == (Rgb{4, 5, 6}));
    CHECK(recents.Clear());
    CHECK(!fs::exists(TemporaryFile(L"recent.txt")));
}

TEST(RecentColorsAreLimited) {
    RecentColors recents(TemporaryFile(L"recent-limit.txt"));
    recents.Clear();
    for (int i = 0; i < static_cast<int>(RecentColors::kMaxColors) + 10; ++i) recents.Add({static_cast<uint8_t>(i), 0, 0});
    const auto colors = recents.Load();
    CHECK(colors.size() == RecentColors::kMaxColors);
    CHECK(colors.front() == (Rgb{static_cast<uint8_t>(RecentColors::kMaxColors + 9), 0, 0}));
    recents.Clear();
}

TEST(RecentColorsIgnoreDamagedLines) {
    const fs::path file = TemporaryFile(L"recent-damaged.txt");
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << "#112233\r\ngarbage\n\n#112233\n#ABCDEF\n";
    const auto colors = RecentColors(file).Load();
    CHECK(colors.size() == 2 && colors[0] == (Rgb{0x11, 0x22, 0x33}) && colors[1] == (Rgb{0xAB, 0xCD, 0xEF}));
    RecentColors(file).Clear();
}

// --- Screen ---------------------------------------------------------------------------------

TEST(PicksTheExactColorShownOnScreen) {
    // A window filled with one color, read back from a screen capture the
    // way the picker does.
    const Rgb color{26, 115, 232};
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hbrBackground = CreateSolidBrush(color.ToColorRef());
    wc.lpszClassName = L"IroTestSwatch";
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP | WS_VISIBLE, 120, 120, 80, 80,
                                  nullptr, nullptr, wc.hInstance, nullptr);
    CHECK(window);
    for (int i = 0; i < 10; ++i) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
        Sleep(30);
    }
    RECT bounds;
    GetWindowRect(window, &bounds);
    auto snapshot = ScreenSnapshot::Capture();
    DestroyWindow(window);
    DeleteObject(wc.hbrBackground);
    CHECK(snapshot);
    const POINT center = ScreenToSnapshot({(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2}, snapshot->bounds());
    const Rgb picked = PixelAt(*snapshot, center);
    if (!(picked == color)) SKIP("the test window was not visible (locked or remote session)");
    // Coordinates past the edges are clamped instead of reading outside the image.
    PixelAt(*snapshot, {-50, -50});
    PixelAt(*snapshot, {snapshot->width() + 50, snapshot->height() + 50});
}

// --- Clipboard and text ---------------------------------------------------------------------

TEST(ClipboardStoresUnicodeText) {
    const auto previous = GetClipboardText(nullptr);
    CHECK(SetClipboardText(nullptr, L"#1A73E8"));
    CHECK(GetClipboardText(nullptr) == L"#1A73E8");
    if (previous) SetClipboardText(nullptr, *previous);
}

TEST(TextHelpers) {
    CHECK(Trim(L"  #abc \n") == L"#abc");
    CHECK(FromUtf8(ToUtf8(L"ñ\U0001F600")) == L"ñ\U0001F600");
}

// --- Settings and Start with Windows ---------------------------------------------------

TEST(SettingsPersist) {
    RegDeleteTreeW(HKEY_CURRENT_USER, kTestSettingsKey);
    Settings settings = Settings::Load(kTestSettingsKey);
    CHECK(settings.pickHotkey == (Hotkey{MOD_CONTROL | MOD_ALT, 'C'}));
    CHECK(settings.copyFormat == ColorFormat::Hex);
    CHECK(settings.keepRecent);

    settings.pickHotkey = {MOD_CONTROL | MOD_SHIFT, 'P'};
    settings.copyFormat = ColorFormat::Hsl;
    settings.keepRecent = false;
    CHECK(settings.Save(kTestSettingsKey));
    const Settings loaded = Settings::Load(kTestSettingsKey);
    CHECK(loaded.pickHotkey == settings.pickHotkey && loaded.copyFormat == ColorFormat::Hsl && !loaded.keepRecent);
    RegDeleteTreeW(HKEY_CURRENT_USER, kTestSettingsKey);
}

TEST(InvalidStoredSettingsFallBackToDefaults) {
    HKEY key;
    CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, kTestSettingsKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS);
    const DWORD noModifier = Pack({0, 'C'});
    RegSetValueExW(key, L"PickHotkey", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&noModifier), sizeof(noModifier));
    const wchar_t format[] = L"pantone";
    RegSetValueExW(key, L"CopyFormat", 0, REG_SZ, reinterpret_cast<const BYTE*>(format), sizeof(format));
    RegCloseKey(key);
    const Settings loaded = Settings::Load(kTestSettingsKey);
    CHECK(loaded.pickHotkey == (Hotkey{MOD_CONTROL | MOD_ALT, 'C'}));
    CHECK(loaded.copyFormat == ColorFormat::Hex);
    RegDeleteTreeW(HKEY_CURRENT_USER, kTestSettingsKey);
}

TEST(FirstRunIsReportedOnce) {
    RegDeleteTreeW(HKEY_CURRENT_USER, kTestSettingsKey);
    CHECK(ConsumeFirstRun(kTestSettingsKey));
    CHECK(!ConsumeFirstRun(kTestSettingsKey));
    CHECK(Settings{}.Save(kTestSettingsKey));
    CHECK(!ConsumeFirstRun(kTestSettingsKey));
    RegDeleteTreeW(HKEY_CURRENT_USER, kTestSettingsKey);
}

TEST(AutostartCanBeEnabledDisabledAndReenabled) {
    const std::wstring exe = L"C:\\Program Files\\Iro Test\\Iro.exe";
    SetAutostartEnabled(false, exe, kTestRunValue);
    CHECK(!IsAutostartEnabled(kTestRunValue));
    CHECK(SetAutostartEnabled(true, exe, kTestRunValue));
    CHECK(IsAutostartEnabled(kTestRunValue));
    CHECK(SetAutostartEnabled(true, exe, kTestRunValue));
    wchar_t value[512]{};
    DWORD size = sizeof(value);
    CHECK(RegGetValueW(HKEY_CURRENT_USER, kRunKey, kTestRunValue, RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS);
    CHECK(std::wstring(value) == L"\"C:\\Program Files\\Iro Test\\Iro.exe\" --background");
    CHECK(SetAutostartEnabled(false, exe, kTestRunValue));
    CHECK(!IsAutostartEnabled(kTestRunValue));
}

TEST(AutostartRespectsTaskManagerDisable) {
    const std::wstring exe = L"C:\\Iro\\Iro.exe";
    CHECK(SetAutostartEnabled(true, exe, kTestRunValue));
    // Task Manager marks a disabled startup entry with an odd first byte.
    const BYTE disabled[12]{3};
    RegSetKeyValueW(HKEY_CURRENT_USER, kStartupApprovedKey, kTestRunValue, REG_BINARY, disabled, sizeof(disabled));
    CHECK(!IsAutostartEnabled(kTestRunValue));
    CHECK(SetAutostartEnabled(true, exe, kTestRunValue));
    CHECK(IsAutostartEnabled(kTestRunValue));
    CHECK(SetAutostartEnabled(false, exe, kTestRunValue));
}

// --- Application lifecycle ----------------------------------------------------------------------

namespace {

HWND FindIroWindow() { return FindWindowW(L"IroColorPickerTray", nullptr); }

PROCESS_INFORMATION StartIro() {
    std::wstring command = L"\"" + (buildDir / L"Iro.exe").wstring() + L"\" --background";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process);
    if (process.hThread) CloseHandle(process.hThread);
    return process;
}

HWND WaitForIroWindow() {
    for (int i = 0; i < 100; ++i) {
        if (HWND window = FindIroWindow()) return window;
        Sleep(50);
    }
    return nullptr;
}

}  // namespace

TEST(SingleInstanceAndCleanShutdown) {
    if (!fs::exists(buildDir / L"Iro.exe")) SKIP("Iro.exe not built");
    if (FindIroWindow()) SKIP("Iro is already running on this machine");

    // Keep the user's first-run state: mark it done for this run, restore it after.
    const bool firstRunPending = ConsumeFirstRun();
    struct RestoreFirstRun {
        bool pending;
        ~RestoreFirstRun() {
            if (pending) RegDeleteKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"FirstRunDone");
        }
    } restoreFirstRun{firstRunPending};

    // Hold Iro's default shortcut so it starts with a conflict: it must keep running.
    const bool holdingShortcut = RegisterHotKey(nullptr, 0x1B3, MOD_CONTROL | MOD_ALT, 'C') != FALSE;

    PROCESS_INFORMATION first = StartIro();
    CHECK(first.hProcess != nullptr);
    HWND window = WaitForIroWindow();
    CHECK(window != nullptr);

    // A second launch exits at once and leaves the first one running.
    PROCESS_INFORMATION second = StartIro();
    CHECK(second.hProcess != nullptr);
    CHECK(WaitForSingleObject(second.hProcess, 5000) == WAIT_OBJECT_0);
    DWORD secondExit = 1;
    GetExitCodeProcess(second.hProcess, &secondExit);
    CloseHandle(second.hProcess);
    CHECK(secondExit == 0);
    CHECK(WaitForSingleObject(first.hProcess, 0) == WAIT_TIMEOUT);
    if (holdingShortcut) UnregisterHotKey(nullptr, 0x1B3);

    // WM_CLOSE is what the installer sends; Iro must exit cleanly.
    PostMessageW(window, WM_CLOSE, 0, 0);
    CHECK(WaitForSingleObject(first.hProcess, 5000) == WAIT_OBJECT_0);
    DWORD firstExit = 1;
    GetExitCodeProcess(first.hProcess, &firstExit);
    CloseHandle(first.hProcess);
    CHECK(firstExit == 0);
    CHECK(FindIroWindow() == nullptr);
}

// --- Runner -----------------------------------------------------------------------------------------

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    // Capture coordinates are physical pixels, as in Iro.exe.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    buildDir = argc > 1 ? fs::path(argv[1]) : fs::path(argv[0]).parent_path();

    int passed = 0, failed = 0, skipped = 0;
    for (const auto& test : Tests()) {
        try {
            test.run();
            ++passed;
            std::cout << "PASS " << test.name << "\n";
        } catch (const Skip& skip) {
            ++skipped;
            std::cout << "SKIP " << test.name << ": " << skip.reason << "\n";
        } catch (const Failure& failure) {
            ++failed;
            std::cout << "FAIL " << test.name << ": " << failure.message << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "FAIL " << test.name << ": exception: " << e.what() << "\n";
        }
    }
    std::error_code error;
    fs::remove_all(TemporaryFile(L"").parent_path(), error);
    std::cout << "\n" << passed << " passed, " << failed << " failed, " << skipped << " skipped\n";
    return failed == 0 ? 0 : 1;
}
