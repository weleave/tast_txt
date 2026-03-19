#include "app.h"

#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>
#include <charconv>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

using Gdiplus::REAL;

namespace
{
constexpr int kInputId = 1001;
constexpr int kPriorityId = 1002;
constexpr int kAddButtonId = 1003;
constexpr int kDeleteButtonId = 1004;
constexpr int kListId = 1005;
constexpr int kTopButtonId = 1006;
constexpr int kStarDownButtonId = 1008;
constexpr int kStarUpButtonId = 1009;
constexpr int kAutoLaunchButtonId = 1010;
constexpr int kBackgroundButtonId = 1011;
constexpr int kReminderButtonId = 1012;
constexpr int kTaskListButtonId = 1013;
constexpr int kClearCompletedButtonId = 1014;
constexpr int kCompletedListId = 1015;
constexpr UINT_PTR kUiTimerId = 1;
constexpr int kMinWidth = 1280;
constexpr int kMinHeight = 760;
constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"TaskQueueDesktop";
constexpr wchar_t kExportHeader[] = L"TASK_QUEUE_EXPORT_V1";
constexpr UINT kTaskListMenuExportId = 40001;
constexpr UINT kTaskListMenuImportId = 40002;
constexpr UINT kReminderMenuSetId = 40003;
constexpr UINT kReminderMenuClearId = 40004;
constexpr int kReminderDialogDateId = 5001;
constexpr int kReminderDialogTimeId = 5002;
constexpr int kReminderDialogLeadId = 5003;
constexpr int kReminderDialogSaveId = 5004;
constexpr int kReminderDialogClearId = 5005;
constexpr wchar_t kReminderDialogClassName[] = L"TaskQueueReminderDialog";

struct ButtonPalette
{
    Gdiplus::Color top;
    Gdiplus::Color bottom;
    Gdiplus::Color border;
    Gdiplus::Color text;
};

Gdiplus::Color MakeColor(BYTE alpha, BYTE red, BYTE green, BYTE blue)
{
    return Gdiplus::Color(alpha, red, green, blue);
}

void AddRoundedRectangle(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, REAL radius)
{
    const REAL diameter = radius * 2.0F;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0F, 90.0F);
    path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270.0F, 90.0F);
    path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter, diameter, diameter, 0.0F, 90.0F);
    path.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90.0F, 90.0F);
    path.CloseFigure();
}

std::wstring TruncateText(std::wstring_view text, std::size_t max_length)
{
    if (text.size() <= max_length)
    {
        return std::wstring(text);
    }

    return std::wstring(text.substr(0, max_length)) + L"...";
}

bool ParseInteger(std::string_view text, int& value)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{};
}

int ReminderLeadMinutesFromDialogIndex(int combo_index)
{
    switch (combo_index)
    {
        case 1:
            return 5;
        case 2:
            return 10;
        case 3:
            return 15;
        case 4:
            return 30;
        default:
            return 0;
    }
}

int ReminderDialogIndexFromMinutes(int minutes)
{
    switch (minutes)
    {
        case 5:
            return 1;
        case 10:
            return 2;
        case 15:
            return 3;
        case 30:
            return 4;
        default:
            return 0;
    }
}

struct ReminderDialogState
{
    HFONT font{};
    HWND date_picker{};
    HWND time_picker{};
    HWND lead_combo{};
    SYSTEMTIME reminder_time{};
    int lead_minutes{};
    bool allow_clear{};
    bool accepted{};
    bool clear_requested{};
};

LRESULT CALLBACK ReminderDialogProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    auto* state = reinterpret_cast<ReminderDialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message)
    {
        case WM_CREATE:
        {
            const auto* create_struct = reinterpret_cast<const CREATESTRUCTW*>(l_param);
            state = static_cast<ReminderDialogState*>(create_struct->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

            const int padding = 18;
            const int label_width = 82;
            const int control_width = 252;
            const int button_width = 96;
            const int control_height = 34;
            const int row_gap = 14;
            const int row1_y = 22;
            const int row2_y = row1_y + control_height + row_gap;
            const int row3_y = row2_y + control_height + row_gap;
            const int button_y = row3_y + 48;

            const auto apply_font = [state](HWND control) {
                if (control != nullptr && state->font != nullptr)
                {
                    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
                }
            };

            HWND label = CreateWindowExW(
                0,
                L"STATIC",
                L"\u65e5\u671f",
                WS_CHILD | WS_VISIBLE,
                padding,
                row1_y + 5,
                label_width,
                24,
                window,
                nullptr,
                nullptr,
                nullptr);
            apply_font(label);

            state->date_picker = CreateWindowExW(
                0,
                DATETIMEPICK_CLASSW,
                L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATECENTURYFORMAT,
                padding + label_width,
                row1_y,
                control_width,
                control_height,
                window,
                reinterpret_cast<HMENU>(kReminderDialogDateId),
                nullptr,
                nullptr);
            apply_font(state->date_picker);
            SendMessageW(
                state->date_picker, DTM_SETFORMATW, 0, reinterpret_cast<LPARAM>(L"yyyy-MM-dd"));
            SendMessageW(
                state->date_picker, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(&state->reminder_time));

            label = CreateWindowExW(
                0,
                L"STATIC",
                L"\u65f6\u95f4",
                WS_CHILD | WS_VISIBLE,
                padding,
                row2_y + 5,
                label_width,
                24,
                window,
                nullptr,
                nullptr,
                nullptr);
            apply_font(label);

            state->time_picker = CreateWindowExW(
                0,
                DATETIMEPICK_CLASSW,
                L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_TIMEFORMAT | DTS_UPDOWN,
                padding + label_width,
                row2_y,
                control_width,
                control_height,
                window,
                reinterpret_cast<HMENU>(kReminderDialogTimeId),
                nullptr,
                nullptr);
            apply_font(state->time_picker);
            SendMessageW(state->time_picker, DTM_SETFORMATW, 0, reinterpret_cast<LPARAM>(L"HH:mm"));
            SendMessageW(
                state->time_picker, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(&state->reminder_time));

            label = CreateWindowExW(
                0,
                L"STATIC",
                L"\u63d0\u524d",
                WS_CHILD | WS_VISIBLE,
                padding,
                row3_y + 5,
                label_width,
                24,
                window,
                nullptr,
                nullptr,
                nullptr);
            apply_font(label);

            state->lead_combo = CreateWindowExW(
                0,
                WC_COMBOBOXW,
                L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                padding + label_width,
                row3_y,
                control_width,
                220,
                window,
                reinterpret_cast<HMENU>(kReminderDialogLeadId),
                nullptr,
                nullptr);
            apply_font(state->lead_combo);

            const wchar_t* options[] = {
                L"\u51c6\u65f6",
                L"\u63d0\u524d 5 \u5206\u949f",
                L"\u63d0\u524d 10 \u5206\u949f",
                L"\u63d0\u524d 15 \u5206\u949f",
                L"\u63d0\u524d 30 \u5206\u949f"};
            for (const auto* option : options)
            {
                SendMessageW(state->lead_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option));
            }
            SendMessageW(
                state->lead_combo, CB_SETCURSEL, ReminderDialogIndexFromMinutes(state->lead_minutes), 0);

            HWND save_button = CreateWindowExW(
                0,
                L"BUTTON",
                L"\u4fdd\u5b58",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                28,
                button_y,
                button_width,
                34,
                window,
                reinterpret_cast<HMENU>(kReminderDialogSaveId),
                nullptr,
                nullptr);
            apply_font(save_button);

            HWND clear_button = CreateWindowExW(
                0,
                L"BUTTON",
                L"\u6e05\u9664",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                148,
                button_y,
                button_width,
                34,
                window,
                reinterpret_cast<HMENU>(kReminderDialogClearId),
                nullptr,
                nullptr);
            apply_font(clear_button);
            EnableWindow(clear_button, state->allow_clear ? TRUE : FALSE);

            HWND cancel_button = CreateWindowExW(
                0,
                L"BUTTON",
                L"\u53d6\u6d88",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                268,
                button_y,
                button_width,
                34,
                window,
                reinterpret_cast<HMENU>(IDCANCEL),
                nullptr,
                nullptr);
            apply_font(cancel_button);
            return 0;
        }
        case WM_COMMAND:
            if (state == nullptr)
            {
                break;
            }

            if (HIWORD(w_param) == BN_CLICKED)
            {
                const int control_id = LOWORD(w_param);
                if (control_id == kReminderDialogSaveId)
                {
                    SYSTEMTIME date_value{};
                    SYSTEMTIME time_value{};
                    if (SendMessageW(
                            state->date_picker, DTM_GETSYSTEMTIME, 0, reinterpret_cast<LPARAM>(&date_value)) !=
                            GDT_VALID ||
                        SendMessageW(
                            state->time_picker, DTM_GETSYSTEMTIME, 0, reinterpret_cast<LPARAM>(&time_value)) !=
                            GDT_VALID)
                    {
                        MessageBoxW(
                            window, L"\u8bf7\u5148\u9009\u62e9\u63d0\u9192\u7684\u65e5\u671f\u548c\u65f6\u95f4\u3002",
                            L"\u63d0\u9192", MB_OK | MB_ICONWARNING);
                        return 0;
                    }

                    date_value.wHour = time_value.wHour;
                    date_value.wMinute = time_value.wMinute;
                    date_value.wSecond = 0;
                    date_value.wMilliseconds = 0;
                    state->reminder_time = date_value;
                    state->lead_minutes = ReminderLeadMinutesFromDialogIndex(
                        static_cast<int>(SendMessageW(state->lead_combo, CB_GETCURSEL, 0, 0)));
                    state->accepted = true;
                    state->clear_requested = false;
                    DestroyWindow(window);
                    return 0;
                }
                if (control_id == kReminderDialogClearId)
                {
                    state->lead_minutes = ReminderLeadMinutesFromDialogIndex(
                        static_cast<int>(SendMessageW(state->lead_combo, CB_GETCURSEL, 0, 0)));
                    state->accepted = true;
                    state->clear_requested = true;
                    DestroyWindow(window);
                    return 0;
                }
                if (control_id == IDCANCEL)
                {
                    DestroyWindow(window);
                    return 0;
                }
            }
            break;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        default:
            break;
    }

    return DefWindowProcW(window, message, w_param, l_param);
}

