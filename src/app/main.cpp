#include "app/tray_app.h"

#include <commctrl.h>

#include <cwchar>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    // "--background" is used by the Start with Windows entry: start silently.
    const bool startedAtSignIn = wcsstr(commandLine, L"--background") != nullptr;

    // One instance per user session. A second launch only tells the running
    // one, which explains where Iro is.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\IroColorPicker.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND running = FindWindowW(iro::kTrayWindowClass, nullptr); running && !startedAtSignIn) {
            PostMessageW(running, iro::WM_APP_ALREADY_RUNNING, 0, 0);
        }
        return 0;
    }

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    int exitCode = 0;
    {
        iro::TrayApp app;
        if (app.Initialize(instance, startedAtSignIn)) {
            MSG message;
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                if (app.PreTranslate(message)) continue;
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        } else {
            exitCode = 1;
        }
    }
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return exitCode;
}
