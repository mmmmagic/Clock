#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <string>
#include <utility>
#include <vector>

using namespace Gdiplus;

namespace {

constexpr UINT_PTR kClockTimer = 1;
constexpr UINT_PTR kGuardTimer = 2;
constexpr int kClockTimerMs = 250;
constexpr int kGuardTimerMs = 1000;
constexpr double kPi = 3.14159265358979323846;
constexpr int kMinFocusMinutes = 1;
constexpr int kMaxFocusMinutes = 240;
constexpr int kStartButtonId = 1001;
constexpr int kExitButtonId = 1002;
constexpr int kCustomMinusFiveId = 1101;
constexpr int kCustomMinusOneId = 1102;
constexpr int kCustomPlusOneId = 1103;
constexpr int kCustomPlusFiveId = 1104;
constexpr int kCustomDisplayId = 1105;
constexpr int kWhitelistButtonBaseId = 2000;

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
    int id = 0;
    bool enabled = true;
    bool selected = false;
    bool primary = false;
    bool danger = false;
    bool iconOnly = false;
};

struct WhitelistEntry {
    std::wstring launchSpec;
    std::wstring normalized;
    std::wstring exeName;
    std::wstring iconPath;
    std::wstring label;
};

class FocusClockApp;

struct FindWindowContext {
    const FocusClockApp* app = nullptr;
    const WhitelistEntry* entry = nullptr;
    HWND found = nullptr;
};

class FocusClockApp {
public:
    int Run(HINSTANCE instance, int show);

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    bool Create(HINSTANCE instance, int show);
    void EnterFullscreenTopmost();
    void EnterFullscreenNotTopmost();
    void EnterFullscreenBelow(HWND aboveWindow);
    void ApplyDarkMode();
    void RefreshTheme();
    bool LoadWhitelistIfNeeded(bool force = false);
    void StartFocus();
    void FinishFocus();
    void RebuildLayout();
    void Paint();
    void DrawBackground(Graphics& g, const RECT& rc);
    void DrawAnalogClock(Graphics& g, const RectF& bounds, const SYSTEMTIME& now);
    void DrawHand(Graphics& g, const PointF& center, float radius, double angleRadians, float width, const Color& color);
    void DrawTextBlock(Graphics& g, const std::wstring& text, const RectF& rect, Font& font, const Color& color, StringAlignment align, StringAlignment lineAlign);
    void DrawButton(Graphics& g, const UiButton& button);
    void DrawButtonIcon(Graphics& g, const UiButton& button, const RectF& bounds);
    void HitTestAndClick(POINT pt);
    void OpenWhitelistEntry(size_t index);
    HWND FindRunningWhitelistWindow(const WhitelistEntry& entry) const;
    void BringWindowToFront(HWND target);
    void PromoteWhitelistWindow(HWND target);
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
    static std::wstring StripExtension(const std::wstring& filename);
    static std::wstring ResolveLaunchPath(const std::wstring& launchSpec);
    static HICON LoadIconForPath(const std::wstring& path);
    static std::wstring DecodeTextFile(const std::vector<char>& bytes);
    static BOOL CALLBACK EnumWhitelistWindows(HWND window, LPARAM param);

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    ULONG_PTR gdiplusToken_ = 0;
    bool darkMode_ = false;
    bool focusActive_ = false;
    HWND activeWhitelistWindow_ = nullptr;
    int selectedMinutes_ = 25;
    long long remainingSeconds_ = 25 * 60;
    std::chrono::steady_clock::time_point focusEnd_{};
    std::chrono::steady_clock::time_point whitelistYieldUntil_{};
    int pendingWhitelistIndex_ = -1;
    std::vector<UiButton> buttons_;
    std::vector<WhitelistEntry> whitelistEntries_;
    FILETIME whitelistWriteTime_{};
    bool whitelistKnown_ = false;
};

int FocusClockApp::Run(HINSTANCE instance, int show) {
    GdiplusStartupInput gdiplusStartupInput;
    if (GdiplusStartup(&gdiplusToken_, &gdiplusStartupInput, nullptr) != Ok) {
        MessageBoxW(nullptr, L"无法初始化 GDI+。", L"FocusClock", MB_ICONERROR);
        return 1;
    }

    if (!Create(instance, show)) {
        GdiplusShutdown(gdiplusToken_);
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    GdiplusShutdown(gdiplusToken_);
    return static_cast<int>(msg.wParam);
}

bool FocusClockApp::Create(HINSTANCE instance, int show) {
    instance_ = instance;
    const wchar_t* className = L"FocusClockWindow";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = FocusClockApp::WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = className;
    wc.hbrBackground = nullptr;

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"无法注册窗口类。", L"FocusClock", MB_ICONERROR);
        return false;
    }

    hwnd_ = CreateWindowExW(
        WS_EX_APPWINDOW,
        className,
        L"FocusClock",
        WS_POPUP,
        0,
        0,
        GetSystemMetrics(SM_CXSCREEN),
        GetSystemMetrics(SM_CYSCREEN),
        nullptr,
        nullptr,
        instance,
        this);

    if (!hwnd_) {
        MessageBoxW(nullptr, L"无法创建窗口。", L"FocusClock", MB_ICONERROR);
        return false;
    }

    RefreshTheme();
    LoadWhitelistIfNeeded(true);
    ApplyDarkMode();
    EnterFullscreenNotTopmost();
    RebuildLayout();
    ShowWindow(hwnd_, show);
    UpdateWindow(hwnd_);

    SetTimer(hwnd_, kClockTimer, kClockTimerMs, nullptr);
    SetTimer(hwnd_, kGuardTimer, kGuardTimerMs, nullptr);
    return true;
}