bool ShowReminderDialog(
    HWND owner,
    HINSTANCE instance,
    HFONT font,
    SYSTEMTIME& reminder_time,
    int& lead_minutes,
    bool allow_clear,
    bool& clear_requested)
{
    static bool class_registered = false;
    if (!class_registered)
    {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.hInstance = instance;
        window_class.lpfnWndProc = ReminderDialogProc;
        window_class.lpszClassName = kReminderDialogClassName;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (RegisterClassExW(&window_class) == 0)
        {
            return false;
        }
        class_registered = true;
    }

    ReminderDialogState state{};
    state.font = font;
    state.reminder_time = reminder_time;
    state.lead_minutes = lead_minutes;
    state.allow_clear = allow_clear;

    RECT owner_rect{};
    GetWindowRect(owner, &owner_rect);
    const int dialog_width = 420;
    const int dialog_height = 250;
    const int x = owner_rect.left + ((owner_rect.right - owner_rect.left) - dialog_width) / 2;
    const int y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - dialog_height) / 2;

    HWND dialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        kReminderDialogClassName,
        L"\u63d0\u9192\u8bbe\u7f6e",
        WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_VISIBLE,
        x,
        y,
        dialog_width,
        dialog_height,
        owner,
        nullptr,
        instance,
        &state);
    if (dialog == nullptr)
    {
        return false;
    }

    EnableWindow(owner, FALSE);
    SetActiveWindow(dialog);
    SetForegroundWindow(dialog);

    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (!IsDialogMessageW(dialog, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    SetForegroundWindow(owner);

    if (!state.accepted)
    {
        return false;
    }

    reminder_time = state.reminder_time;
    lead_minutes = state.lead_minutes;
    clear_requested = state.clear_requested;
    return true;
}
}  // namespace

constexpr wchar_t TaskApp::kWindowClassName[];

TaskApp::TaskApp(HINSTANCE instance) : instance_(instance)
{
    Gdiplus::GdiplusStartupInput startup_input;
    if (Gdiplus::GdiplusStartup(&gdiplus_token_, &startup_input, nullptr) != Gdiplus::Ok)
    {
        gdiplus_token_ = 0;
    }
}

TaskApp::~TaskApp()
{
    background_image_.reset();

    if (edit_brush_ != nullptr)
    {
        DeleteObject(edit_brush_);
    }
    if (small_font_ != nullptr)
    {
        DeleteObject(small_font_);
    }
    if (font_ != nullptr)
    {
        DeleteObject(font_);
    }
    if (gdiplus_token_ != 0)
    {
        Gdiplus::GdiplusShutdown(gdiplus_token_);
    }
}

