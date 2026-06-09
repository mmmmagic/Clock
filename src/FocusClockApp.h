#pragma once

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <objbase.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cwctype>
#include <future>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace focus_clock {

using namespace Gdiplus;
constexpr UINT_PTR kClockTimer = 1;
constexpr UINT_PTR kGuardTimer = 2;
constexpr UINT_PTR kWhitelistLayoutTimer = 3;
constexpr int kClockTimerMs = 250;
constexpr int kGuardTimerMs = 1000;
constexpr int kWhitelistLayoutTimerMs = 16;
constexpr DWORD kWhitelistLayoutSaveDelayMs = 700;
constexpr double kPi = 3.14159265358979323846;
constexpr int kMinFocusMinutes = 1;
constexpr int kDefaultMaxFocusMinutes = 240;
constexpr int kAbsoluteMaxFocusMinutes = 1440;
constexpr int kDefaultFocusMinutes = 25;
constexpr int kStartButtonId = 1001;
constexpr int kExitButtonId = 1002;
constexpr int kCustomMinusFiveId = 1101;
constexpr int kCustomMinusOneId = 1102;
constexpr int kCustomPlusOneId = 1103;
constexpr int kCustomPlusFiveId = 1104;
constexpr int kCustomDisplayId = 1105;
constexpr int kWhitelistButtonBaseId = 2000;
constexpr int kWhitelistButtonLimit = 3000;
constexpr int kPanelButtonBaseId = 3000;
constexpr int kPanelCloseButtonId = 3001;
constexpr int kPanelResetSettingsId = 3002;
constexpr int kPanelTabButtonBaseId = 3100;
constexpr int kPanelTabOverviewId = 1;
constexpr int kPanelTabSettingsId = 2;
constexpr int kPanelTabScheduleId = 3;
constexpr int kPanelTabRerunId = 4;
constexpr int kPanelTabWhitelistId = 5;
constexpr int kPanelTabUpdateId = 6;
constexpr int kSettingMaxMinusFiveId = 3201;
constexpr int kSettingMaxMinusOneId = 3202;
constexpr int kSettingMaxDisplayId = 3203;
constexpr int kSettingMaxPlusOneId = 3204;
constexpr int kSettingMaxPlusFiveId = 3205;
constexpr int kSettingDefaultMinusFiveId = 3211;
constexpr int kSettingDefaultMinusOneId = 3212;
constexpr int kSettingDefaultDisplayId = 3213;
constexpr int kSettingDefaultPlusOneId = 3214;
constexpr int kSettingDefaultPlusFiveId = 3215;
constexpr int kScheduleStartHourMinusId = 3301;
constexpr int kScheduleStartHourDisplayId = 3302;
constexpr int kScheduleStartHourPlusId = 3303;
constexpr int kScheduleStartMinuteMinusId = 3304;
constexpr int kScheduleStartMinuteDisplayId = 3305;
constexpr int kScheduleStartMinutePlusId = 3306;
constexpr int kScheduleEndHourMinusId = 3311;
constexpr int kScheduleEndHourDisplayId = 3312;
constexpr int kScheduleEndHourPlusId = 3313;
constexpr int kScheduleEndMinuteMinusId = 3314;
constexpr int kScheduleEndMinuteDisplayId = 3315;
constexpr int kScheduleEndMinutePlusId = 3316;
constexpr int kScheduleAddTaskId = 3320;
constexpr int kScheduleDeleteButtonBaseId = 3400;
constexpr int kScheduleDeleteButtonLimit = 3600;
constexpr int kRerunCreateTaskId = 3601;
constexpr int kWhitelistAddFileId = 3701;
constexpr int kWhitelistAddFolderId = 3702;
constexpr int kWhitelistAddWindowId = 3703;
constexpr int kWhitelistDeleteButtonBaseId = 3800;
constexpr int kWhitelistDeleteButtonLimit = 4000;
constexpr int kPanelHotkeyId = 4001;
constexpr int kUpdateNowButtonId = 4101;
constexpr int kWindowPopupBaseId = 50000;
constexpr int kMinWhitelistIconSize = 48;
constexpr int kMaxWhitelistIconSize = 120;
constexpr int kDefaultWhitelistIconSize = 72;
constexpr int kWhitelistMoveStep = 10;
constexpr int kWhitelistResizeStep = 6;
constexpr UINT kRerunTaskCommandFinishedMessage = WM_APP + 1;
constexpr UINT kUpdateCheckFinishedMessage = WM_APP + 2;
constexpr UINT kUpdateDownloadFinishedMessage = WM_APP + 3;
constexpr UINT kUpdateLogMessage = WM_APP + 4;
constexpr WPARAM kRerunTaskCreateCommand = 1;
constexpr WPARAM kRerunTaskStatusCommand = 2;
constexpr DWORD kRerunTaskLaunchFailed = 0xFFFFFFFF;
constexpr DWORD kRerunTaskTimedOut = 0xFFFFFFFE;
constexpr DWORD kUpdateApiTimeoutMs = 8000;
constexpr DWORD kUpdateDownloadTimeoutMs = 300000;
constexpr const wchar_t* kLatestReleaseUrl = L"https://api.github.com/repos/mmmmagic/Clock/releases/latest";

constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;

struct Theme {
    Color background;
    Color panel;
    Color primaryText;
    Color secondaryText;
    Color mutedText;
    Color accent;
    Color accentSoft;
    Color danger;
    Color line;
};

struct UiButton {
    RECT rect{};
    std::wstring label;
    std::wstring iconPath;
    HICON icon = nullptr;
    int id = 0;
    bool enabled = true;
    bool selected = false;
    bool primary = false;
    bool danger = false;
    bool iconOnly = false;
};

struct PanelTabDefinition {
    int id = 0;
    const wchar_t* title = L"";
};

struct WhitelistEntry {
    std::wstring launchSpec;
    std::wstring normalized;
    std::wstring exeName;
    std::wstring iconPath;
    std::wstring label;
    HICON icon = nullptr;
};

struct WindowPathChoice {
    HWND window = nullptr;
    DWORD pid = 0;
    std::wstring title;
    std::wstring path;
    std::wstring exeName;
};

struct ScheduledFocusTask {
    int startMinute = 9 * 60;
    int endMinute = 9 * 60 + kDefaultFocusMinutes;
    int lastStartedDate = 0;
};

struct ReleaseInfo {
    bool ok = false;
    std::wstring body;
    std::wstring publishedAt;
    std::wstring browserDownloadUrl;
    std::wstring assetName;
    std::wstring assetDigest;
    long long publishedUnix = 0;
};

struct HttpTextResult {
    bool ok = false;
    DWORD statusCode = 0;
    DWORD elapsedMs = std::numeric_limits<DWORD>::max();
    std::wstring body;
};

struct UpdateSource {
    int order = 0;
    const wchar_t* name = L"";
    const wchar_t* prefix = L"";
};

struct UpdateDownloadResult {
    bool ok = false;
    std::wstring message;
};

struct UpdateSourceProbe {
    UpdateSource source;
    HttpTextResult response;
    ReleaseInfo release;
};

class FocusClockApp;

struct FindWindowContext {
    const FocusClockApp* app = nullptr;
    const WhitelistEntry* entry = nullptr;
    HWND found = nullptr;
};

struct WhitelistedWindowListContext {
    const FocusClockApp* app = nullptr;
    std::vector<HWND>* windows = nullptr;
};

struct WindowPathChoiceContext {
    const FocusClockApp* app = nullptr;
    std::vector<WindowPathChoice>* choices = nullptr;
};