LRESULT CALLBACK FocusClockApp::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    FocusClockApp* app = nullptr;

    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        app = static_cast<FocusClockApp*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->hwnd_ = hwnd;
    } else {
        app = reinterpret_cast<FocusClockApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (app) {
        return app->HandleMessage(hwnd, msg, wparam, lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT FocusClockApp::HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_CREATE:
        return 0;

    case WM_SIZE:
        RebuildLayout();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_DISPLAYCHANGE:
        if (focusActive_) {
            EnterFullscreenTopmost();
        } else {
            EnterFullscreenNotTopmost();
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        RefreshTheme();
        ApplyDarkMode();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_TIMER:
        if (wparam == kClockTimer) {
            UpdateRemaining();
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (wparam == kGuardTimer) {
            if (LoadWhitelistIfNeeded()) {
                RebuildLayout();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            if (focusActive_) {
                TryResolvePendingWhitelistWindow();
                if ((ShouldYieldToWhitelist() || IsWhitelistedForegroundWindow()) && IsTrackedWhitelistWindowValid()) {
                    EnterFullscreenBelow(activeWhitelistWindow_);
                } else {
                    activeWhitelistWindow_ = nullptr;
                    EnterFullscreenTopmost();
                }
            }
            RefreshTheme();
        }
        return 0;

    case WM_WINDOWPOSCHANGING:
        // Disabled: forcing Z-order here interferes with normal window actions
        // such as minimizing whitelisted applications.
        //
        // if (focusActive_) {
        //     auto* pos = reinterpret_cast<WINDOWPOS*>(lparam);
        //     if (IsTrackedWhitelistWindowValid()) {
        //         pos->hwndInsertAfter = activeWhitelistWindow_;
        //     } else {
        //         pos->hwndInsertAfter = HWND_TOPMOST;
        //     }
        //     pos->flags &= ~SWP_NOZORDER;
        // }
        return DefWindowProcW(hwnd, msg, wparam, lparam);

    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) {
            if (!focusActive_) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        return 0;

    case WM_SYSKEYDOWN:
        if (wparam == VK_F4 && focusActive_) {
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);

    case WM_LBUTTONUP: {
        POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        HitTestAndClick(pt);
        return 0;
    }

    case WM_PAINT:
        Paint();
        return 0;

    case WM_CLOSE:
        if (focusActive_) {
            MessageBeep(MB_ICONINFORMATION);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, kClockTimer);
        KillTimer(hwnd, kGuardTimer);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void FocusClockApp::EnterFullscreenTopmost() {
    HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);

    SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_APPWINDOW);
    SetWindowPos(
        hwnd_,
        HWND_TOPMOST,
        info.rcMonitor.left,
        info.rcMonitor.top,
        info.rcMonitor.right - info.rcMonitor.left,
        info.rcMonitor.bottom - info.rcMonitor.top,
        SWP_SHOWWINDOW | SWP_FRAMECHANGED);
}

void FocusClockApp::EnterFullscreenNotTopmost() {
    HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);

    SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, WS_EX_APPWINDOW);
    SetWindowPos(
        hwnd_,
        HWND_NOTOPMOST,
        info.rcMonitor.left,
        info.rcMonitor.top,
        info.rcMonitor.right - info.rcMonitor.left,
        info.rcMonitor.bottom - info.rcMonitor.top,
        SWP_SHOWWINDOW | SWP_FRAMECHANGED);
}

void FocusClockApp::EnterFullscreenBelow(HWND aboveWindow) {
    HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);

    SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_APPWINDOW);
    SetWindowPos(
        hwnd_,
        aboveWindow,
        info.rcMonitor.left,
        info.rcMonitor.top,
        info.rcMonitor.right - info.rcMonitor.left,
        info.rcMonitor.bottom - info.rcMonitor.top,
        SWP_SHOWWINDOW | SWP_FRAMECHANGED | SWP_NOACTIVATE);
}

void FocusClockApp::ApplyDarkMode() {
    BOOL enabled = darkMode_ ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_, kDwmwaUseImmersiveDarkMode, &enabled, sizeof(enabled));
}