bool TaskApp::Initialize(int command_show)
{
    LoadSettings();

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance_;
    window_class.lpfnWndProc = WindowProc;
    window_class.lpszClassName = kWindowClassName;
    window_class.hIcon = reinterpret_cast<HICON>(
        LoadImageW(instance_, MAKEINTRESOURCEW(1), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    window_class.hIconSm = reinterpret_cast<HICON>(
        LoadImageW(instance_, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = nullptr;
    window_class.style = CS_HREDRAW | CS_VREDRAW;

    if (RegisterClassExW(&window_class) == 0)
    {
        return false;
    }

    window_ = CreateWindowExW(
        0,
        kWindowClassName,
        L"\u4efb\u52a1\u961f\u5217",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        startup_bounds_.left,
        startup_bounds_.top,
        startup_bounds_.right,
        startup_bounds_.bottom,
        nullptr,
        nullptr,
        instance_,
        this);

    if (window_ == nullptr)
    {
        return false;
    }

    ShowWindow(window_, command_show);
    UpdateWindow(window_);
    return true;
}

int TaskApp::Run()
{
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK TaskApp::WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    if (message == WM_NCCREATE)
    {
        const auto* create_struct = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        auto* app = static_cast<TaskApp*>(create_struct->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->window_ = window;
    }

    auto* app = reinterpret_cast<TaskApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (app != nullptr)
    {
        return app->HandleMessage(window, message, w_param, l_param);
    }

    return DefWindowProcW(window, message, w_param, l_param);
}

LRESULT CALLBACK TaskApp::InputSubclassProc(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param,
    UINT_PTR,
    DWORD_PTR reference_data)
{
    auto* app = reinterpret_cast<TaskApp*>(reference_data);

    if (message == WM_KEYDOWN && w_param == VK_RETURN && app != nullptr)
    {
        app->AddTaskFromInput();
        return 0;
    }

    return DefSubclassProc(window, message, w_param, l_param);
}

LRESULT CALLBACK TaskApp::HoverSubclassProc(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param,
    UINT_PTR,
    DWORD_PTR reference_data)
{
    auto* app = reinterpret_cast<TaskApp*>(reference_data);
    if (app == nullptr)
    {
        return DefSubclassProc(window, message, w_param, l_param);
    }

    switch (message)
    {
        case WM_MOUSEMOVE:
            app->TrackButtonHover(window);
            break;
        case WM_MOUSELEAVE:
            if (app->hovered_control_ == window)
            {
                app->hovered_control_ = nullptr;
                InvalidateRect(window, nullptr, TRUE);
            }
            break;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
            InvalidateRect(window, nullptr, TRUE);
            break;
        default:
            break;
    }

    return DefSubclassProc(window, message, w_param, l_param);
}

LRESULT CALLBACK TaskApp::ListSubclassProc(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param,
    UINT_PTR,
    DWORD_PTR reference_data)
{
    auto* app = reinterpret_cast<TaskApp*>(reference_data);
    if (app == nullptr)
    {
        return DefSubclassProc(window, message, w_param, l_param);
    }

    switch (message)
    {
        case WM_MOUSEMOVE:
            app->TrackListHover();
            break;
        case WM_MOUSELEAVE:
            if (app->hot_item_index_ >= 0)
            {
                const int old_index = app->hot_item_index_;
                app->hot_item_index_ = -1;
                app->RedrawTaskRow(old_index);
            }
            break;
        default:
            break;
    }

    return DefSubclassProc(window, message, w_param, l_param);
}

LRESULT TaskApp::HandleMessage(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    switch (message)
    {
        case WM_CREATE:
        {
            font_ = CreateFontW(
                -22,
                0,
                0,
                0,
                FW_NORMAL,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE,
                L"Microsoft YaHei UI");

            small_font_ = CreateFontW(
                -18,
                0,
                0,
                0,
                FW_SEMIBOLD,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE,
                L"Microsoft YaHei UI");

            edit_brush_ = CreateSolidBrush(RGB(255, 255, 255));

            CreateChildControls();
            LoadTasks();
            LoadCompletedTasks();
            RefreshTaskList();
            RefreshCompletedList();
            LayoutControls();
            UpdateClock();
            SyncReminderEditor();
            SetTimer(window_, kUiTimerId, 1000, nullptr);
            SetFocus(input_);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            HDC device_context = BeginPaint(window, &paint);
            PaintWindow(device_context);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_SIZE:
            LayoutControls();
            InvalidateRect(window, nullptr, TRUE);
            return 0;
        case WM_TIMER:
            if (w_param == kUiTimerId)
            {
                UpdateClock();
                CheckReminders();
                return 0;
            }
            break;
        case WM_DRAWITEM:
        {
            const auto* draw_item = reinterpret_cast<const DRAWITEMSTRUCT*>(l_param);
            if (draw_item != nullptr)
            {
                DrawButton(draw_item);
                return TRUE;
            }
            break;
        }
        case WM_CTLCOLORSTATIC:
        {
            HDC device_context = reinterpret_cast<HDC>(w_param);
            SetBkMode(device_context, TRANSPARENT);
            SetTextColor(device_context, RGB(46, 60, 84));
            return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        {
            HDC device_context = reinterpret_cast<HDC>(w_param);
            SetBkColor(device_context, RGB(255, 255, 255));
            SetTextColor(device_context, RGB(30, 41, 59));
            return reinterpret_cast<LRESULT>(edit_brush_);
        }
        case WM_COMMAND:
        {
            const int control_id = LOWORD(w_param);
            const int notification_code = HIWORD(w_param);

            if (notification_code == BN_CLICKED)
            {
                switch (control_id)
                {
                    case kAddButtonId:
                        AddTaskFromInput();
                        return 0;
                    case kDeleteButtonId:
                        DeleteSelectedTask();
                        return 0;
                    case kStarDownButtonId:
                        ChangeSelectedPriority(-1);
                        return 0;
                    case kStarUpButtonId:
                        ChangeSelectedPriority(1);
                        return 0;
                    case kTopButtonId:
                        ToggleSortMode();
                        return 0;
                    case kAutoLaunchButtonId:
                        ToggleAutoLaunch();
                        return 0;
                    case kBackgroundButtonId:
                        ChooseBackgroundImage();
                        return 0;
                    case kClearCompletedButtonId:
                        ClearCompletedTasks();
                        return 0;
                    case kReminderButtonId:
                        ShowReminderMenu();
                        return 0;
                    case kTaskListButtonId:
                        ShowTaskListMenu();
                        return 0;
                    default:
                        break;
                }
            }

            return 0;
        }
        case WM_NOTIFY:
        {
            const auto* header = reinterpret_cast<const NMHDR*>(l_param);
            if (header != nullptr && header->idFrom == kListId)
            {
                if (header->code == NM_CUSTOMDRAW)
                {
                    return HandleListCustomDraw(l_param);
                }
                if (header->code == NM_DBLCLK)
                {
                    DeleteSelectedTask();
                    return 0;
                }
                if (header->code == NM_CLICK)
                {
                    const auto* activate = reinterpret_cast<const NMITEMACTIVATE*>(l_param);
                    if (activate->iItem >= 0 && activate->iSubItem == 0)
                    {
                        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
                        {
                            ChangeSelectedPriority(-1);
                        }
                        else
                        {
                            CycleSelectedPriority();
                        }
                    }
                    return 0;
                }
                if (header->code == LVN_KEYDOWN)
                {
                    const auto* key_down = reinterpret_cast<const NMLVKEYDOWN*>(l_param);
                    switch (key_down->wVKey)
                    {
                        case VK_DELETE:
                            DeleteSelectedTask();
                            break;
                        case VK_ADD:
                            ChangeSelectedPriority(1);
                            break;
                        case VK_SUBTRACT:
                            ChangeSelectedPriority(-1);
                            break;
                        default:
                            break;
                    }
                    return 0;
                }
                if (header->code == LVN_ITEMCHANGED)
                {
                    SyncReminderEditor();
                    UpdateSummary();
                    return 0;
                }
            }

            break;
        }
        case WM_GETMINMAXINFO:
        {
            auto* minmax = reinterpret_cast<MINMAXINFO*>(l_param);
            minmax->ptMinTrackSize.x = kMinWidth;
            minmax->ptMinTrackSize.y = kMinHeight;
            return 0;
        }
        case WM_CLOSE:
            KillTimer(window_, kUiTimerId);
            SaveTasks();
            SaveCompletedTasks();
            SaveSettings();
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }

    return DefWindowProcW(window, message, w_param, l_param);
}

void TaskApp::CreateChildControls()
{
    input_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(kInputId),
        instance_,
        nullptr);

    priority_ = CreateWindowExW(
        0,
        WC_COMBOBOXW,
        L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(kPriorityId),
        instance_,
        nullptr);

    reminder_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"\u63d0\u9192",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(kReminderButtonId),
        instance_,
        nullptr);

    task_list_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"\u4efb\u52a1\u6e05\u5355",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(kTaskListButtonId),
        instance_,
        nullptr);

    add_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"添加任务",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(kAddButtonId),
        instance_,
        nullptr);

    delete_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"完成任务",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(kDeleteButtonId),
        instance_,
        nullptr);

    star_down_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"降星",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(kStarDownButtonId),
        instance_,
        nullptr);

    star_up_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"升星",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(kStarUpButtonId),
        instance_,
        nullptr);

    top_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"窗口置顶",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(kTopButtonId),
        instance_,
        nullptr);

    auto_launch_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"开机启动",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(kAutoLaunchButtonId),
        instance_,
        nullptr);

    background_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"设置背景",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(kBackgroundButtonId),
        instance_,
        nullptr);

    clear_completed_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"\u6e05\u7a7a\u5df2\u5b8c\u6210",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(kClearCompletedButtonId),
        instance_,
        nullptr);

    list_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWW,
        L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(kListId),
        instance_,
        nullptr);

    completed_list_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWW,
        L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(kCompletedListId),
        instance_,
        nullptr);

    ApplyFont(input_, font_);
    ApplyFont(priority_, font_);
    ApplyFont(reminder_button_, small_font_);
    ApplyFont(task_list_button_, small_font_);
    ApplyFont(add_button_, small_font_);
    ApplyFont(delete_button_, small_font_);
    ApplyFont(star_down_button_, small_font_);
    ApplyFont(star_up_button_, small_font_);
    ApplyFont(top_button_, small_font_);
    ApplyFont(auto_launch_button_, small_font_);
    ApplyFont(background_button_, small_font_);
    ApplyFont(clear_completed_button_, small_font_);
    ApplyFont(list_, font_);
    ApplyFont(completed_list_, font_);

    SendMessageW(input_, EM_LIMITTEXT, 400, 0);
    SendMessageW(
        input_,
        EM_SETCUEBANNER,
        TRUE,
        reinterpret_cast<LPARAM>(L"\u5728\u8fd9\u91cc\u8f93\u5165\u6700\u65b0\u4efb\u52a1\uff0c\u6309 Enter \u76f4\u63a5\u6dfb\u52a0"));
    SetWindowSubclass(input_, InputSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    const wchar_t* options[] = {L"0 \u661f", L"1 \u661f", L"2 \u661f", L"3 \u661f"};
    for (const auto* option : options)
    {
        SendMessageW(priority_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option));
    }
    SendMessageW(priority_, CB_SETCURSEL, 0, 0);

    SetWindowSubclass(add_button_, HoverSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(delete_button_, HoverSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(star_down_button_, HoverSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(star_up_button_, HoverSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(reminder_button_, HoverSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(task_list_button_, HoverSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(top_button_, HoverSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(auto_launch_button_, HoverSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(background_button_, HoverSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(clear_completed_button_, HoverSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(list_, ListSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));

    ConfigureListView();

    LVCOLUMNW column_update{};
    column_update.mask = LVCF_TEXT | LVCF_WIDTH;
    column_update.pszText = const_cast<LPWSTR>(L"\u661f\u7ea7");
    column_update.cx = 100;
    ListView_SetColumn(list_, 0, &column_update);
    column_update.pszText = const_cast<LPWSTR>(L"\u4efb\u52a1");
    column_update.cx = 560;
    ListView_SetColumn(list_, 1, &column_update);
    column_update.pszText = const_cast<LPWSTR>(L"\u6dfb\u52a0\u65f6\u95f4");
    column_update.cx = 180;
    ListView_SetColumn(list_, 2, &column_update);
    column_update.pszText = const_cast<LPWSTR>(L"\u63d0\u9192");
    column_update.cx = 188;
    ListView_SetColumn(list_, 3, &column_update);

    ListView_SetExtendedListViewStyle(
        completed_list_,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
    ListView_SetBkColor(completed_list_, RGB(252, 254, 255));
    ListView_SetTextBkColor(completed_list_, RGB(252, 254, 255));
    ListView_SetTextColor(completed_list_, RGB(30, 41, 59));

    LVCOLUMNW completed_column{};
    completed_column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    completed_column.pszText = const_cast<LPWSTR>(L"\u4efb\u52a1\u5185\u5bb9");
    completed_column.cx = 760;
    completed_column.iSubItem = 0;
    ListView_InsertColumn(completed_list_, 0, &completed_column);
    completed_column.pszText = const_cast<LPWSTR>(L"\u5b8c\u6210\u65f6\u95f4");
    completed_column.cx = 220;
    completed_column.iSubItem = 1;
    ListView_InsertColumn(completed_list_, 1, &completed_column);

    UpdateButtonLabels();
}

void TaskApp::ConfigureListView() const
{
    ListView_SetExtendedListViewStyle(
        list_,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
    ListView_SetBkColor(list_, RGB(252, 254, 255));
    ListView_SetTextBkColor(list_, RGB(252, 254, 255));
    ListView_SetTextColor(list_, RGB(30, 41, 59));

    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    column.pszText = const_cast<LPWSTR>(L"星级");
    column.cx = 90;
    column.iSubItem = 0;
    ListView_InsertColumn(list_, 0, &column);

    column.pszText = const_cast<LPWSTR>(L"任务");
    column.cx = 550;
    column.iSubItem = 1;
    ListView_InsertColumn(list_, 1, &column);

    column.pszText = const_cast<LPWSTR>(L"添加时间");
    column.cx = 170;
    column.iSubItem = 2;
    ListView_InsertColumn(list_, 2, &column);
    column.pszText = const_cast<LPWSTR>(L"Reminder");
    column.cx = 170;
    column.iSubItem = 3;
    ListView_InsertColumn(list_, 3, &column);
}

void TaskApp::ApplyFont(HWND control, HFONT font) const
{
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void TaskApp::LayoutControls()
{
    if (window_ == nullptr || input_ == nullptr)
    {
        return;
    }

    RECT client_rect{};
    GetClientRect(window_, &client_rect);

    const int width = client_rect.right - client_rect.left;
    const int height = client_rect.bottom - client_rect.top;
    const int padding = 20;
    const int gap = 12;
    const int header_height = 28;
    const int row_height = 44;
    const int priority_width = 108;
    const int add_width = 138;

    int y = header_height;
    const int input_width = std::max(220, width - (padding * 2) - priority_width - add_width - (gap * 2));

    MoveWindow(input_, padding, y, input_width, row_height, TRUE);
    MoveWindow(priority_, padding + input_width + gap, y, priority_width, 220, TRUE);
    MoveWindow(add_button_, padding + input_width + gap + priority_width + gap, y, add_width, row_height, TRUE);

    y += row_height + 16;

    const int task_list_width = 132;
    const int reminder_width = 110;
    const int star_width = 86;
    const int delete_width = 128;
    const int top_width = 96;
    const int auto_width = 126;
    const int background_width = 102;
    const int total_button_width =
        task_list_width + gap + reminder_width + gap + star_width + gap + star_width + gap + delete_width + gap +
        top_width + gap + auto_width + gap + background_width;
    const int summary_width = std::max(260, width - (padding * 2) - total_button_width - gap);
    int x = padding;

    summary_bounds_.left = x;
    summary_bounds_.top = y;
    summary_bounds_.right = x + summary_width;
    summary_bounds_.bottom = y + row_height;
    x += summary_width + gap;

    MoveWindow(task_list_button_, x, y, task_list_width, row_height, TRUE);
    x += task_list_width + gap;
    MoveWindow(reminder_button_, x, y, reminder_width, row_height, TRUE);
    x += reminder_width + gap;
    MoveWindow(star_down_button_, x, y, star_width, row_height, TRUE);
    x += star_width + gap;
    MoveWindow(star_up_button_, x, y, star_width, row_height, TRUE);
    x += star_width + gap;
    MoveWindow(delete_button_, x, y, delete_width, row_height, TRUE);
    x += delete_width + gap;
    MoveWindow(top_button_, x, y, top_width, row_height, TRUE);
    x += top_width + gap;
    MoveWindow(auto_launch_button_, x, y, auto_width, row_height, TRUE);
    x += auto_width + gap;
    MoveWindow(background_button_, x, y, background_width, row_height, TRUE);

    y += row_height + 14;
    const int content_height = std::max(360, height - y - padding);
    const int completed_header_height = 38;
    const int completed_gap = 10;
    const int completed_list_height = std::max(140, (content_height * 33) / 100);
    const int main_list_height = std::max(
        180,
        content_height - completed_header_height - completed_gap - completed_list_height);

    MoveWindow(list_, padding, y, width - (padding * 2), main_list_height, TRUE);

    y += main_list_height + completed_gap;
    completed_header_bounds_.left = padding;
    completed_header_bounds_.top = y;
    completed_header_bounds_.right = width - padding;
    completed_header_bounds_.bottom = y + completed_header_height;

    const int clear_completed_width = 154;
    MoveWindow(
        clear_completed_button_,
        width - padding - clear_completed_width,
        y,
        clear_completed_width,
        row_height - 4,
        TRUE);

    y += completed_header_height + 8;
    MoveWindow(completed_list_, padding, y, width - (padding * 2), completed_list_height, TRUE);

    const int total_list_width = std::max(320, width - (padding * 2));
    const int priority_column = 100;
    const int added_column = 180;
    const int reminder_column = 188;
    const int task_column = std::max(180, total_list_width - priority_column - added_column - reminder_column - 10);
    ListView_SetColumnWidth(list_, 0, priority_column);
    ListView_SetColumnWidth(list_, 1, task_column);
    ListView_SetColumnWidth(list_, 2, added_column);
    ListView_SetColumnWidth(list_, 3, reminder_column);

    const int completed_total_width = std::max(320, width - (padding * 2));
    const int completed_time_width = 210;
    const int completed_text_width = std::max(180, completed_total_width - completed_time_width - 8);
    ListView_SetColumnWidth(completed_list_, 0, completed_text_width);
    ListView_SetColumnWidth(completed_list_, 1, completed_time_width);
}

void TaskApp::AddTaskFromInput()
{
    const std::wstring text = ReadInputText();
    if (text.empty())
    {
        MessageBeep(MB_ICONWARNING);
        SetFocus(input_);
        return;
    }

    tasks_.push_back(TaskItem{
        .id = next_id_++,
        .priority = ReadPriority(),
        .text = text,
        .created_at = CurrentTimestamp(),
        .reminder_at = L"",
        .reminder_state = 0});

    SetWindowTextW(input_, L"");
    SendMessageW(priority_, CB_SETCURSEL, 0, 0);

    ApplyTaskOrder();
    RefreshTaskList();
    SaveTasks();
    SetFocus(input_);
}

void TaskApp::DeleteSelectedTask()
{
    const int index = SelectedTaskIndex();
    if (index < 0 || index >= static_cast<int>(tasks_.size()))
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    completed_tasks_.push_back(CompletedItem{
        .text = tasks_[index].text,
        .completed_at = CurrentTimestamp()});
    tasks_.erase(tasks_.begin() + index);
    hot_item_index_ = -1;
    RefreshTaskList();
    RefreshCompletedList();
    SyncReminderEditor();
    SaveTasks();
    SaveCompletedTasks();

    if (!tasks_.empty())
    {
        const int next_selection = std::min(index, static_cast<int>(tasks_.size()) - 1);
        ListView_SetItemState(list_, next_selection, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    else
    {
        SyncReminderEditor();
    }
}

void TaskApp::ChangeSelectedPriority(int delta)
{
    const int index = SelectedTaskIndex();
    if (index < 0 || index >= static_cast<int>(tasks_.size()))
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    const int task_id = tasks_[index].id;
    const int original = tasks_[index].priority;
    tasks_[index].priority = std::clamp(tasks_[index].priority + delta, 0, 3);
    if (tasks_[index].priority == original)
    {
        return;
    }

    ApplyTaskOrder();
    RefreshTaskList();
    SelectTaskById(task_id);
    SaveTasks();
    UpdateSummary();
}

void TaskApp::CycleSelectedPriority()
{
    const int index = SelectedTaskIndex();
    if (index < 0 || index >= static_cast<int>(tasks_.size()))
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    const int task_id = tasks_[index].id;
    tasks_[index].priority = (tasks_[index].priority + 1) % 4;
    ApplyTaskOrder();
    RefreshTaskList();
    SelectTaskById(task_id);
    SaveTasks();
    UpdateSummary();
}

void TaskApp::ToggleSortMode()
{
    const int selected_index = SelectedTaskIndex();
    const int selected_task_id =
        selected_index >= 0 && selected_index < static_cast<int>(tasks_.size()) ? tasks_[selected_index].id : -1;
    sort_by_priority_ = !sort_by_priority_;
    ApplyTaskOrder();
    RefreshTaskList();
    SelectTaskById(selected_task_id);
    UpdateButtonLabels();
    SaveSettings();
}

void TaskApp::ToggleAutoLaunch()
{
    SetAutoLaunchEnabled(!auto_launch_);
    UpdateButtonLabels();
}

void TaskApp::ChooseBackgroundImage()
{
    if (!background_path_.empty())
    {
        const int action = MessageBoxW(
            window_,
            L"当前已经设置背景。\n\n选择“是”更换图片，选择“否”清除背景，选择“取消”保持不变。",
            L"背景设置",
            MB_YESNOCANCEL | MB_ICONQUESTION);

        if (action == IDCANCEL)
        {
            return;
        }
        if (action == IDNO)
        {
            background_path_.clear();
            background_image_.reset();
            UpdateButtonLabels();
            SaveSettings();
            InvalidateRect(window_, nullptr, TRUE);
            return;
        }
    }

    wchar_t file_path[MAX_PATH]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = L"Image Files\0*.png;*.jpg;*.jpeg;*.bmp\0All Files\0*.*\0";
    dialog.lpstrFile = file_path;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    dialog.lpstrTitle = L"选择背景图片";

    if (!GetOpenFileNameW(&dialog))
    {
        return;
    }

    background_path_ = file_path;
    LoadBackgroundImage();
    UpdateButtonLabels();
    SaveSettings();
    InvalidateRect(window_, nullptr, TRUE);
}

void TaskApp::ExportTaskList()
{
    wchar_t file_path[MAX_PATH]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = L"Text Files\0*.txt\0All Files\0*.*\0";
    dialog.lpstrFile = file_path;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    dialog.lpstrDefExt = L"txt";
    dialog.lpstrTitle = L"Export Task List";

    if (!GetSaveFileNameW(&dialog))
    {
        return;
    }

    std::ofstream output(std::filesystem::path(file_path), std::ios::binary | std::ios::trunc);
    if (!output)
    {
        MessageBoxW(window_, L"Failed to export task list.", L"Export", MB_OK | MB_ICONERROR);
        return;
    }

    output << WideToUtf8(kExportHeader) << '\n';
    for (const TaskItem& task : tasks_)
    {
        output << WideToUtf8(BuildTaskLine(task, false)) << '\n';
    }

    MessageBoxW(window_, L"Task list exported.", L"Export", MB_OK | MB_ICONINFORMATION);
}

void TaskApp::ImportTaskList()
{
    wchar_t file_path[MAX_PATH]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = L"Text Files\0*.txt\0All Files\0*.*\0";
    dialog.lpstrFile = file_path;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    dialog.lpstrTitle = L"Import Task List";

    if (!GetOpenFileNameW(&dialog))
    {
        return;
    }

    std::ifstream input(std::filesystem::path(file_path), std::ios::binary);
    if (!input)
    {
        MessageBoxW(window_, L"Failed to open task list.", L"Import", MB_OK | MB_ICONERROR);
        return;
    }

    int imported_count = 0;
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.empty())
        {
            continue;
        }
        if (Utf8ToWide(line) == kExportHeader)
        {
            continue;
        }

        TaskItem task;
        if (!ParseTaskLine(line, task, false) && !ParseTaskLine(line, task, true))
        {
            continue;
        }

        task.id = next_id_++;
        task.reminder_state = 0;
        tasks_.push_back(std::move(task));
        ++imported_count;
    }

    if (imported_count == 0)
    {
        MessageBoxW(window_, L"No tasks were imported.", L"Import", MB_OK | MB_ICONWARNING);
        return;
    }

    ApplyTaskOrder();
    RefreshTaskList();
    SyncReminderEditor();
    SaveTasks();
    MessageBoxW(window_, (L"Imported " + std::to_wstring(imported_count) + L" tasks.").c_str(), L"Import",
        MB_OK | MB_ICONINFORMATION);
}

void TaskApp::ShowTaskListMenu()
{
    if (task_list_button_ == nullptr)
    {
        return;
    }

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr)
    {
        return;
    }

    AppendMenuW(menu, MF_STRING, kTaskListMenuExportId, L"\u5bfc\u51fa TXT");
    AppendMenuW(menu, MF_STRING, kTaskListMenuImportId, L"\u5bfc\u5165 TXT");

    RECT button_rect{};
    GetWindowRect(task_list_button_, &button_rect);
    SetForegroundWindow(window_);
    const UINT command = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
        button_rect.left,
        button_rect.bottom + 4,
        0,
        window_,
        nullptr);
    DestroyMenu(menu);
    PostMessageW(window_, WM_NULL, 0, 0);

    if (command == kTaskListMenuExportId)
    {
        ExportTaskList();
    }
    else if (command == kTaskListMenuImportId)
    {
        ImportTaskList();
    }
}

void TaskApp::ShowReminderMenu()
{
    if (reminder_button_ == nullptr)
    {
        return;
    }

    const int index = SelectedTaskIndex();
    if (index < 0 || index >= static_cast<int>(tasks_.size()))
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr)
    {
        return;
    }

    AppendMenuW(menu, MF_STRING, kReminderMenuSetId, L"\u8bbe\u7f6e\u63d0\u9192...");
    AppendMenuW(
        menu,
        tasks_[index].reminder_at.empty() ? MF_STRING | MF_GRAYED : MF_STRING,
        kReminderMenuClearId,
        L"\u6e05\u9664\u63d0\u9192");

    RECT button_rect{};
    GetWindowRect(reminder_button_, &button_rect);
    SetForegroundWindow(window_);
    const UINT command = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
        button_rect.left,
        button_rect.bottom + 4,
        0,
        window_,
        nullptr);
    DestroyMenu(menu);
    PostMessageW(window_, WM_NULL, 0, 0);

    if (command == kReminderMenuSetId)
    {
        SetSelectedTaskReminder();
    }
    else if (command == kReminderMenuClearId)
    {
        ClearSelectedTaskReminder();
    }
}

void TaskApp::SetSelectedTaskReminder()
{
    const int index = SelectedTaskIndex();
    if (index < 0 || index >= static_cast<int>(tasks_.size()))
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    SYSTEMTIME reminder_time{};
    if (!tasks_[index].reminder_at.empty() && !ParseSystemTimeString(tasks_[index].reminder_at, reminder_time))
    {
        GetLocalTime(&reminder_time);
    }
    else if (tasks_[index].reminder_at.empty())
    {
        GetLocalTime(&reminder_time);
    }

    bool clear_requested = false;
    int lead_minutes = reminder_lead_minutes_;
    if (!ShowReminderDialog(
            window_,
            instance_,
            small_font_,
            reminder_time,
            lead_minutes,
            !tasks_[index].reminder_at.empty(),
            clear_requested))
    {
        return;
    }

    reminder_lead_minutes_ = lead_minutes;
    SaveSettings();

    if (clear_requested)
    {
        ClearSelectedTaskReminder();
        return;
    }

    tasks_[index].reminder_at = FormatSystemTime(reminder_time, false);
    tasks_[index].reminder_state = 0;

    std::wstring reminder_text = FormatReminder(tasks_[index].reminder_at);
    ListView_SetItemText(list_, index, 3, reminder_text.data());
    SaveTasks();
    SyncReminderEditor();
    UpdateSummary();
}

void TaskApp::ClearSelectedTaskReminder()
{
    const int index = SelectedTaskIndex();
    if (index < 0 || index >= static_cast<int>(tasks_.size()))
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    tasks_[index].reminder_at.clear();
    tasks_[index].reminder_state = 0;
    std::wstring empty_text;
    ListView_SetItemText(list_, index, 3, empty_text.data());
    SaveTasks();
    SyncReminderEditor();
    UpdateSummary();
}

void TaskApp::ClearCompletedTasks()
{
    if (completed_tasks_.empty())
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    const int result = MessageBoxW(
        window_,
        L"\u786e\u5b9a\u8981\u6e05\u7a7a\u5df2\u5b8c\u6210\u5217\u8868\u5417\uff1f",
        L"\u6e05\u7a7a\u5df2\u5b8c\u6210",
        MB_YESNO | MB_ICONQUESTION);
    if (result != IDYES)
    {
        return;
    }

    completed_tasks_.clear();
    RefreshCompletedList();
    SaveCompletedTasks();
}

void TaskApp::UpdateClock() const
{
    if (summary_bounds_.right > summary_bounds_.left && summary_bounds_.bottom > summary_bounds_.top)
    {
        RECT clock_rect = summary_bounds_;
        clock_rect.left = std::max(clock_rect.left, clock_rect.right - 220);
        InvalidateRect(window_, &clock_rect, FALSE);
    }
}

void TaskApp::SyncReminderEditor()
{
    const int index = SelectedTaskIndex();
    EnableWindow(reminder_button_, index >= 0 && index < static_cast<int>(tasks_.size()) ? TRUE : FALSE);
    UpdateSummary();
}

void TaskApp::CheckReminders()
{
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const ULONGLONG now_ticks = ToFileTimeTicks(now);
    const ULONGLONG lead_ticks = static_cast<ULONGLONG>(std::max(0, reminder_lead_minutes_)) * 60ULL * 10000000ULL;

    std::wstring upcoming;
    std::wstring due;
    bool changed = false;

    for (TaskItem& task : tasks_)
    {
        if (task.reminder_at.empty())
        {
            continue;
        }

        SYSTEMTIME reminder_time{};
        if (!ParseSystemTimeString(task.reminder_at, reminder_time))
        {
            continue;
        }

        const ULONGLONG reminder_ticks = ToFileTimeTicks(reminder_time);
        if (now_ticks >= reminder_ticks)
        {
            if (task.reminder_state < 2)
            {
                if (!due.empty())
                {
                    due += L"\n";
                }
                due += L"- " + task.text + L" (" + task.reminder_at + L")";
                task.reminder_state = 2;
                changed = true;
            }
            continue;
        }

        if (task.reminder_state == 0 && lead_ticks > 0 && now_ticks + 10000000ULL >= reminder_ticks - lead_ticks)
        {
            if (!upcoming.empty())
            {
                upcoming += L"\n";
            }
            upcoming += L"- " + task.text + L" (" + task.reminder_at + L")";
            task.reminder_state = 1;
            changed = true;
        }
    }

    if (changed)
    {
        SaveTasks();
    }

    if (!upcoming.empty() || !due.empty())
    {
        std::wstring message;
        if (!upcoming.empty())
        {
            message += L"Coming soon:\n" + upcoming;
        }
        if (!due.empty())
        {
            if (!message.empty())
            {
                message += L"\n\n";
            }
            message += L"Due now:\n" + due;
        }

        FlashWindow(window_, TRUE);
        MessageBoxW(window_, message.c_str(), L"Task Reminder", MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
        RefreshTaskList();
        SyncReminderEditor();
    }
}

void TaskApp::ApplyTaskOrder()
{
    if (sort_by_priority_)
    {
        std::stable_sort(tasks_.begin(), tasks_.end(), [](const TaskItem& left, const TaskItem& right) {
            if (left.priority != right.priority)
            {
                return left.priority > right.priority;
            }
            return left.id < right.id;
        });
        return;
    }

    std::sort(tasks_.begin(), tasks_.end(), [](const TaskItem& left, const TaskItem& right) {
        return left.id < right.id;
    });
}

void TaskApp::SelectTaskById(int task_id) const
{
    if (task_id < 0 || list_ == nullptr)
    {
        return;
    }

    for (int index = 0; index < static_cast<int>(tasks_.size()); ++index)
    {
        if (tasks_[index].id != task_id)
        {
            continue;
        }

        ListView_SetItemState(list_, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list_, index, FALSE);
        return;
    }
}

void TaskApp::RefreshTaskList() const
{
    ListView_DeleteAllItems(list_);

    for (int index = 0; index < static_cast<int>(tasks_.size()); ++index)
    {
        const TaskItem& task = tasks_[index];

        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = index;
        std::wstring priority_text = FormatPriority(task.priority);
        item.pszText = priority_text.data();
        ListView_InsertItem(list_, &item);

        ListView_SetItemText(list_, index, 1, const_cast<LPWSTR>(task.text.c_str()));
        ListView_SetItemText(list_, index, 2, const_cast<LPWSTR>(task.created_at.c_str()));
        std::wstring reminder_text = FormatReminder(task.reminder_at);
        ListView_SetItemText(list_, index, 3, reminder_text.data());
    }

    UpdateSummary();
}

void TaskApp::UpdateSummary() const
{
    const int selected_index = SelectedTaskIndex();
    const BOOL has_selection = selected_index >= 0 ? TRUE : FALSE;
    EnableWindow(delete_button_, has_selection);
    EnableWindow(star_down_button_, has_selection);
    EnableWindow(star_up_button_, has_selection);
    EnableWindow(reminder_button_, has_selection);
    if (summary_bounds_.right > summary_bounds_.left && summary_bounds_.bottom > summary_bounds_.top)
    {
        InvalidateRect(window_, &summary_bounds_, FALSE);
    }
}

std::wstring TaskApp::BuildSummaryText() const
{
    std::wstring summary = L"\u5f85\u529e " + std::to_wstring(tasks_.size()) + L" \u9879";
    const int selected_index = SelectedTaskIndex();
    if (selected_index >= 0 && selected_index < static_cast<int>(tasks_.size()))
    {
        const TaskItem& task = tasks_[selected_index];
        summary += L"  |  " + FormatPriority(task.priority);
        summary += L"  |  " + TruncateText(task.text, 26);
        if (!task.reminder_at.empty())
        {
            summary += L"  |  \u63d0\u9192 " + FormatReminder(task.reminder_at);
        }
    }

    return summary;
}

void TaskApp::RefreshCompletedList() const
{
    ListView_DeleteAllItems(completed_list_);

    for (int index = 0; index < static_cast<int>(completed_tasks_.size()); ++index)
    {
        const CompletedItem& item = completed_tasks_[index];

        LVITEMW row{};
        row.mask = LVIF_TEXT;
        row.iItem = index;
        row.pszText = const_cast<LPWSTR>(item.text.c_str());
        ListView_InsertItem(completed_list_, &row);
        ListView_SetItemText(completed_list_, index, 1, const_cast<LPWSTR>(item.completed_at.c_str()));
    }

    EnableWindow(clear_completed_button_, completed_tasks_.empty() ? FALSE : TRUE);
    if (completed_header_bounds_.right > completed_header_bounds_.left &&
        completed_header_bounds_.bottom > completed_header_bounds_.top)
    {
        InvalidateRect(window_, &completed_header_bounds_, FALSE);
    }
}

std::wstring TaskApp::BuildCompletedSummaryText() const
{
    return L"\u5df2\u5b8c\u6210 " + std::to_wstring(completed_tasks_.size()) + L" \u9879";
}

void TaskApp::UpdateButtonLabels() const
{
    SetWindowTextW(top_button_, sort_by_priority_ ? L"\u6392\u5e8f \u5f00" : L"\u6392\u5e8f");
    SetWindowTextW(auto_launch_button_, auto_launch_ ? L"\u5f00\u673a\u542f\u52a8 \u5f00" : L"\u5f00\u673a\u542f\u52a8");
    SetWindowTextW(background_button_, background_path_.empty() ? L"\u80cc\u666f" : L"\u80cc\u666f\u5df2\u5f00");
    SetWindowTextW(task_list_button_, L"\u4efb\u52a1\u6e05\u5355 \u25be");
    SetWindowTextW(reminder_button_, L"\u63d0\u9192 \u25be");
    SetWindowTextW(add_button_, L"\u6dfb\u52a0\u4efb\u52a1");
    SetWindowTextW(delete_button_, L"\u5b8c\u6210\u4efb\u52a1");
    SetWindowTextW(star_down_button_, L"\u964d\u661f");
    SetWindowTextW(star_up_button_, L"\u5347\u661f");
    SetWindowTextW(clear_completed_button_, L"\u6e05\u7a7a\u5df2\u5b8c\u6210");
    return;
    SetWindowTextW(top_button_, sort_by_priority_ ? L"排序 开" : L"排序");
    SetWindowTextW(auto_launch_button_, auto_launch_ ? L"开机启动: 开" : L"开机启动");
    SetWindowTextW(background_button_, background_path_.empty() ? L"设置背景" : L"背景已开");
}

void TaskApp::RedrawTaskRow(int index) const
{
    if (index >= 0 && index < static_cast<int>(tasks_.size()))
    {
        ListView_RedrawItems(list_, index, index);
        UpdateWindow(list_);
    }
}

void TaskApp::TrackButtonHover(HWND control)
{
    if (hovered_control_ != control)
    {
        HWND previous = hovered_control_;
        hovered_control_ = control;
        if (previous != nullptr)
        {
            InvalidateRect(previous, nullptr, TRUE);
        }
        InvalidateRect(control, nullptr, TRUE);
    }

    TRACKMOUSEEVENT event{};
    event.cbSize = sizeof(event);
    event.dwFlags = TME_LEAVE;
    event.hwndTrack = control;
    TrackMouseEvent(&event);
}

void TaskApp::TrackListHover()
{
    if (list_ == nullptr)
    {
        return;
    }

    POINT point{};
    GetCursorPos(&point);
    ScreenToClient(list_, &point);

    LVHITTESTINFO hit_test{};
    hit_test.pt = point;
    ListView_SubItemHitTest(list_, &hit_test);

    const int new_index = (hit_test.flags & LVHT_ONITEM) != 0 ? hit_test.iItem : -1;
    if (new_index != hot_item_index_)
    {
        const int old_index = hot_item_index_;
        hot_item_index_ = new_index;
        RedrawTaskRow(old_index);
        RedrawTaskRow(hot_item_index_);
    }

    TRACKMOUSEEVENT event{};
    event.cbSize = sizeof(event);
    event.dwFlags = TME_LEAVE;
    event.hwndTrack = list_;
    TrackMouseEvent(&event);
}

void TaskApp::PaintWindow(HDC device_context) const
{
    RECT client_rect{};
    GetClientRect(window_, &client_rect);

    const int width = client_rect.right - client_rect.left;
    const int height = client_rect.bottom - client_rect.top;

    Gdiplus::Graphics graphics(device_context);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);

    const Gdiplus::RectF full_rect(0.0F, 0.0F, static_cast<REAL>(width), static_cast<REAL>(height));

    Gdiplus::LinearGradientBrush background_brush(
        Gdiplus::PointF(0.0F, 0.0F),
        Gdiplus::PointF(static_cast<REAL>(width), static_cast<REAL>(height)),
        MakeColor(255, 248, 250, 252),
        MakeColor(255, 228, 236, 246));
    graphics.FillRectangle(&background_brush, full_rect);

    if (background_image_ != nullptr)
    {
        const REAL image_width = static_cast<REAL>(background_image_->GetWidth());
        const REAL image_height = static_cast<REAL>(background_image_->GetHeight());
        if (image_width > 0.0F && image_height > 0.0F)
        {
            const REAL scale = std::max(
                static_cast<REAL>(width) / image_width,
                static_cast<REAL>(height) / image_height);
            const REAL draw_width = image_width * scale;
            const REAL draw_height = image_height * scale;
            const REAL x = (static_cast<REAL>(width) - draw_width) / 2.0F;
            const REAL y = (static_cast<REAL>(height) - draw_height) / 2.0F;
            graphics.DrawImage(background_image_.get(), x, y, draw_width, draw_height);
            Gdiplus::SolidBrush overlay(MakeColor(170, 248, 251, 255));
            graphics.FillRectangle(&overlay, full_rect);
        }
    }

    Gdiplus::SolidBrush orb_left(MakeColor(36, 66, 133, 244));
    graphics.FillEllipse(&orb_left, -80.0F, -90.0F, 280.0F, 220.0F);
    Gdiplus::SolidBrush orb_right(MakeColor(32, 244, 158, 66));
    graphics.FillEllipse(&orb_right, static_cast<REAL>(width - 240), -60.0F, 280.0F, 180.0F);

    if (summary_bounds_.right > summary_bounds_.left && summary_bounds_.bottom > summary_bounds_.top)
    {
        Gdiplus::RectF summary_panel(
            static_cast<REAL>(summary_bounds_.left),
            static_cast<REAL>(summary_bounds_.top),
            static_cast<REAL>(summary_bounds_.right - summary_bounds_.left),
            static_cast<REAL>(summary_bounds_.bottom - summary_bounds_.top));
        Gdiplus::GraphicsPath summary_path;
        AddRoundedRectangle(summary_path, summary_panel, 16.0F);

        Gdiplus::SolidBrush summary_fill(MakeColor(255, 255, 255, 255));
        graphics.FillPath(&summary_fill, &summary_path);

        Gdiplus::Pen summary_border(MakeColor(255, 221, 229, 240), 1.0F);
        graphics.DrawPath(&summary_border, &summary_path);

        Gdiplus::RectF summary_text_rect(
            summary_panel.X + 18.0F,
            summary_panel.Y + 2.0F,
            std::max<REAL>(60.0F, summary_panel.Width - 230.0F),
            summary_panel.Height - 4.0F);
        Gdiplus::RectF clock_rect(
            summary_panel.GetRight() - 200.0F,
            summary_panel.Y + 2.0F,
            182.0F,
            summary_panel.Height - 4.0F);

        Gdiplus::SolidBrush summary_text_brush(MakeColor(255, 47, 61, 84));
        Gdiplus::SolidBrush clock_text_brush(MakeColor(255, 82, 102, 132));
        Gdiplus::Font text_font(device_context, small_font_);
        Gdiplus::StringFormat summary_format;
        summary_format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        summary_format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        summary_format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

        Gdiplus::StringFormat clock_format;
        clock_format.SetAlignment(Gdiplus::StringAlignmentFar);
        clock_format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        clock_format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

        const std::wstring summary_text = BuildSummaryText();
        const std::wstring clock_text = CurrentClockTimestamp();
        graphics.DrawString(summary_text.c_str(), -1, &text_font, summary_text_rect, &summary_format, &summary_text_brush);
        graphics.DrawString(clock_text.c_str(), -1, &text_font, clock_rect, &clock_format, &clock_text_brush);
    }

    if (completed_header_bounds_.right > completed_header_bounds_.left &&
        completed_header_bounds_.bottom > completed_header_bounds_.top)
    {
        Gdiplus::RectF completed_panel(
            static_cast<REAL>(completed_header_bounds_.left),
            static_cast<REAL>(completed_header_bounds_.top),
            static_cast<REAL>(completed_header_bounds_.right - completed_header_bounds_.left),
            static_cast<REAL>(completed_header_bounds_.bottom - completed_header_bounds_.top));
        Gdiplus::GraphicsPath completed_path;
        AddRoundedRectangle(completed_path, completed_panel, 14.0F);

        Gdiplus::SolidBrush completed_fill(MakeColor(205, 255, 255, 255));
        graphics.FillPath(&completed_fill, &completed_path);

        Gdiplus::Pen completed_border(MakeColor(255, 221, 229, 240), 1.0F);
        graphics.DrawPath(&completed_border, &completed_path);

        Gdiplus::RectF completed_text_rect(
            completed_panel.X + 16.0F,
            completed_panel.Y + 2.0F,
            std::max<REAL>(80.0F, completed_panel.Width - 196.0F),
            completed_panel.Height - 4.0F);
        Gdiplus::SolidBrush completed_text_brush(MakeColor(255, 74, 89, 112));
        Gdiplus::Font completed_font(device_context, small_font_);
        Gdiplus::StringFormat completed_format;
        completed_format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        completed_format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

        const std::wstring completed_text = BuildCompletedSummaryText();
        graphics.DrawString(
            completed_text.c_str(), -1, &completed_font, completed_text_rect, &completed_format, &completed_text_brush);
    }
}

void TaskApp::DrawButton(const DRAWITEMSTRUCT* draw_item) const
{
    if (draw_item == nullptr || draw_item->hwndItem == nullptr)
    {
        return;
    }

    const HWND button = draw_item->hwndItem;
    const bool hot = hovered_control_ == button;
    const bool pressed = (draw_item->itemState & ODS_SELECTED) != 0;
    const bool disabled = (draw_item->itemState & ODS_DISABLED) != 0;
    const bool active =
        (button == top_button_ && sort_by_priority_) || (button == auto_launch_button_ && auto_launch_) ||
        (button == background_button_ && !background_path_.empty());

    ButtonPalette palette{
        .top = MakeColor(255, 250, 252, 255),
        .bottom = MakeColor(255, 236, 241, 247),
        .border = MakeColor(255, 208, 218, 230),
        .text = MakeColor(255, 48, 64, 89)};

    if (button == add_button_)
    {
        palette = {MakeColor(255, 79, 144, 255), MakeColor(255, 48, 116, 231), MakeColor(255, 42, 98, 197),
                   MakeColor(255, 255, 255, 255)};
    }
    else if (button == delete_button_)
    {
        palette = {MakeColor(255, 63, 176, 122), MakeColor(255, 39, 145, 94), MakeColor(255, 34, 122, 80),
                   MakeColor(255, 255, 255, 255)};
    }
    else if (button == star_up_button_)
    {
        palette = {MakeColor(255, 255, 201, 92), MakeColor(255, 241, 166, 47), MakeColor(255, 221, 145, 34),
                   MakeColor(255, 66, 45, 12)};
    }
    else if (button == star_down_button_)
    {
        palette = {MakeColor(255, 240, 244, 250), MakeColor(255, 223, 230, 239), MakeColor(255, 201, 210, 222),
                   MakeColor(255, 64, 79, 99)};
    }
    else if (active)
    {
        palette = {MakeColor(255, 103, 128, 255), MakeColor(255, 73, 102, 227), MakeColor(255, 60, 85, 201),
                   MakeColor(255, 255, 255, 255)};
    }

    if (hot)
    {
        palette.top = MakeColor(
            palette.top.GetA(), static_cast<BYTE>(std::min(255, palette.top.GetR() + 8)),
            static_cast<BYTE>(std::min(255, palette.top.GetG() + 8)),
            static_cast<BYTE>(std::min(255, palette.top.GetB() + 8)));
        palette.bottom = MakeColor(
            palette.bottom.GetA(), static_cast<BYTE>(std::min(255, palette.bottom.GetR() + 8)),
            static_cast<BYTE>(std::min(255, palette.bottom.GetG() + 8)),
            static_cast<BYTE>(std::min(255, palette.bottom.GetB() + 8)));
    }
    if (pressed)
    {
        palette.top = MakeColor(
            palette.top.GetA(), static_cast<BYTE>(std::max(0, palette.top.GetR() - 16)),
            static_cast<BYTE>(std::max(0, palette.top.GetG() - 16)),
            static_cast<BYTE>(std::max(0, palette.top.GetB() - 16)));
        palette.bottom = MakeColor(
            palette.bottom.GetA(), static_cast<BYTE>(std::max(0, palette.bottom.GetR() - 20)),
            static_cast<BYTE>(std::max(0, palette.bottom.GetG() - 20)),
            static_cast<BYTE>(std::max(0, palette.bottom.GetB() - 20)));
    }
    if (disabled)
    {
        palette.top = MakeColor(255, 242, 245, 249);
        palette.bottom = MakeColor(255, 232, 236, 241);
        palette.border = MakeColor(255, 218, 224, 232);
        palette.text = MakeColor(255, 146, 155, 170);
    }

    RECT rect = draw_item->rcItem;
    const Gdiplus::RectF button_rect(
        static_cast<REAL>(rect.left) + 1.0F,
        static_cast<REAL>(rect.top) + 1.0F,
        static_cast<REAL>(rect.right - rect.left) - 2.0F,
        static_cast<REAL>(rect.bottom - rect.top) - 2.0F);

    Gdiplus::Graphics graphics(draw_item->hDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    Gdiplus::GraphicsPath path;
    AddRoundedRectangle(path, button_rect, 13.0F);

    Gdiplus::LinearGradientBrush fill_brush(
        Gdiplus::PointF(button_rect.X, button_rect.Y),
        Gdiplus::PointF(button_rect.X, button_rect.GetBottom()),
        palette.top,
        palette.bottom);
    graphics.FillPath(&fill_brush, &path);

    Gdiplus::Pen border_pen(palette.border, 1.0F);
    graphics.DrawPath(&border_pen, &path);

    std::wstring text(64, L'\0');
    const int length = GetWindowTextW(button, text.data(), static_cast<int>(text.size()));
    text.resize(std::max(0, length));

    Gdiplus::SolidBrush text_brush(palette.text);
    Gdiplus::Font font(draw_item->hDC, small_font_);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    graphics.DrawString(text.c_str(), -1, &font, button_rect, &format, &text_brush);

    if ((draw_item->itemState & ODS_FOCUS) != 0)
    {
        Gdiplus::Pen focus_pen(MakeColor(180, 22, 78, 185), 1.0F);
        focus_pen.SetDashStyle(Gdiplus::DashStyleDash);
        Gdiplus::RectF focus_rect = button_rect;
        focus_rect.Inflate(-4.0F, -4.0F);
        Gdiplus::GraphicsPath focus_path;
        AddRoundedRectangle(focus_path, focus_rect, 10.0F);
        graphics.DrawPath(&focus_pen, &focus_path);
    }
}

LRESULT TaskApp::HandleListCustomDraw(LPARAM l_param) const
{
    auto* custom_draw = reinterpret_cast<NMLVCUSTOMDRAW*>(l_param);
    if (custom_draw == nullptr)
    {
        return CDRF_DODEFAULT;
    }

    switch (custom_draw->nmcd.dwDrawStage)
    {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT:
            return CDRF_NOTIFYSUBITEMDRAW;
        case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
        {
            const int item_index = static_cast<int>(custom_draw->nmcd.dwItemSpec);
            if (item_index < 0 || item_index >= static_cast<int>(tasks_.size()))
            {
                return CDRF_DODEFAULT;
            }

            const bool selected = (custom_draw->nmcd.uItemState & CDIS_SELECTED) != 0;
            const bool hot = item_index == hot_item_index_;
            COLORREF background = RGB(251, 253, 255);

            if (tasks_[item_index].priority == 3)
            {
                background = RGB(255, 247, 235);
            }
            else if (tasks_[item_index].priority == 2)
            {
                background = RGB(255, 251, 242);
            }
            else if (tasks_[item_index].priority == 1)
            {
                background = RGB(247, 251, 255);
            }

            if (hot)
            {
                background = RGB(235, 244, 255);
            }
            if (selected)
            {
                background = RGB(212, 229, 255);
            }

            custom_draw->clrTextBk = background;
            custom_draw->clrText = RGB(31, 41, 55);

            if (custom_draw->iSubItem == 0)
            {
                if (tasks_[item_index].priority >= 2)
                {
                    custom_draw->clrText = RGB(180, 107, 0);
                }
                else if (tasks_[item_index].priority == 1)
                {
                    custom_draw->clrText = RGB(92, 112, 138);
                }
                else
                {
                    custom_draw->clrText = RGB(130, 141, 158);
                }
            }

            return CDRF_DODEFAULT;
        }
        default:
            break;
    }

    return CDRF_DODEFAULT;
}

std::wstring TaskApp::BuildTaskLine(const TaskItem& task, bool include_internal_state) const
{
    std::wstring line = std::to_wstring(task.id) + L"\t" + std::to_wstring(task.priority) + L"\t" + task.created_at +
                        L"\t" + task.reminder_at;
    if (include_internal_state)
    {
        line += L"\t" + std::to_wstring(task.reminder_state);
    }
    line += L"\t" + Escape(task.text);
    return line;
}

std::wstring TaskApp::BuildCompletedLine(const CompletedItem& item) const
{
    return item.completed_at + L"\t" + Escape(item.text);
}

bool TaskApp::ParseTaskLine(std::string_view line, TaskItem& task, bool allow_legacy_format) const
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= line.size())
    {
        const std::size_t end = line.find('\t', start);
        if (end == std::string_view::npos)
        {
            parts.push_back(line.substr(start));
            break;
        }
        parts.push_back(line.substr(start, end - start));
        start = end + 1;
    }

    if (parts.size() < 4)
    {
        return false;
    }

    int id = 0;
    int priority = 0;
    if (!ParseInteger(parts[0], id) || !ParseInteger(parts[1], priority))
    {
        return false;
    }

    task.id = id;
    task.priority = std::clamp(priority, 0, 3);
    task.created_at = Utf8ToWide(parts[2]);

    if (parts.size() == 4 && allow_legacy_format)
    {
        task.reminder_at = L"";
        task.reminder_state = 0;
        task.text = Unescape(Utf8ToWide(parts[3]));
        return !task.text.empty();
    }

    if (parts.size() >= 6)
    {
        task.reminder_at = Utf8ToWide(parts[3]);
        if (!ParseInteger(parts[4], task.reminder_state))
        {
            task.reminder_state = 0;
        }
        task.text = Unescape(Utf8ToWide(parts[5]));
        return !task.text.empty();
    }

    if (allow_legacy_format)
    {
        task.reminder_at = L"";
        task.reminder_state = 0;
        task.text = Unescape(Utf8ToWide(parts[4]));
        return !task.text.empty();
    }

    task.reminder_at = Utf8ToWide(parts[3]);
    task.reminder_state = 0;
    task.text = Unescape(Utf8ToWide(parts[4]));
    return !task.text.empty();
}

bool TaskApp::ParseCompletedLine(std::string_view line, CompletedItem& item) const
{
    const std::size_t separator = line.find('\t');
    if (separator == std::string_view::npos)
    {
        return false;
    }

    item.completed_at = Utf8ToWide(line.substr(0, separator));
    item.text = Unescape(Utf8ToWide(line.substr(separator + 1)));
    return !item.text.empty() && !item.completed_at.empty();
}

void TaskApp::LoadTasks()
{
    tasks_.clear();
    next_id_ = 1;

    const std::filesystem::path path(TaskFilePath());
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return;
    }

    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        TaskItem task;
        if (!ParseTaskLine(line, task, true))
        {
            continue;
        }

        next_id_ = std::max(next_id_, task.id + 1);
        tasks_.push_back(std::move(task));
    }

    ApplyTaskOrder();
}

void TaskApp::SaveTasks() const
{
    const std::filesystem::path path(TaskFilePath());
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        return;
    }

    for (const TaskItem& task : tasks_)
    {
        output << WideToUtf8(BuildTaskLine(task, true)) << '\n';
    }
}

void TaskApp::LoadCompletedTasks()
{
    completed_tasks_.clear();

    const std::filesystem::path path(CompletedTaskFilePath());
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return;
    }

    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.empty())
        {
            continue;
        }

        CompletedItem item;
        if (!ParseCompletedLine(line, item))
        {
            continue;
        }
        completed_tasks_.push_back(std::move(item));
    }
}

