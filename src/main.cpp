#include "app.h"

#include <commctrl.h>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int command_show)
{
    if (const HMODULE user32 = GetModuleHandleW(L"user32.dll"))
    {
        using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
        const auto set_process_dpi_awareness_context =
            reinterpret_cast<SetProcessDpiAwarenessContextFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (set_process_dpi_awareness_context != nullptr)
        {
            set_process_dpi_awareness_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
        else
        {
            SetProcessDPIAware();
        }
    }

    INITCOMMONCONTROLSEX common_controls{};
    common_controls.dwSize = sizeof(common_controls);
    common_controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_DATE_CLASSES;
    InitCommonControlsEx(&common_controls);

    TaskApp app(instance);
    if (!app.Initialize(command_show))
    {
        return 1;
    }

    return app.Run();
}
