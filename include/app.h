#pragma once

#include <windows.h>

#include <gdiplus.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct TaskItem
{
    int id{};
    int priority{};
    std::wstring text;
    std::wstring created_at;
    std::wstring reminder_at;
    int reminder_state{};
};

struct CompletedItem
{
    std::wstring text;
    std::wstring completed_at;
};

class TaskApp
{
public:
    explicit TaskApp(HINSTANCE instance);
    ~TaskApp();

    bool Initialize(int command_show);
    int Run();

private:
    static constexpr wchar_t kWindowClassName[] = L"TaskQueueWindowClass";

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
    static LRESULT CALLBACK InputSubclassProc(
        HWND window,
        UINT message,
        WPARAM w_param,
        LPARAM l_param,
        UINT_PTR subclass_id,
        DWORD_PTR reference_data);
    static LRESULT CALLBACK HoverSubclassProc(
        HWND window,
        UINT message,
        WPARAM w_param,
        LPARAM l_param,
        UINT_PTR subclass_id,
        DWORD_PTR reference_data);
    static LRESULT CALLBACK ListSubclassProc(
        HWND window,
        UINT message,
        WPARAM w_param,
        LPARAM l_param,
        UINT_PTR subclass_id,
        DWORD_PTR reference_data);

    LRESULT HandleMessage(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
    void CreateChildControls();
    void ConfigureListView() const;
    void ApplyFont(HWND control, HFONT font) const;
    void LayoutControls();
    void AddTaskFromInput();
    void DeleteSelectedTask();
    void ChangeSelectedPriority(int delta);
    void CycleSelectedPriority();
    void ToggleSortMode();
    void ToggleAutoLaunch();
    void ChooseBackgroundImage();
    void ExportTaskList();
    void ImportTaskList();
    void ShowTaskListMenu();
    void ShowReminderMenu();
    void SetSelectedTaskReminder();
    void ClearSelectedTaskReminder();
    void ClearCompletedTasks();
    void RefreshTaskList() const;
    void RefreshCompletedList() const;
    void UpdateSummary() const;
    void UpdateButtonLabels() const;
    void UpdateClock() const;
    void RedrawTaskRow(int index) const;
    void TrackButtonHover(HWND control);
    void TrackListHover();
    void SyncReminderEditor();
    void CheckReminders();
    void ApplyTaskOrder();
    void SelectTaskById(int task_id) const;
    void PaintWindow(HDC device_context) const;
    void DrawButton(const DRAWITEMSTRUCT* draw_item) const;
    LRESULT HandleListCustomDraw(LPARAM l_param) const;
    void LoadTasks();
    void SaveTasks() const;
    void LoadCompletedTasks();
    void SaveCompletedTasks() const;
    void LoadSettings();
    void SaveSettings() const;
    void LoadBackgroundImage();
    std::wstring BuildSummaryText() const;
    std::wstring BuildCompletedSummaryText() const;
    std::wstring ReadInputText() const;
    int ReadPriority() const;
    int SelectedTaskIndex() const;
    std::wstring TaskFilePath() const;
    std::wstring CompletedTaskFilePath() const;
    std::wstring SettingsFilePath() const;
    bool IsAutoLaunchEnabled() const;
    void SetAutoLaunchEnabled(bool enabled);
    std::wstring BuildTaskLine(const TaskItem& task, bool include_internal_state) const;
    bool ParseTaskLine(std::string_view line, TaskItem& task, bool allow_legacy_format) const;
    std::wstring BuildCompletedLine(const CompletedItem& item) const;
    bool ParseCompletedLine(std::string_view line, CompletedItem& item) const;

    static std::wstring Trim(std::wstring text);
    static std::wstring Escape(std::wstring_view text);
    static std::wstring Unescape(std::wstring_view text);
    static std::string WideToUtf8(std::wstring_view text);
    static std::wstring Utf8ToWide(std::string_view text);
    static std::wstring FormatPriority(int priority);
    static std::wstring CurrentTimestamp();
    static std::wstring CurrentClockTimestamp();
    static std::wstring FormatReminder(std::wstring_view reminder_at);
    static std::wstring FormatSystemTime(const SYSTEMTIME& time, bool include_seconds);
    static bool ParseSystemTimeString(std::wstring_view text, SYSTEMTIME& time);
    static ULONGLONG ToFileTimeTicks(const SYSTEMTIME& time);
    static int ReminderLeadMinutesFromIndex(int combo_index);
    static int ReminderLeadIndexFromMinutes(int minutes);

    HINSTANCE instance_{};
    HWND window_{nullptr};
    HWND input_{nullptr};
    HWND priority_{nullptr};
    HWND reminder_button_{nullptr};
    HWND task_list_button_{nullptr};
    HWND add_button_{nullptr};
    HWND delete_button_{nullptr};
    HWND star_down_button_{nullptr};
    HWND star_up_button_{nullptr};
    HWND top_button_{nullptr};
    HWND auto_launch_button_{nullptr};
    HWND background_button_{nullptr};
    HWND clear_completed_button_{nullptr};
    HWND list_{nullptr};
    HWND completed_list_{nullptr};
    HFONT font_{nullptr};
    HFONT small_font_{nullptr};
    HBRUSH edit_brush_{nullptr};
    bool sort_by_priority_{false};
    bool auto_launch_{false};
    int reminder_lead_minutes_{10};
    int next_id_{1};
    int hot_item_index_{-1};
    HWND hovered_control_{nullptr};
    RECT startup_bounds_{CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720};
    RECT summary_bounds_{};
    RECT completed_header_bounds_{};
    std::wstring background_path_;
    std::unique_ptr<Gdiplus::Image> background_image_;
    ULONG_PTR gdiplus_token_{};
    std::vector<TaskItem> tasks_;
    std::vector<CompletedItem> completed_tasks_;
};