void TaskApp::SaveCompletedTasks() const
{
    const std::filesystem::path path(CompletedTaskFilePath());
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        return;
    }

    for (const CompletedItem& item : completed_tasks_)
    {
        output << WideToUtf8(BuildCompletedLine(item)) << '\n';
    }
}

void TaskApp::LoadSettings()
{
    auto_launch_ = IsAutoLaunchEnabled();

    const std::filesystem::path path(SettingsFilePath());
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return;
    }

    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        const std::size_t separator = line.find('\t');
        if (separator == std::string::npos)
        {
            continue;
        }

        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        int numeric_value = 0;

        if (key == "sort_by_priority")
        {
            sort_by_priority_ = value == "1";
            continue;
        }
        if (key == "top_most")
        {
            continue;
        }
        if (key == "reminder_lead" && ParseInteger(value, numeric_value))
        {
            reminder_lead_minutes_ = std::max(0, numeric_value);
            continue;
        }

        if ((key == "x" || key == "y" || key == "width" || key == "height") && ParseInteger(value, numeric_value))
        {
            if (key == "x")
            {
                startup_bounds_.left = numeric_value;
            }
            else if (key == "y")
            {
                startup_bounds_.top = numeric_value;
            }
            else if (key == "width")
            {
                startup_bounds_.right = std::max(kMinWidth, numeric_value);
            }
            else if (key == "height")
            {
                startup_bounds_.bottom = std::max(kMinHeight, numeric_value);
            }
            continue;
        }

        if (key == "background")
        {
            background_path_ = Unescape(Utf8ToWide(value));
        }
    }

    LoadBackgroundImage();
}

