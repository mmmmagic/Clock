#include "FocusClockApp.h"
#include "Res/resource.h"

namespace focus_clock {

FocusClockApp::~FocusClockApp() {
    ReleaseRenderResources();
    DestroyWhitelistIconCache();
}

int FocusClockApp::Run(HINSTANCE instance, int show, long long rerunResumeSeconds) {
    rerunResumeSeconds_ = rerunResumeSeconds;

    GdiplusStartupInput gdiplusStartupInput;
    if (GdiplusStartup(&gdiplusToken_, &gdiplusStartupInput, nullptr) != Ok) {
        MessageBoxW(nullptr, L"无法初始化 GDI+。", L"FocusClock", MB_ICONERROR);
        return 1;
    }

    uiFontFamily_ = std::make_unique<FontFamily>(L"Segoe UI");
    monoFontFamily_ = std::make_unique<FontFamily>(L"Consolas");

    if (!Create(instance, show)) {
        ReleaseRenderResources();
        GdiplusShutdown(gdiplusToken_);
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ReleaseRenderResources();
    GdiplusShutdown(gdiplusToken_);
    return static_cast<int>(msg.wParam);
}

bool FocusClockApp::Create(HINSTANCE instance, int show) {
    instance_ = instance;
    CleanupUpdateArtifacts();
    if (std::none_of(panelTabs_.begin(), panelTabs_.end(), [](const PanelTabDefinition& tab) {
        return tab.id == kPanelTabRerunId;
    })) {
        panelTabs_.push_back(PanelTabDefinition{ kPanelTabRerunId, L"防重启" });
    }

    const wchar_t* className = L"FocusClockWindow";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = FocusClockApp::WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
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
    LoadAppSettings();
    LoadScheduledFocusTasks();
    LoadWhitelistIfNeeded(true);
    LoadWhitelistLayoutSettings();
    ApplyDarkMode();
    EnterFullscreenNotTopmost();
    RebuildLayout();
    ShowWindow(hwnd_, show);
    UpdateWindow(hwnd_);

    RegisterHotKey(hwnd_, kPanelHotkeyId, 0, VK_F6);
    SetTimer(hwnd_, kClockTimer, kClockTimerMs, nullptr);
    SetTimer(hwnd_, kGuardTimer, kGuardTimerMs, nullptr);
    if (rerunResumeSeconds_ > 0) {
        long long rerunTotalSeconds = rerunResumeSeconds_;
        wchar_t buffer[64]{};
        GetPrivateProfileStringW(
            L"FocusSession",
            L"TotalSeconds",
            L"0",
            buffer,
            static_cast<DWORD>(std::size(buffer)),
            GetSettingsPath().c_str());
        long long storedTotalSeconds = _wtoi64(buffer);
        if (storedTotalSeconds <= 0) {
            GetPrivateProfileStringW(
                L"FocusSession",
                L"DurationSeconds",
                L"0",
                buffer,
                static_cast<DWORD>(std::size(buffer)),
                GetSettingsPath().c_str());
            storedTotalSeconds = _wtoi64(buffer);
        }
        if (storedTotalSeconds > 0) {
            rerunTotalSeconds = std::max(rerunResumeSeconds_, storedTotalSeconds);
        }
        StartFocusForSeconds(rerunResumeSeconds_, false, rerunTotalSeconds);
    } else {
        CheckScheduledFocusTasks(true);
    }
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

    case kRerunTaskCommandFinishedMessage:
        HandleRerunTaskCommandResult(wparam, lparam);
        return 0;

    case kUpdateCheckFinishedMessage:
        HandleUpdateCheckResult(reinterpret_cast<ReleaseInfo*>(lparam));
        return 0;

    case kUpdateDownloadFinishedMessage:
        HandleUpdateDownloadResult(reinterpret_cast<UpdateDownloadResult*>(lparam));
        return 0;

    case kUpdateLogMessage: {
        std::unique_ptr<std::wstring> message(reinterpret_cast<std::wstring*>(lparam));
        if (message) {
            AppendUpdateLog(*message, wparam != 0);
        }
        return 0;
    }

    case WM_TIMER:
        if (wparam == kClockTimer) {
            UpdateRemaining();
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (wparam == kGuardTimer) {
            if (LoadWhitelistIfNeeded()) {
                RebuildLayout();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            CheckScheduledFocusTasks();
            if (focusActive_) {
                TryResolvePendingWhitelistWindow();
                bool foregroundWhitelisted = IsWhitelistedForegroundWindow();
                if (HWND bottomWhitelistWindow = PromoteVisibleWhitelistedWindows()) {
                    activeWhitelistWindow_ = bottomWhitelistWindow;
                    EnterFullscreenBelow(activeWhitelistWindow_);
                } else if (foregroundWhitelisted && IsTrackedWhitelistWindowValid()) {
                    EnterFullscreenBelow(activeWhitelistWindow_);
                } else {
                    activeWhitelistWindow_ = nullptr;
                    EnterFullscreenTopmost();
                }
            }
            RefreshTheme();
        } else if (wparam == kWhitelistLayoutTimer) {
            ProcessPendingWhitelistLayout();
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
        if (wparam == VK_F6) {
            if (!focusActive_) {
                TogglePanel();
            } else {
                MessageBeep(MB_ICONINFORMATION);
            }
            return 0;
        }

        if (panelOpen_) {
            if (wparam == VK_ESCAPE) {
                ClosePanel();
            }
            return 0;
        }

        if (IsShiftDown()) {
            bool handled = true;
            switch (wparam) {
            case VK_LEFT:
                MoveWhitelistLayout(-kWhitelistMoveStep, 0);
                break;
            case VK_RIGHT:
                MoveWhitelistLayout(kWhitelistMoveStep, 0);
                break;
            case VK_UP:
                MoveWhitelistLayout(0, -kWhitelistMoveStep);
                break;
            case VK_DOWN:
                MoveWhitelistLayout(0, kWhitelistMoveStep);
                break;
            case VK_OEM_PLUS:
            case VK_ADD:
                ResizeWhitelistLayout(kWhitelistResizeStep);
                break;
            case VK_OEM_MINUS:
            case VK_SUBTRACT:
                ResizeWhitelistLayout(-kWhitelistResizeStep);
                break;
            default:
                handled = false;
                break;
            }
            if (handled) {
                return 0;
            }
        }

        if (wparam == VK_ESCAPE) {
            if (!focusActive_) {
                DestroyWindow(hwnd);
            }
            return 0;
        }

        if (wparam == VK_RETURN) {
            StartFocus();
            return 0;
        }
        return 0;

    case WM_HOTKEY:
        if (wparam == kPanelHotkeyId) {
            if (!focusActive_) {
                TogglePanel();
                ShowWindow(hwnd_, SW_SHOW);
                SetForegroundWindow(hwnd_);
            } else {
                MessageBeep(MB_ICONINFORMATION);
            }
            return 0;
        }
        return 0;

    case WM_SYSKEYDOWN:
        if (wparam == VK_F4 && focusActive_) {
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);

    case WM_LBUTTONDOWN: {
        if (panelOpen_) {
            return 0;
        }
        POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        if (BeginWhitelistDrag(pt)) {
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_MOUSEMOVE: {
        if (whitelistDragging_) {
            POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            UpdateWhitelistDrag(pt);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_LBUTTONUP: {
        POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        if (whitelistDragging_) {
            bool moved = whitelistDragMoved_;
            EndWhitelistDrag();
            if (moved) {
                return 0;
            }
        }
        HitTestAndClick(pt);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (panelOpen_) {
            if (activePanelTab_ == kPanelTabScheduleId) {
                int delta = GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? -48 : 48;
                ScrollScheduleTab(delta);
            } else if (activePanelTab_ == kPanelTabWhitelistId) {
                int delta = GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? -48 : 48;
                ScrollWhitelistTab(delta);
            }
            return 0;
        }
        if (IsShiftDown()) {
            POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            ScreenToClient(hwnd, &pt);
            if (IsPointInWhitelistArea(pt)) {
                int delta = GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? kWhitelistResizeStep : -kWhitelistResizeStep;
                ResizeWhitelistLayout(delta);
                return 0;
            }
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
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
        ProcessPendingWhitelistLayout(true);
        RestorePromotedWhitelistWindows();
        KillTimer(hwnd, kWhitelistLayoutTimer);
        RemoveFocusKeyboardHook();
        UnregisterHotKey(hwnd, kPanelHotkeyId);
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

void FocusClockApp::RefreshTheme() {
    bool next = IsSystemDarkMode();
    if (next != darkMode_) {
        darkMode_ = next;
        ApplyDarkMode();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void FocusClockApp::StartFocus() {
    StartFocusForSeconds(selectedMinutes_ * 60LL);
}

void FocusClockApp::StartFocusForSeconds(long long durationSeconds, bool updateSessionSettings, long long totalSeconds) {
    ClosePanel();
    focusActive_ = true;
    InstallFocusKeyboardHook();
    remainingSeconds_ = std::max(1LL, durationSeconds);
    focusTotalSeconds_ = std::max(remainingSeconds_, totalSeconds > 0 ? totalSeconds : remainingSeconds_);
    focusEnd_ = std::chrono::steady_clock::now() + std::chrono::seconds(remainingSeconds_);
    if (updateSessionSettings) {
        SaveFocusSessionSettings(remainingSeconds_, focusTotalSeconds_);
    }
    EnterFullscreenTopmost();
    RebuildLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void FocusClockApp::FinishFocus() {
    focusActive_ = false;
    ClearFocusSessionSettings();
    RemoveFocusKeyboardHook();
    activeWhitelistWindow_ = nullptr;
    pendingWhitelistIndex_ = -1;
    whitelistYieldUntil_ = std::chrono::steady_clock::time_point{};
    remainingSeconds_ = selectedMinutes_ * 60LL;
    focusTotalSeconds_ = remainingSeconds_;

    RestorePromotedWhitelistWindows();

    MessageBeep(MB_OK);
    EnterFullscreenNotTopmost();
    RebuildLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
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

} // namespace focus_clock