bool FocusClockApp::LoadWhitelistIfNeeded(bool force) {
    std::wstring path = GetExecutableDirectory() + L"\\Whitelist.txt";

    WIN32_FILE_ATTRIBUTE_DATA data{};
    bool exists = GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) != FALSE;
    if (!exists) {
        if (force || whitelistKnown_) {
            whitelistEntries_.clear();
            whitelistWriteTime_ = FILETIME{};
            whitelistKnown_ = false;
            return true;
        }
        return false;
    }

    if (!force && whitelistKnown_ && CompareFileTime(&data.ftLastWriteTime, &whitelistWriteTime_) == 0) {
        return false;
    }

    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart > 1024 * 1024) {
        CloseHandle(file);
        return false;
    }

    std::vector<char> bytes(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    if (!bytes.empty()) {
        ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
        bytes.resize(read);
    }
    CloseHandle(file);

    std::wstring text = DecodeTextFile(bytes);
    std::vector<WhitelistEntry> entries;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find_first_of(L"\r\n", start);
        std::wstring line = Trim(text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
        if (!line.empty() && line[0] != L'#' && line[0] != L';') {
            if (line.size() >= 2 && line.front() == L'"' && line.back() == L'"') {
                line = Trim(line.substr(1, line.size() - 2));
            }
            if (!line.empty()) {
                WhitelistEntry entry{};
                entry.launchSpec = line;
                entry.normalized = ToLower(line);
                entry.exeName = BaseName(entry.normalized);
                entry.iconPath = ResolveLaunchPath(line);
                entry.label = StripExtension(BaseName(line));
                if (entry.label.empty()) {
                    entry.label = line;
                }
                entries.push_back(std::move(entry));
            }
        }

        if (end == std::wstring::npos) {
            break;
        }
        start = text.find_first_not_of(L"\r\n", end);
        if (start == std::wstring::npos) {
            break;
        }
    }

    whitelistEntries_ = std::move(entries);
    whitelistWriteTime_ = data.ftLastWriteTime;
    whitelistKnown_ = true;
    return true;
}