void TaskApp::SaveSettings() const
{
    RECT bounds = startup_bounds_;
    if (window_ != nullptr)
    {
        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        if (GetWindowPlacement(window_, &placement))
        {
            bounds.left = placement.rcNormalPosition.left;
            bounds.top = placement.rcNormalPosition.top;
            bounds.right = placement.rcNormalPosition.right - placement.rcNormalPosition.left;
            bounds.bottom = placement.rcNormalPosition.bottom - placement.rcNormalPosition.top;
        }
    }

    const std::filesystem::path path(SettingsFilePath());
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        return;
    }

    output << "sort_by_priority\t" << (sort_by_priority_ ? "1" : "0") << '\n';
    output << "reminder_lead\t" << reminder_lead_minutes_ << '\n';
    output << "x\t" << bounds.left << '\n';
    output << "y\t" << bounds.top << '\n';
    output << "width\t" << bounds.right << '\n';
    output << "height\t" << bounds.bottom << '\n';
    output << "background\t" << WideToUtf8(Escape(background_path_)) << '\n';
}

void TaskApp::LoadBackgroundImage()
{
    background_image_.reset();
    if (background_path_.empty() || gdiplus_token_ == 0)
    {
        return;
    }

    auto image = std::make_unique<Gdiplus::Image>(background_path_.c_str());
    if (image->GetLastStatus() != Gdiplus::Ok || image->GetWidth() == 0 || image->GetHeight() == 0)
    {
        background_path_.clear();
        return;
    }

    background_image_ = std::move(image);
}