class FocusClockApp {
public:
    ~FocusClockApp();
    int Run(HINSTANCE instance, int show, long long rerunResumeSeconds = 0);

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    bool Create(HINSTANCE instance, int show);
    void InstallFocusKeyboardHook();
    void RemoveFocusKeyboardHook();
    bool ShouldBlockFocusShortcut(DWORD vkCode) const;
    void EnterFullscreenTopmost();
    void EnterFullscreenNotTopmost();
    void EnterFullscreenBelow(HWND aboveWindow);
    void ApplyDarkMode();
    void RefreshTheme();
    void EnsurePaintFonts(int width);
    void ReleaseRenderResources();
    bool LoadWhitelistIfNeeded(bool force = false);
    void StartFocus();
    void StartFocusForSeconds(long long durationSeconds, bool updateSessionSettings = true, long long totalSeconds = 0);
    void FinishFocus();
    void RebuildLayout();
    void Paint();
    void DrawBackground(Graphics& g, const RECT& rc);
    void DrawAnalogClock(Graphics& g, const RectF& bounds, const SYSTEMTIME& now);
    void DrawFocusProgressBar(Graphics& g, const RectF& bounds, const Theme& theme) const;
    void DrawHand(Graphics& g, const PointF& center, float radius, double angleRadians, float width, const Color& color);
    void DrawTextBlock(Graphics& g, const std::wstring& text, const RectF& rect, Font& font, const Color& color, StringAlignment align, StringAlignment lineAlign);
    void DrawButton(Graphics& g, const UiButton& button);
    void DrawButtonIcon(Graphics& g, const UiButton& button, const RectF& bounds);
    void HitTestAndClick(POINT pt);
    bool BeginWhitelistDrag(POINT pt);
    void UpdateWhitelistDrag(POINT pt);
    void EndWhitelistDrag();
    bool IsShiftDown() const;
    bool IsPointInWhitelistArea(POINT pt) const;
    void MoveWhitelistLayout(int dx, int dy);
    void ResizeWhitelistLayout(int delta);
    void RebuildWhitelistLayoutOnly();
    void MarkWhitelistLayoutChanged(bool needsRebuild);
    void ProcessPendingWhitelistLayout(bool forceSave = false);
    void InvalidateWhitelistArea(const RECT& previousBounds);
    void ClampWhitelistLayout(int clientWidth, int clientHeight);
    void LoadWhitelistLayoutSettings();
    void SaveWhitelistLayoutSettings() const;
    void LoadAppSettings();
    void SaveAppSettings() const;
    void ResetAppSettings();
    void SaveFocusSessionSettings(long long durationSeconds, long long totalSeconds = 0) const;
    void ClearFocusSessionSettings() const;
    void TogglePanel();
    void ClosePanel();
    void DrawPanel(Graphics& g, const RECT& rc);
    void DrawOverviewTab(Graphics& g, const RectF& contentRect, const Theme& theme, FontFamily& family);
    void DrawSettingsTab(Graphics& g, const RectF& contentRect, const Theme& theme, FontFamily& family);
    void DrawScheduleTab(Graphics& g, const RectF& contentRect, const Theme& theme, FontFamily& family);
    void DrawRerunTab(Graphics& g, const RectF& contentRect, const Theme& theme, FontFamily& family);
    void DrawWhitelistTab(Graphics& g, const RectF& contentRect, const Theme& theme, FontFamily& family);
    void DrawUpdateTab(Graphics& g, const RectF& contentRect, const Theme& theme, FontFamily& family);
    void AddPanelButton(int id, const RECT& rect, const std::wstring& label, bool selected = false, bool enabled = true, bool primary = false, bool danger = false);
    void AddStepperButtons(int minusFiveId, int minusOneId, int displayId, int plusOneId, int plusFiveId, int left, int top, int value, const std::wstring& suffix, bool canDecrease, bool canIncrease);
    void AddTimeStepperButtons(int minusId, int displayId, int plusId, int left, int top, int value, int maxValue, const std::wstring& suffix);
    void HandlePanelCommand(int id);
    void StartUpdateCheck(bool force = false);
    void HandleUpdateCheckResult(ReleaseInfo* release);
    void StartUpdateDownload();
    void HandleUpdateDownloadResult(UpdateDownloadResult* result);
    void AppendUpdateLog(const std::wstring& message, bool isError = false);
    void CleanupUpdateArtifacts() const;
    void SetMaxFocusMinutes(int minutes);
    void SetDefaultFocusMinutes(int minutes);
    void AdjustScheduleDraft(int id);
    void AddScheduledFocusTask();
    void DeleteScheduledFocusTask(size_t index);
    void ScrollScheduleTab(int delta);
    int ScheduleContentMaxScroll() const;
    void ScrollWhitelistTab(int delta);
    int WhitelistContentMaxScroll() const;
    RECT PanelContentViewport() const;
    void LoadScheduledFocusTasks();
    void SaveScheduledFocusTasks() const;
    void CheckScheduledFocusTasks(bool forceResumeActiveRange = false);
    void CreateRerunStartupTask();
    void CheckRerunStartupTaskStatus();
    void RunRerunTaskCommandAsync(std::wstring command, WPARAM commandKind, DWORD timeoutMs);
    void HandleRerunTaskCommandResult(WPARAM commandKind, LPARAM result);
    std::wstring GetWhitelistPath() const;
    void DestroyWhitelistIconCache();
    bool SaveWhitelistEntries(const std::vector<std::wstring>& launchSpecs);
    void AddWhitelistPath(const std::wstring& path);
    void AddWhitelistFolder(const std::wstring& folder);
    void AddWhitelistFromFileDialog();
    void AddWhitelistFromFolderDialog();
    void AddWhitelistFromWindow();
    void DeleteWhitelistEntry(size_t index);
    std::vector<std::wstring> EnumerateExecutableFilesInDirectory(const std::wstring& folder) const;
    std::vector<WindowPathChoice> EnumerateSelectableWindows() const;
    bool IsWhitelistPathDuplicate(const std::wstring& path) const;
    bool HasScheduleConflict(int startMinute, int endMinute, int ignoreIndex = -1) const;
    bool IsValidScheduleRange(int startMinute, int endMinute) const;
    int ScheduleDurationMinutes(int startMinute, int endMinute) const;
    bool IsMinuteInScheduleRange(int minuteOfDay, int startMinute, int endMinute) const;
    int ScheduleRemainingSeconds(const ScheduledFocusTask& task, int secondOfDay) const;
    int ScheduleStartDateStamp(const ScheduledFocusTask& task, const SYSTEMTIME& now) const;
    int ScheduleDraftStartMinute() const;
    int ScheduleDraftEndMinute() const;
    std::wstring FormatMinuteOfDay(int minute) const;
    static int DateStamp(const SYSTEMTIME& time);
    static int PreviousDateStamp(const SYSTEMTIME& time);
    std::wstring GetSettingsPath() const;
    void OpenWhitelistEntry(size_t index);
    HWND FindRunningWhitelistWindow(const WhitelistEntry& entry) const;
    void BringWindowToFront(HWND target);
    void PromoteWhitelistWindow(HWND target, bool reorderExisting = false);
    HWND PromoteVisibleWhitelistedWindows();
    void RestorePromotedWhitelistWindows();
    void RestoreWhitelistWindow(HWND target, LONG_PTR savedStyle);
    bool IsTrackedWhitelistWindowValid() const;
    bool TryResolvePendingWhitelistWindow();
    void UpdateRemaining();
    std::wstring FormatTime(const SYSTEMTIME& now) const;
    std::wstring FormatDate(const SYSTEMTIME& now) const;
    std::wstring FormatRemaining() const;
    bool IsSystemDarkMode() const;
    bool IsWhitelistedForegroundWindow();
    bool IsExecutableWhitelisted(const std::wstring& path) const;
    bool ShouldYieldToWhitelist() const;
    std::wstring GetExecutableDirectory() const;
    std::wstring GetProcessImagePath(HWND window) const;
    static std::wstring ToLower(std::wstring value);
    static std::wstring Trim(std::wstring value);
    static std::wstring BaseName(const std::wstring& path);
    static bool IsExecutableFileName(const std::wstring& filename);
    static std::wstring JoinPath(const std::wstring& directory, const std::wstring& name);
    static std::wstring StripExtension(const std::wstring& filename);
    static std::wstring ResolveLaunchPath(const std::wstring& launchSpec);
    static HICON LoadIconForPath(const std::wstring& path);
    static std::wstring FormatConfigDateTime(const SYSTEMTIME& time);
    static long long FileTimeToUnixSeconds(const FILETIME& fileTime);
    static std::wstring DecodeTextFile(const std::vector<char>& bytes);
    static HttpTextResult HttpGetText(const std::wstring& url, DWORD timeoutMs);
    static ReleaseInfo ParseReleaseInfo(const std::wstring& text);
    static bool TryParseGithubDateTime(const std::wstring& value, long long& unixSeconds);
    static bool TryParseJsonStringField(const std::wstring& json, const std::wstring& key, std::wstring& value, size_t startAt = 0);
    static bool TryRunCommand(std::wstring command, DWORD timeoutMs, DWORD& exitCode);
    static bool LaunchDetachedCommand(std::wstring command);
    static bool LaunchDetachedProcess(const std::wstring& application, const std::wstring& arguments);
    static std::wstring QuoteCommandArgument(const std::wstring& value);
    static std::wstring QuotePowerShellString(const std::wstring& value);
    static std::wstring SanitizeFileName(std::wstring value);
    static BOOL CALLBACK EnumSelectableWindows(HWND window, LPARAM param);
    static BOOL CALLBACK EnumWhitelistWindows(HWND window, LPARAM param);
    static BOOL CALLBACK EnumVisibleWhitelistedWindows(HWND window, LPARAM param);
    static BOOL CALLBACK EnumWhitelistedWindowsForRestore(HWND window, LPARAM param);

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HHOOK keyboardHook_ = nullptr;
    ULONG_PTR gdiplusToken_ = 0;
    std::unique_ptr<FontFamily> uiFontFamily_;
    std::unique_ptr<FontFamily> monoFontFamily_;
    std::unique_ptr<Font> digitalFont_;
    std::unique_ptr<Font> dateFont_;
    std::unique_ptr<Font> statusFont_;
    std::unique_ptr<Font> remainFont_;
    std::unique_ptr<Font> focusHintFont_;
    std::unique_ptr<Font> idleHintFont_;
    int cachedPaintFontWidth_ = 0;
    bool darkMode_ = false;
    bool focusActive_ = false;
    bool panelOpen_ = false;
    long long rerunResumeSeconds_ = 0;
    HWND activeWhitelistWindow_ = nullptr;
    int maxFocusMinutes_ = kDefaultMaxFocusMinutes;
    int defaultFocusMinutes_ = kDefaultFocusMinutes;
    int selectedMinutes_ = kDefaultFocusMinutes;
    long long remainingSeconds_ = kDefaultFocusMinutes * 60;
    long long focusTotalSeconds_ = kDefaultFocusMinutes * 60;
    int activePanelTab_ = kPanelTabSettingsId;
    RECT panelBounds_{};
    std::vector<PanelTabDefinition> panelTabs_{
        { kPanelTabOverviewId, L"概览" },
        { kPanelTabScheduleId, L"计划" },
        { kPanelTabSettingsId, L"设置" },
        { kPanelTabWhitelistId, L"白名单" },
        { kPanelTabUpdateId, L"更新" }
    };
    std::vector<ScheduledFocusTask> scheduledTasks_;
    int scheduleScrollOffset_ = 0;
    int scheduleDraftStartHour_ = 9;
    int scheduleDraftStartMinute_ = 0;
    int scheduleDraftEndHour_ = 9;
    int scheduleDraftEndMinute_ = 25;
    std::wstring scheduleMessage_;
    bool scheduleMessageIsError_ = false;
    std::wstring rerunTaskMessage_;
    bool rerunTaskMessageIsError_ = false;
    std::wstring whitelistMessage_;
    bool whitelistMessageIsError_ = false;
    std::wstring updateMessage_ = L"正在获取版本信息...";
    std::wstring updateDownloadUrl_;
    std::wstring updateAssetName_ = L"FocusClock.exe";
    std::wstring updateAssetDigest_;
    bool updateCheckStarted_ = false;
    bool updateCheckInProgress_ = false;
    bool updateAvailable_ = false;
    bool updateMessageIsError_ = false;
    bool updateDownloadInProgress_ = false;
    int whitelistScrollOffset_ = 0;
    std::chrono::steady_clock::time_point focusEnd_{};
    std::chrono::steady_clock::time_point whitelistYieldUntil_{};
    int pendingWhitelistIndex_ = -1;
    int whitelistLeft_ = -1;
    int whitelistTop_ = 92;
    int whitelistIconSize_ = kDefaultWhitelistIconSize;
    RECT whitelistBounds_{};
    RECT previousWhitelistBounds_{};
    bool whitelistLayoutDirty_ = false;
    bool whitelistLayoutNeedsRebuild_ = false;
    bool whitelistLayoutSavePending_ = false;
    DWORD whitelistLayoutLastChangeTick_ = 0;
    bool whitelistDragging_ = false;
    bool whitelistDragMoved_ = false;
    POINT whitelistDragStartPt_{};
    POINT whitelistDragStartOrigin_{};
    std::vector<UiButton> buttons_;
    std::vector<WhitelistEntry> whitelistEntries_;
    std::map<HWND, LONG_PTR> promotedWindows_;
    FILETIME whitelistWriteTime_{};
    bool whitelistKnown_ = false;
};
} // namespace focus_clock