void FocusClockApp::RefreshTheme() {
    bool next = IsSystemDarkMode();
    if (next != darkMode_) {
        darkMode_ = next;
        ApplyDarkMode();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void FocusClockApp::StartFocus() {
    focusActive_ = true;
    remainingSeconds_ = selectedMinutes_ * 60LL;
    focusEnd_ = std::chrono::steady_clock::now() + std::chrono::seconds(remainingSeconds_);
    EnterFullscreenTopmost();
    RebuildLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void FocusClockApp::FinishFocus() {
    focusActive_ = false;
    activeWhitelistWindow_ = nullptr;
    pendingWhitelistIndex_ = -1;
    whitelistYieldUntil_ = std::chrono::steady_clock::time_point{};
    remainingSeconds_ = selectedMinutes_ * 60LL;
    MessageBeep(MB_OK);
    EnterFullscreenNotTopmost();
    RebuildLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void FocusClockApp::RebuildLayout() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    buttons_.clear();

    const int buttonHeight = 64;
    const int gap = 18;
    const int bottom = height - 86;
    const int durationWidth = 132;
    const std::array<int, 4> minutes{ 25, 45, 60, 90 };

    int totalWidth = static_cast<int>(minutes.size()) * durationWidth + static_cast<int>(minutes.size() - 1) * gap;
    int startX = (width - totalWidth) / 2;

    if (!focusActive_) {
        for (size_t i = 0; i < minutes.size(); ++i) {
            int left = startX + static_cast<int>(i) * (durationWidth + gap);
            UiButton b{};
            b.rect = RECT{ left, bottom - buttonHeight - 178, left + durationWidth, bottom - 178 };
            b.label = std::to_wstring(minutes[i]) + L" 分钟";
            b.id = minutes[i];
            b.selected = selectedMinutes_ == minutes[i];
            buttons_.push_back(b);
        }

        const int customSmallWidth = 74;
        const int customDisplayWidth = 218;
        const int customTotalWidth = customSmallWidth * 4 + customDisplayWidth + gap * 4;
        int customLeft = (width - customTotalWidth) / 2;
        int customTop = bottom - buttonHeight - 88;

        const std::array<std::pair<int, const wchar_t*>, 5> customButtons{ {
            { kCustomMinusFiveId, L"-5" },
            { kCustomMinusOneId, L"-1" },
            { kCustomDisplayId, L"自定义 " },
            { kCustomPlusOneId, L"+1" },
            { kCustomPlusFiveId, L"+5" },
        } };

        for (size_t i = 0; i < customButtons.size(); ++i) {
            int buttonWidth = (customButtons[i].first == kCustomDisplayId) ? customDisplayWidth : customSmallWidth;
            UiButton custom{};
            custom.rect = RECT{ customLeft, customTop, customLeft + buttonWidth, customTop + buttonHeight };
            custom.id = customButtons[i].first;
            custom.label = customButtons[i].second;
            if (custom.id == kCustomDisplayId) {
                custom.label += std::to_wstring(selectedMinutes_) + L" 分钟";
                custom.selected = true;
                custom.enabled = false;
            } else if (custom.id == kCustomMinusFiveId || custom.id == kCustomMinusOneId) {
                custom.enabled = selectedMinutes_ > kMinFocusMinutes;
            } else {
                custom.enabled = selectedMinutes_ < kMaxFocusMinutes;
            }
            buttons_.push_back(custom);
            customLeft += buttonWidth + gap;
        }

        UiButton start{};
        start.rect = RECT{ width / 2 - 150, bottom - buttonHeight, width / 2 + 150, bottom };
        start.label = L"开始专注";
        start.id = kStartButtonId;
        start.primary = true;
        buttons_.push_back(start);

        UiButton exit{};
        exit.rect = RECT{ width - 172, height - 70, width - 32, height - 26 };
        exit.label = L"退出";
        exit.id = kExitButtonId;
        exit.danger = true;
        buttons_.push_back(exit);
    }

    const int appButtonSize = 72;
    const int appColumns = 3;
    const int appGap = 14;
    const int appGridWidth = appColumns * appButtonSize + (appColumns - 1) * appGap;
    const int appLeft = width - appGridWidth - 36;
    const int appTop = 92;
    const int appBottomLimit = height - 112;
    const int appRows = std::max(0, (appBottomLimit - appTop - 40) / (appButtonSize + appGap));
    const int appCapacity = std::max(0, std::min(12, appRows * appColumns));
    const int appCount = std::min(static_cast<int>(whitelistEntries_.size()), appCapacity);

    for (int i = 0; i < appCount; ++i) {
        int row = i / appColumns;
        int column = i % appColumns;
        int left = appLeft + column * (appButtonSize + appGap);
        int top = appTop + 40 + row * (appButtonSize + appGap);
        UiButton app{};
        app.rect = RECT{ left, top, left + appButtonSize, top + appButtonSize };
        app.label = whitelistEntries_[i].label;
        app.iconPath = whitelistEntries_[i].iconPath;
        app.id = kWhitelistButtonBaseId + i;
        app.primary = focusActive_;
        app.iconOnly = true;
        buttons_.push_back(app);
    }
}

void FocusClockApp::Paint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ oldBitmap = SelectObject(mem, bitmap);

    Graphics g(mem);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    DrawBackground(g, rc);

    Theme theme{
        darkMode_ ? Color(255, 11, 15, 20) : Color(255, 245, 247, 250),
        darkMode_ ? Color(255, 24, 30, 39) : Color(255, 255, 255, 255),
        darkMode_ ? Color(255, 242, 245, 247) : Color(255, 24, 28, 35),
        darkMode_ ? Color(255, 178, 187, 199) : Color(255, 79, 88, 103),
        darkMode_ ? Color(255, 119, 131, 146) : Color(255, 104, 116, 132),
        darkMode_ ? Color(255, 89, 180, 255) : Color(255, 0, 113, 188),
        darkMode_ ? Color(50, 89, 180, 255) : Color(32, 0, 113, 188),
        darkMode_ ? Color(255, 255, 115, 115) : Color(255, 190, 46, 46),
        darkMode_ ? Color(255, 54, 64, 78) : Color(255, 214, 220, 228)
    };

    SYSTEMTIME now{};
    GetLocalTime(&now);

    int clockSize = std::min(width, height) / 3;
    clockSize = std::max(220, std::min(clockSize, 390));
    RectF clockRect(
        static_cast<REAL>((width - clockSize) / 2),
        static_cast<REAL>(height * 0.12),
        static_cast<REAL>(clockSize),
        static_cast<REAL>(clockSize));
    DrawAnalogClock(g, clockRect, now);

    FontFamily family(L"Segoe UI");
    FontFamily monoFamily(L"Consolas");

    REAL digitalSize = static_cast<REAL>(std::max(56, std::min(132, width / 10)));
    Font digitalFont(&monoFamily, digitalSize, FontStyleRegular, UnitPixel);
    Font dateFont(&family, static_cast<REAL>(std::max(20, std::min(34, width / 48))), FontStyleRegular, UnitPixel);
    Font statusFont(&family, static_cast<REAL>(std::max(22, std::min(40, width / 42))), FontStyleRegular, UnitPixel);
    Font remainFont(&monoFamily, static_cast<REAL>(std::max(48, std::min(96, width / 14))), FontStyleRegular, UnitPixel);

    RectF digitalRect(0, static_cast<REAL>(height * 0.52), static_cast<REAL>(width), digitalSize + 18);
    DrawTextBlock(g, FormatTime(now), digitalRect, digitalFont, theme.primaryText, StringAlignmentCenter, StringAlignmentCenter);

    RectF dateRect(0, digitalRect.Y + digitalRect.Height + 4, static_cast<REAL>(width), 48);
    DrawTextBlock(g, FormatDate(now), dateRect, dateFont, theme.secondaryText, StringAlignmentCenter, StringAlignmentCenter);

    if (focusActive_) {
        RectF remainRect(0, dateRect.Y + 56, static_cast<REAL>(width), 92);
        DrawTextBlock(g, FormatRemaining(), remainRect, remainFont, theme.accent, StringAlignmentCenter, StringAlignmentCenter);

        RectF statusRect(0, remainRect.Y + remainRect.Height + 8, static_cast<REAL>(width), 52);
        DrawTextBlock(g, L"专注进行中", statusRect, statusFont, theme.secondaryText, StringAlignmentCenter, StringAlignmentCenter);

        Font hintFont(&family, 18, FontStyleRegular, UnitPixel);
        RectF hintRect(0, static_cast<REAL>(height - 70), static_cast<REAL>(width), 28);
        DrawTextBlock(g, L"专注结束前会保持全屏和置顶", hintRect, hintFont, theme.mutedText, StringAlignmentCenter, StringAlignmentCenter);
    } else {
        Font hintFont(&family, 20, FontStyleRegular, UnitPixel);
        RectF hintRect(0, dateRect.Y + 58, static_cast<REAL>(width), 34);
        DrawTextBlock(g, L"选择时长后开始", hintRect, hintFont, theme.mutedText, StringAlignmentCenter, StringAlignmentCenter);
    }

    auto appButton = std::find_if(buttons_.begin(), buttons_.end(), [](const UiButton& button) {
        return button.id >= 2000;
    });
    if (appButton != buttons_.end()) {
        Font appTitleFont(&family, 18, FontStyleRegular, UnitPixel);
        RectF titleRect(
            static_cast<REAL>(appButton->rect.left),
            static_cast<REAL>(appButton->rect.top - 38),
            static_cast<REAL>(width - appButton->rect.left - 36),
            28.0f);
        DrawTextBlock(g, L"白名单程序", titleRect, appTitleFont, theme.secondaryText, StringAlignmentNear, StringAlignmentCenter);
    }

    for (const auto& button : buttons_) {
        DrawButton(g, button);
    }

    BitBlt(hdc, 0, 0, width, height, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(mem);
    EndPaint(hwnd_, &ps);
}

void FocusClockApp::DrawBackground(Graphics& g, const RECT& rc) {
    Color top = darkMode_ ? Color(255, 9, 13, 18) : Color(255, 241, 245, 249);
    Color bottom = darkMode_ ? Color(255, 18, 24, 31) : Color(255, 255, 255, 255);
    LinearGradientBrush brush(
        Point(0, rc.top),
        Point(0, rc.bottom),
        top,
        bottom);
    g.FillRectangle(
        &brush,
        static_cast<INT>(rc.left),
        static_cast<INT>(rc.top),
        static_cast<INT>(rc.right - rc.left),
        static_cast<INT>(rc.bottom - rc.top));
}

void FocusClockApp::DrawAnalogClock(Graphics& g, const RectF& bounds, const SYSTEMTIME& now) {
    Theme theme{
        darkMode_ ? Color(255, 11, 15, 20) : Color(255, 245, 247, 250),
        darkMode_ ? Color(255, 24, 30, 39) : Color(255, 255, 255, 255),
        darkMode_ ? Color(255, 242, 245, 247) : Color(255, 24, 28, 35),
        darkMode_ ? Color(255, 178, 187, 199) : Color(255, 79, 88, 103),
        darkMode_ ? Color(255, 119, 131, 146) : Color(255, 104, 116, 132),
        darkMode_ ? Color(255, 89, 180, 255) : Color(255, 0, 113, 188),
        darkMode_ ? Color(50, 89, 180, 255) : Color(32, 0, 113, 188),
        darkMode_ ? Color(255, 255, 115, 115) : Color(255, 190, 46, 46),
        darkMode_ ? Color(255, 54, 64, 78) : Color(255, 214, 220, 228)
    };

    PointF center(bounds.X + bounds.Width / 2.0f, bounds.Y + bounds.Height / 2.0f);
    float radius = bounds.Width / 2.0f;

    SolidBrush faceBrush(darkMode_ ? Color(235, 18, 24, 32) : Color(242, 255, 255, 255));
    Pen rimPen(theme.line, 2.0f);
    g.FillEllipse(&faceBrush, bounds);
    g.DrawEllipse(&rimPen, bounds);

    Pen tickPen(theme.secondaryText, 2.0f);
    Pen hourTickPen(theme.primaryText, 4.0f);
    for (int i = 0; i < 60; ++i) {
        double angle = (i / 60.0) * 2.0 * kPi - kPi / 2.0;
        float outer = radius - 16.0f;
        float inner = outer - ((i % 5 == 0) ? 16.0f : 8.0f);
        PointF p1(center.X + std::cos(angle) * inner, center.Y + std::sin(angle) * inner);
        PointF p2(center.X + std::cos(angle) * outer, center.Y + std::sin(angle) * outer);
        g.DrawLine(i % 5 == 0 ? &hourTickPen : &tickPen, p1, p2);
    }

    double second = now.wSecond + now.wMilliseconds / 1000.0;
    double minute = now.wMinute + second / 60.0;
    double hour = (now.wHour % 12) + minute / 60.0;

    DrawHand(g, center, radius * 0.50f, hour / 12.0 * 2.0 * kPi - kPi / 2.0, 7.0f, theme.primaryText);
    DrawHand(g, center, radius * 0.70f, minute / 60.0 * 2.0 * kPi - kPi / 2.0, 5.0f, theme.primaryText);
    DrawHand(g, center, radius * 0.78f, second / 60.0 * 2.0 * kPi - kPi / 2.0, 2.0f, theme.danger);

    SolidBrush centerBrush(theme.accent);
    g.FillEllipse(&centerBrush, center.X - 6.0f, center.Y - 6.0f, 12.0f, 12.0f);
}

void FocusClockApp::DrawHand(Graphics& g, const PointF& center, float radius, double angleRadians, float width, const Color& color) {
    Pen pen(color, width);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    PointF end(center.X + std::cos(angleRadians) * radius, center.Y + std::sin(angleRadians) * radius);
    g.DrawLine(&pen, center, end);
}

void FocusClockApp::DrawTextBlock(Graphics& g, const std::wstring& text, const RectF& rect, Font& font, const Color& color, StringAlignment align, StringAlignment lineAlign) {
    StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(lineAlign);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    SolidBrush brush(color);
    g.DrawString(text.c_str(), static_cast<INT>(text.size()), &font, rect, &format, &brush);
}

void FocusClockApp::DrawButton(Graphics& g, const UiButton& button) {
    Theme theme{
        darkMode_ ? Color(255, 11, 15, 20) : Color(255, 245, 247, 250),
        darkMode_ ? Color(255, 24, 30, 39) : Color(255, 255, 255, 255),
        darkMode_ ? Color(255, 242, 245, 247) : Color(255, 24, 28, 35),
        darkMode_ ? Color(255, 178, 187, 199) : Color(255, 79, 88, 103),
        darkMode_ ? Color(255, 119, 131, 146) : Color(255, 104, 116, 132),
        darkMode_ ? Color(255, 89, 180, 255) : Color(255, 0, 113, 188),
        darkMode_ ? Color(50, 89, 180, 255) : Color(32, 0, 113, 188),
        darkMode_ ? Color(255, 255, 115, 115) : Color(255, 190, 46, 46),
        darkMode_ ? Color(255, 54, 64, 78) : Color(255, 214, 220, 228)
    };

    RectF r(
        static_cast<REAL>(button.rect.left),
        static_cast<REAL>(button.rect.top),
        static_cast<REAL>(button.rect.right - button.rect.left),
        static_cast<REAL>(button.rect.bottom - button.rect.top));

    Color fill = theme.panel;
    Color border = theme.line;
    Color text = theme.primaryText;

    if (button.primary) {
        fill = theme.accent;
        border = theme.accent;
        text = Color(255, 255, 255, 255);
    } else if (button.selected) {
        fill = theme.accentSoft;
        border = theme.accent;
        text = theme.primaryText;
    } else if (button.danger) {
        text = theme.danger;
        border = Color(120, theme.danger.GetR(), theme.danger.GetG(), theme.danger.GetB());
    }

    if (!button.enabled) {
        text = theme.secondaryText;
        border = theme.line;
        if (!button.selected) {
            fill = darkMode_ ? Color(160, 24, 30, 39) : Color(170, 245, 247, 250);
        }
    }

    GraphicsPath path;
    const REAL radius = 8.0f;
    path.AddArc(r.X, r.Y, radius * 2, radius * 2, 180, 90);
    path.AddArc(r.X + r.Width - radius * 2, r.Y, radius * 2, radius * 2, 270, 90);
    path.AddArc(r.X + r.Width - radius * 2, r.Y + r.Height - radius * 2, radius * 2, radius * 2, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - radius * 2, radius * 2, radius * 2, 90, 90);
    path.CloseFigure();

    SolidBrush fillBrush(fill);
    Pen borderPen(border, 1.5f);
    g.FillPath(&fillBrush, &path);
    g.DrawPath(&borderPen, &path);

    if (button.iconOnly) {
        DrawButtonIcon(g, button, r);
        return;
    }

    FontFamily family(L"Segoe UI");
    int buttonHeight = static_cast<int>(button.rect.bottom - button.rect.top);
    REAL fontSize = static_cast<REAL>(std::max(18, std::min(24, buttonHeight / 3)));
    Font font(&family, fontSize, FontStyleRegular, UnitPixel);
    DrawTextBlock(g, button.label, r, font, text, StringAlignmentCenter, StringAlignmentCenter);
}

void FocusClockApp::DrawButtonIcon(Graphics& g, const UiButton& button, const RectF& bounds) {
    HICON icon = LoadIconForPath(button.iconPath);
    if (!icon) {
        return;
    }

    int iconSize = static_cast<int>(std::min(bounds.Width, bounds.Height) * 0.62f);
    iconSize = std::max(28, std::min(iconSize, 48));
    int x = static_cast<int>(bounds.X + (bounds.Width - iconSize) / 2.0f);
    int y = static_cast<int>(bounds.Y + (bounds.Height - iconSize) / 2.0f);

    g.Flush();
    HDC hdc = g.GetHDC();
    DrawIconEx(hdc, x, y, icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
    g.ReleaseHDC(hdc);
    DestroyIcon(icon);
}

void FocusClockApp::HitTestAndClick(POINT pt) {
    for (const auto& button : buttons_) {
        if (!button.enabled || !PtInRect(&button.rect, pt)) {
            continue;
        }

        if (button.id == kStartButtonId) {
            StartFocus();
        } else if (button.id == kExitButtonId) {
            DestroyWindow(hwnd_);
        } else if (button.id == kCustomMinusFiveId) {
            selectedMinutes_ = std::max(kMinFocusMinutes, selectedMinutes_ - 5);
            remainingSeconds_ = selectedMinutes_ * 60LL;
            RebuildLayout();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (button.id == kCustomMinusOneId) {
            selectedMinutes_ = std::max(kMinFocusMinutes, selectedMinutes_ - 1);
            remainingSeconds_ = selectedMinutes_ * 60LL;
            RebuildLayout();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (button.id == kCustomPlusOneId) {
            selectedMinutes_ = std::min(kMaxFocusMinutes, selectedMinutes_ + 1);
            remainingSeconds_ = selectedMinutes_ * 60LL;
            RebuildLayout();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (button.id == kCustomPlusFiveId) {
            selectedMinutes_ = std::min(kMaxFocusMinutes, selectedMinutes_ + 5);
            remainingSeconds_ = selectedMinutes_ * 60LL;
            RebuildLayout();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (button.id >= kWhitelistButtonBaseId) {
            OpenWhitelistEntry(static_cast<size_t>(button.id - kWhitelistButtonBaseId));
        } else {
            selectedMinutes_ = std::clamp(button.id, kMinFocusMinutes, kMaxFocusMinutes);
            remainingSeconds_ = selectedMinutes_ * 60LL;
            RebuildLayout();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
}

void FocusClockApp::OpenWhitelistEntry(size_t index) {
    if (index >= whitelistEntries_.size()) {
        return;
    }

    const WhitelistEntry& entry = whitelistEntries_[index];
    pendingWhitelistIndex_ = static_cast<int>(index);
    whitelistYieldUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(12);

    HWND running = FindRunningWhitelistWindow(entry);
    if (running) {
        pendingWhitelistIndex_ = -1;
        activeWhitelistWindow_ = running;
        BringWindowToFront(running);
        EnterFullscreenBelow(running);
        return;
    }

    HINSTANCE result = ShellExecuteW(hwnd_, L"open", entry.launchSpec.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<intptr_t>(result) <= 32) {
        MessageBoxW(hwnd_, L"无法启动该白名单程序，请检查 Whitelist.txt 中的路径。", L"FocusClock", MB_ICONWARNING);
        whitelistYieldUntil_ = std::chrono::steady_clock::time_point{};
        pendingWhitelistIndex_ = -1;
        activeWhitelistWindow_ = nullptr;
        EnterFullscreenTopmost();
    }
}

HWND FocusClockApp::FindRunningWhitelistWindow(const WhitelistEntry& entry) const {
    FindWindowContext context{};
    context.app = this;
    context.entry = &entry;
    EnumWindows(EnumWhitelistWindows, reinterpret_cast<LPARAM>(&context));
    return context.found;
}

void FocusClockApp::BringWindowToFront(HWND target) {
    if (!target) {
        return;
    }

    PromoteWhitelistWindow(target);

    if (IsIconic(target)) {
        ShowWindow(target, SW_RESTORE);
    } else {
        ShowWindow(target, SW_SHOWNORMAL);
    }

    SetWindowPos(target, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(target);
}

void FocusClockApp::PromoteWhitelistWindow(HWND target) {
    if (!target) {
        return;
    }

    LONG_PTR exStyle = GetWindowLongPtrW(target, GWL_EXSTYLE);
    SetWindowLongPtrW(target, GWL_EXSTYLE, exStyle | WS_EX_TOPMOST);
    SetWindowPos(target, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

bool FocusClockApp::IsTrackedWhitelistWindowValid() const {
    return activeWhitelistWindow_ &&
        IsWindow(activeWhitelistWindow_) &&
        IsWindowVisible(activeWhitelistWindow_) &&
        !IsIconic(activeWhitelistWindow_) &&
        activeWhitelistWindow_ != hwnd_;
}

bool FocusClockApp::TryResolvePendingWhitelistWindow() {
    if (pendingWhitelistIndex_ < 0 || pendingWhitelistIndex_ >= static_cast<int>(whitelistEntries_.size())) {
        return false;
    }

    if (!ShouldYieldToWhitelist()) {
        pendingWhitelistIndex_ = -1;
        return false;
    }

    HWND running = FindRunningWhitelistWindow(whitelistEntries_[static_cast<size_t>(pendingWhitelistIndex_)]);
    if (!running) {
        return false;
    }

    pendingWhitelistIndex_ = -1;
    activeWhitelistWindow_ = running;
    BringWindowToFront(running);
    return true;
}

void FocusClockApp::UpdateRemaining() {
    if (!focusActive_) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    if (now >= focusEnd_) {
        FinishFocus();
        return;
    }

    remainingSeconds_ = std::chrono::duration_cast<std::chrono::seconds>(focusEnd_ - now).count();
}

std::wstring FocusClockApp::FormatTime(const SYSTEMTIME& now) const {
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%02u:%02u:%02u", now.wHour, now.wMinute, now.wSecond);
    return buffer;
}

std::wstring FocusClockApp::FormatDate(const SYSTEMTIME& now) const {
    static const std::array<const wchar_t*, 7> weekdays{
        L"星期日", L"星期一", L"星期二", L"星期三", L"星期四", L"星期五", L"星期六"
    };

    wchar_t buffer[96]{};
    swprintf_s(buffer, L"%04u 年 %02u 月 %02u 日  %s", now.wYear, now.wMonth, now.wDay, weekdays[now.wDayOfWeek]);
    return buffer;
}

std::wstring FocusClockApp::FormatRemaining() const {
    long long seconds = std::max(0LL, remainingSeconds_);
    long long minutes = seconds / 60;
    long long secs = seconds % 60;

    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%02lld:%02lld", minutes, secs);
    return buffer;
}

bool FocusClockApp::IsSystemDarkMode() const {
    DWORD value = 1;
    DWORD size = sizeof(value);
    LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size);

    return status == ERROR_SUCCESS && value == 0;
}

bool FocusClockApp::IsWhitelistedForegroundWindow() {
    HWND foreground = GetForegroundWindow();
    if (!foreground || foreground == hwnd_) {
        return false;
    }

    std::wstring path = GetProcessImagePath(foreground);
    if (!path.empty() && IsExecutableWhitelisted(path)) {
        activeWhitelistWindow_ = foreground;
        PromoteWhitelistWindow(foreground);
        return true;
    }

    return false;
}

bool FocusClockApp::IsExecutableWhitelisted(const std::wstring& path) const {
    std::wstring normalizedPath = ToLower(path);
    std::wstring exeName = BaseName(normalizedPath);

    for (const auto& entry : whitelistEntries_) {
        if (entry.normalized == normalizedPath || entry.exeName == exeName) {
            return true;
        }
    }
    return false;
}

bool FocusClockApp::ShouldYieldToWhitelist() const {
    return whitelistYieldUntil_ != std::chrono::steady_clock::time_point{} &&
        std::chrono::steady_clock::now() < whitelistYieldUntil_;
}

std::wstring FocusClockApp::GetExecutableDirectory() const {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (length == buffer.size()) {
        buffer.resize(buffer.size() * 2);
        length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }

    buffer.resize(length);
    size_t slash = buffer.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return L".";
    }
    return buffer.substr(0, slash);
}

std::wstring FocusClockApp::GetProcessImagePath(HWND window) const {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (!pid) {
        return L"";
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return L"";
    }

    std::wstring path(32768, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    BOOL ok = QueryFullProcessImageNameW(process, 0, path.data(), &size);
    CloseHandle(process);

    if (!ok || size == 0) {
        return L"";
    }

    path.resize(size);
    return path;
}

std::wstring FocusClockApp::ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::wstring FocusClockApp::Trim(std::wstring value) {
    size_t first = 0;
    while (first < value.size() && std::iswspace(value[first])) {
        ++first;
    }

    size_t last = value.size();
    while (last > first && std::iswspace(value[last - 1])) {
        --last;
    }

    return value.substr(first, last - first);
}

std::wstring FocusClockApp::BaseName(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

std::wstring FocusClockApp::StripExtension(const std::wstring& filename) {
    size_t dot = filename.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0) {
        return filename;
    }
    return filename.substr(0, dot);
}

std::wstring FocusClockApp::ResolveLaunchPath(const std::wstring& launchSpec) {
    std::wstring expanded = launchSpec;
    DWORD needed = ExpandEnvironmentStringsW(launchSpec.c_str(), nullptr, 0);
    if (needed > 0) {
        std::wstring buffer(needed, L'\0');
        DWORD written = ExpandEnvironmentStringsW(launchSpec.c_str(), buffer.data(), needed);
        if (written > 0 && written <= needed) {
            buffer.resize(written - 1);
            expanded = buffer;
        }
    }

    if (expanded.find_first_of(L"\\/") != std::wstring::npos) {
        return expanded;
    }

    DWORD length = SearchPathW(nullptr, expanded.c_str(), nullptr, 0, nullptr, nullptr);
    if (length == 0) {
        return expanded;
    }

    std::wstring resolved(length, L'\0');
    DWORD written = SearchPathW(nullptr, expanded.c_str(), nullptr, length, resolved.data(), nullptr);
    if (written == 0 || written >= length) {
        return expanded;
    }

    resolved.resize(written);
    return resolved;
}

HICON FocusClockApp::LoadIconForPath(const std::wstring& path) {
    SHFILEINFOW info{};
    DWORD_PTR ok = SHGetFileInfoW(
        path.c_str(),
        FILE_ATTRIBUTE_NORMAL,
        &info,
        sizeof(info),
        SHGFI_ICON | SHGFI_LARGEICON);

    if (!ok || !info.hIcon) {
        ok = SHGetFileInfoW(
            path.c_str(),
            FILE_ATTRIBUTE_NORMAL,
            &info,
            sizeof(info),
            SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES);
    }

    if (ok && info.hIcon) {
        return info.hIcon;
    }

    return CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
}

std::wstring FocusClockApp::DecodeTextFile(const std::vector<char>& bytes) {
    if (bytes.empty()) {
        return L"";
    }

    UINT codePage = CP_UTF8;
    int offset = 0;
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        offset = 3;
    }

    int wideLength = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, bytes.data() + offset, static_cast<int>(bytes.size() - offset), nullptr, 0);
    if (wideLength == 0) {
        codePage = CP_ACP;
        wideLength = MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    }

    if (wideLength == 0) {
        return L"";
    }

    std::wstring text(static_cast<size_t>(wideLength), L'\0');
    MultiByteToWideChar(codePage, codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, bytes.data() + (codePage == CP_UTF8 ? offset : 0), static_cast<int>(bytes.size() - (codePage == CP_UTF8 ? offset : 0)), text.data(), wideLength);
    return text;
}

BOOL CALLBACK FocusClockApp::EnumWhitelistWindows(HWND window, LPARAM param) {
    auto* context = reinterpret_cast<FindWindowContext*>(param);
    if (!context || !context->app || !context->entry || !window || window == context->app->hwnd_) {
        return TRUE;
    }

    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER)) {
        return TRUE;
    }

    std::wstring path = context->app->GetProcessImagePath(window);
    if (path.empty()) {
        return TRUE;
    }

    std::wstring normalizedPath = ToLower(path);
    std::wstring exeName = BaseName(normalizedPath);
    if (normalizedPath == context->entry->normalized || exeName == context->entry->exeName) {
        context->found = window;
        return FALSE;
    }

    return TRUE;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware();

    FocusClockApp app;
    return app.Run(instance, show);
}