std::wstring TaskApp::ReadInputText() const
{
    const int text_length = GetWindowTextLengthW(input_);
    std::wstring text(text_length + 1, L'\0');
    GetWindowTextW(input_, text.data(), text_length + 1);
    text.resize(text_length);
    return Trim(std::move(text));
}

int TaskApp::ReadPriority() const
{
    const LRESULT index = SendMessageW(priority_, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR)
    {
        return 0;
    }

    return std::clamp(static_cast<int>(index), 0, 3);
}

int TaskApp::SelectedTaskIndex() const
{
    return ListView_GetNextItem(list_, -1, LVNI_SELECTED);
}

std::wstring TaskApp::TaskFilePath() const
{
    wchar_t appdata[MAX_PATH]{};
    const DWORD size = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (size > 0 && size < MAX_PATH)
    {
        return std::wstring(appdata) + L"\\TaskQueue\\tasks.txt";
    }

    wchar_t module_path[MAX_PATH]{};
    GetModuleFileNameW(instance_, module_path, MAX_PATH);
    std::wstring fallback(module_path);
    const std::size_t separator = fallback.find_last_of(L"\\/");
    const std::wstring folder = separator == std::wstring::npos ? L"." : fallback.substr(0, separator);
    return folder + L"\\tasks.txt";
}

std::wstring TaskApp::CompletedTaskFilePath() const
{
    wchar_t appdata[MAX_PATH]{};
    const DWORD size = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (size > 0 && size < MAX_PATH)
    {
        return std::wstring(appdata) + L"\\TaskQueue\\completed.txt";
    }

    wchar_t module_path[MAX_PATH]{};
    GetModuleFileNameW(instance_, module_path, MAX_PATH);
    std::wstring fallback(module_path);
    const std::size_t separator = fallback.find_last_of(L"\\/");
    const std::wstring folder = separator == std::wstring::npos ? L"." : fallback.substr(0, separator);
    return folder + L"\\completed.txt";
}

std::wstring TaskApp::SettingsFilePath() const
{
    wchar_t appdata[MAX_PATH]{};
    const DWORD size = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (size > 0 && size < MAX_PATH)
    {
        return std::wstring(appdata) + L"\\TaskQueue\\settings.txt";
    }

    wchar_t module_path[MAX_PATH]{};
    GetModuleFileNameW(instance_, module_path, MAX_PATH);
    std::wstring fallback(module_path);
    const std::size_t separator = fallback.find_last_of(L"\\/");
    const std::wstring folder = separator == std::wstring::npos ? L"." : fallback.substr(0, separator);
    return folder + L"\\settings.txt";
}

bool TaskApp::IsAutoLaunchEnabled() const
{
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_READ, &key) != ERROR_SUCCESS)
    {
        return false;
    }

    wchar_t value[2048]{};
    DWORD type = REG_SZ;
    DWORD size = sizeof(value);
    const LONG result = RegQueryValueExW(
        key, kRunValueName, nullptr, &type, reinterpret_cast<LPBYTE>(value), &size);
    RegCloseKey(key);

    return result == ERROR_SUCCESS && type == REG_SZ && value[0] != L'\0';
}

void TaskApp::SetAutoLaunchEnabled(bool enabled)
{
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
    {
        MessageBoxW(window_, L"无法写入开机启动设置。", L"设置失败", MB_ICONERROR | MB_OK);
        return;
    }

    if (enabled)
    {
        wchar_t module_path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, module_path, MAX_PATH);
        const std::wstring command = L"\"" + std::wstring(module_path) + L"\"";
        const LONG result = RegSetValueExW(
            key,
            kRunValueName,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);

        if (result != ERROR_SUCCESS)
        {
            MessageBoxW(window_, L"开机启动设置失败。", L"设置失败", MB_ICONERROR | MB_OK);
            return;
        }
    }
    else
    {
        RegDeleteValueW(key, kRunValueName);
        RegCloseKey(key);
    }

    auto_launch_ = enabled;
    SaveSettings();
}

std::wstring TaskApp::Trim(std::wstring text)
{
    const auto not_space = [](wchar_t value) { return !iswspace(value); };
    const auto begin = std::find_if(text.begin(), text.end(), not_space);
    if (begin == text.end())
    {
        return L"";
    }

    const auto end = std::find_if(text.rbegin(), text.rend(), not_space).base();
    return std::wstring(begin, end);
}

std::wstring TaskApp::Escape(std::wstring_view text)
{
    std::wstring escaped;
    escaped.reserve(text.size());

    for (const wchar_t value : text)
    {
        switch (value)
        {
            case L'\\':
                escaped += L"\\\\";
                break;
            case L'\n':
                escaped += L"\\n";
                break;
            case L'\r':
                break;
            case L'\t':
                escaped += L"\\t";
                break;
            default:
                escaped += value;
                break;
        }
    }

    return escaped;
}

std::wstring TaskApp::Unescape(std::wstring_view text)
{
    std::wstring unescaped;
    unescaped.reserve(text.size());

    bool escape = false;
    for (const wchar_t value : text)
    {
        if (!escape)
        {
            if (value == L'\\')
            {
                escape = true;
            }
            else
            {
                unescaped += value;
            }
            continue;
        }

        switch (value)
        {
            case L'n':
                unescaped += L'\n';
                break;
            case L't':
                unescaped += L'\t';
                break;
            case L'\\':
                unescaped += L'\\';
                break;
            default:
                unescaped += value;
                break;
        }
        escape = false;
    }

    if (escape)
    {
        unescaped += L'\\';
    }

    return unescaped;
}

std::string TaskApp::WideToUtf8(std::wstring_view text)
{
    if (text.empty())
    {
        return {};
    }

    const int size = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);

    std::string utf8(size, '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
    return utf8;
}

std::wstring TaskApp::Utf8ToWide(std::string_view text)
{
    if (text.empty())
    {
        return {};
    }

    const int size = MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);

    std::wstring wide(size, L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

#if 0
std::wstring TaskApp::FormatPriority(int priority)
{
    priority = std::clamp(priority, 0, 3);
    if (priority == 0)
    {
        return L"0 星";
    }

    std::wstring stars;
    for (int index = 0; index < priority; ++index)
    {
        stars += L"\x2605";
    }
    return stars;
}

std::wstring TaskApp::CurrentTimestamp()
{
    SYSTEMTIME time{};
    GetLocalTime(&time);

    wchar_t buffer[32]{};
    swprintf(
        buffer,
        sizeof(buffer) / sizeof(buffer[0]),
        L"%04u-%02u-%02u %02u:%02u",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute);

    return buffer;
}
#endif

std::wstring TaskApp::FormatPriority(int priority)
{
    priority = std::clamp(priority, 0, 3);
    if (priority == 0)
    {
        return L"0 \u661f";
    }

    std::wstring stars;
    for (int index = 0; index < priority; ++index)
    {
        stars += L"\u2605";
    }
    return stars;
}

std::wstring TaskApp::CurrentClockTimestamp()
{
    SYSTEMTIME time{};
    GetLocalTime(&time);
    return FormatSystemTime(time, true);
}

std::wstring TaskApp::FormatReminder(std::wstring_view reminder_at)
{
    return reminder_at.empty() ? L"" : std::wstring(reminder_at);
}

std::wstring TaskApp::FormatSystemTime(const SYSTEMTIME& time, bool include_seconds)
{
    wchar_t buffer[32]{};
    if (include_seconds)
    {
        swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%04u-%02u-%02u %02u:%02u:%02u", time.wYear, time.wMonth,
            time.wDay, time.wHour, time.wMinute, time.wSecond);
    }
    else
    {
        swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%04u-%02u-%02u %02u:%02u", time.wYear, time.wMonth,
            time.wDay, time.wHour, time.wMinute);
    }
    return buffer;
}

bool TaskApp::ParseSystemTimeString(std::wstring_view text, SYSTEMTIME& time)
{
    unsigned int year = 0;
    unsigned int month = 0;
    unsigned int day = 0;
    unsigned int hour = 0;
    unsigned int minute = 0;
    unsigned int second = 0;

    const int parsed = swscanf(text.data(), L"%u-%u-%u %u:%u:%u", &year, &month, &day, &hour, &minute, &second);
    if (parsed < 5)
    {
        return false;
    }

    time = {};
    time.wYear = static_cast<WORD>(year);
    time.wMonth = static_cast<WORD>(month);
    time.wDay = static_cast<WORD>(day);
    time.wHour = static_cast<WORD>(hour);
    time.wMinute = static_cast<WORD>(minute);
    time.wSecond = parsed >= 6 ? static_cast<WORD>(second) : 0;
    return true;
}

ULONGLONG TaskApp::ToFileTimeTicks(const SYSTEMTIME& time)
{
    FILETIME file_time{};
    if (!SystemTimeToFileTime(&time, &file_time))
    {
        return 0;
    }

    ULARGE_INTEGER value{};
    value.LowPart = file_time.dwLowDateTime;
    value.HighPart = file_time.dwHighDateTime;
    return value.QuadPart;
}

int TaskApp::ReminderLeadMinutesFromIndex(int combo_index)
{
    switch (combo_index)
    {
        case 0:
            return 0;
        case 1:
            return 5;
        case 2:
            return 10;
        case 3:
            return 15;
        case 4:
            return 30;
        default:
            return 10;
    }
}

int TaskApp::ReminderLeadIndexFromMinutes(int minutes)
{
    switch (minutes)
    {
        case 0:
            return 0;
        case 5:
            return 1;
        case 10:
            return 2;
        case 15:
            return 3;
        case 30:
            return 4;
        default:
            return 2;
    }
}

std::wstring TaskApp::CurrentTimestamp()
{
    SYSTEMTIME time{};
    GetLocalTime(&time);
    return FormatSystemTime(time, false);
}
