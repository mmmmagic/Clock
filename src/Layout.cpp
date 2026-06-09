#include "FocusClockApp.h"

namespace focus_clock {

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
            b.enabled = minutes[i] <= maxFocusMinutes_;
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
                custom.enabled = selectedMinutes_ < maxFocusMinutes_;
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

    if (panelOpen_ && !focusActive_) {
        int panelWidth = std::min(760, std::max(520, width - 96));
        int panelHeight = std::min(560, std::max(420, height - 120));
        int left = (width - panelWidth) / 2;
        int top = (height - panelHeight) / 2;
        panelBounds_ = RECT{ left, top, left + panelWidth, top + panelHeight };

        UiButton close{};
        close.rect = RECT{ panelBounds_.right - 58, panelBounds_.top + 24, panelBounds_.right - 24, panelBounds_.top + 58 };
        close.label = L"×";
        close.id = kPanelCloseButtonId;
        close.danger = true;
        buttons_.push_back(close);

        int tabLeft = panelBounds_.left + 32;
        int tabTop = panelBounds_.top + 84;
        for (size_t i = 0; i < panelTabs_.size(); ++i) {
            UiButton tab{};
            tab.rect = RECT{ tabLeft, tabTop + static_cast<int>(i) * 52, tabLeft + 148, tabTop + static_cast<int>(i) * 52 + 40 };
            tab.label = panelTabs_[i].title;
            tab.id = kPanelTabButtonBaseId + panelTabs_[i].id;
            tab.selected = activePanelTab_ == panelTabs_[i].id;
            buttons_.push_back(tab);
        }

        int contentLeft = panelBounds_.left + 216;
        if (activePanelTab_ == kPanelTabScheduleId) {
            scheduleScrollOffset_ = std::clamp(scheduleScrollOffset_, 0, ScheduleContentMaxScroll());
            int scrollY = scheduleScrollOffset_;
            AddTimeStepperButtons(kScheduleStartHourMinusId, kScheduleStartHourDisplayId, kScheduleStartHourPlusId, contentLeft, panelBounds_.top + 214 - scrollY, scheduleDraftStartHour_, 23, L" 时");
            AddTimeStepperButtons(kScheduleStartMinuteMinusId, kScheduleStartMinuteDisplayId, kScheduleStartMinutePlusId, contentLeft + 236, panelBounds_.top + 214 - scrollY, scheduleDraftStartMinute_, 59, L" 分");
            AddTimeStepperButtons(kScheduleEndHourMinusId, kScheduleEndHourDisplayId, kScheduleEndHourPlusId, contentLeft, panelBounds_.top + 316 - scrollY, scheduleDraftEndHour_, 23, L" 时");
            AddTimeStepperButtons(kScheduleEndMinuteMinusId, kScheduleEndMinuteDisplayId, kScheduleEndMinutePlusId, contentLeft + 236, panelBounds_.top + 316 - scrollY, scheduleDraftEndMinute_, 59, L" 分");

            AddPanelButton(
                kScheduleAddTaskId,
                RECT{ contentLeft, panelBounds_.top + 378 - scrollY, contentLeft + 154, panelBounds_.top + 422 - scrollY },
                L"添加计划",
                false,
                true,
                true,
                false);

            int listTop = panelBounds_.top + 506 - scrollY;
            int rowHeight = 34;
            RECT viewport = PanelContentViewport();
            for (int i = 0; i < static_cast<int>(scheduledTasks_.size()); ++i) {
                int rowTop = listTop + i * rowHeight;
                int rowBottom = rowTop + 26;
                if (rowBottom < viewport.top || rowTop > viewport.bottom) {
                    continue;
                }
                AddPanelButton(
                    kScheduleDeleteButtonBaseId + i,
                    RECT{ panelBounds_.right - 104, rowTop, panelBounds_.right - 40, rowBottom },
                    L"删除",
                    false,
                    true,
                    false,
                    true);
            }
        } else if (activePanelTab_ == kPanelTabSettingsId) {
            AddStepperButtons(
                kSettingMaxMinusFiveId,
                kSettingMaxMinusOneId,
                kSettingMaxDisplayId,
                kSettingMaxPlusOneId,
                kSettingMaxPlusFiveId,
                contentLeft,
                panelBounds_.top + 206,
                maxFocusMinutes_,
                L" 分钟",
                maxFocusMinutes_ > kMinFocusMinutes,
                maxFocusMinutes_ < kAbsoluteMaxFocusMinutes);

            AddStepperButtons(
                kSettingDefaultMinusFiveId,
                kSettingDefaultMinusOneId,
                kSettingDefaultDisplayId,
                kSettingDefaultPlusOneId,
                kSettingDefaultPlusFiveId,
                contentLeft,
                panelBounds_.top + 350,
                defaultFocusMinutes_,
                L" 分钟",
                defaultFocusMinutes_ > kMinFocusMinutes,
                defaultFocusMinutes_ < maxFocusMinutes_);

            AddPanelButton(
                kPanelResetSettingsId,
                RECT{ contentLeft, panelBounds_.bottom - 82, contentLeft + 154, panelBounds_.bottom - 38 },
                L"恢复默认",
                false,
                true,
                false,
                true);
        } else if (activePanelTab_ == kPanelTabRerunId) {
            AddPanelButton(
                kRerunCreateTaskId,
                RECT{ contentLeft, panelBounds_.top + 184, contentLeft + 250, panelBounds_.top + 232 },
                L"添加开机自启任务",
                false,
                true,
                true,
                false);
        } else if (activePanelTab_ == kPanelTabUpdateId) {
            AddPanelButton(
                kUpdateNowButtonId,
                RECT{ contentLeft, panelBounds_.top + 150, contentLeft + 172, panelBounds_.top + 198 },
                L"立即更新",
                false,
                updateAvailable_ && !updateCheckInProgress_ && !updateDownloadInProgress_,
                updateAvailable_ && !updateCheckInProgress_ && !updateDownloadInProgress_,
                false);
        } else if (activePanelTab_ == kPanelTabWhitelistId) {
            whitelistScrollOffset_ = std::clamp(whitelistScrollOffset_, 0, WhitelistContentMaxScroll());
            int scrollY = whitelistScrollOffset_;
            AddPanelButton(
                kWhitelistAddFileId,
                RECT{ contentLeft, panelBounds_.top + 150 - scrollY, contentLeft + 126, panelBounds_.top + 194 - scrollY },
                L"选择文件添加",
                false,
                true,
                true,
                false);
            AddPanelButton(
                kWhitelistAddFolderId,
                RECT{ contentLeft + 138, panelBounds_.top + 150 - scrollY, contentLeft + 264, panelBounds_.top + 194 - scrollY },
                L"选择文件夹",
                false,
                true,
                false,
                false);
            AddPanelButton(
                kWhitelistAddWindowId,
                RECT{ contentLeft, panelBounds_.top + 200 - scrollY, contentLeft + 182, panelBounds_.top + 244 - scrollY },
                L"从窗口添加",
                false,
                true,
                false,
                false);

            int listTop = panelBounds_.top + 336 - scrollY;
            int rowHeight = 36;
            RECT viewport = PanelContentViewport();
            for (int i = 0; i < static_cast<int>(whitelistEntries_.size()); ++i) {
                int top = listTop + i * rowHeight;
                int rowBottom = top + 26;
                if (rowBottom < viewport.top || top > viewport.bottom) {
                    continue;
                }
                AddPanelButton(
                    kWhitelistDeleteButtonBaseId + i,
                    RECT{ panelBounds_.right - 104, top, panelBounds_.right - 40, rowBottom },
                    L"删除",
                    false,
                    true,
                    false,
                    true);
            }
        }
    } else {
        panelBounds_ = RECT{};
    }

    const int appButtonSize = whitelistIconSize_;
    const int appColumns = 3;
    const int appGap = 14;
    const int appGridWidth = appColumns * appButtonSize + (appColumns - 1) * appGap;
    if (whitelistLeft_ < 0) {
        whitelistLeft_ = width - appGridWidth - 36;
    }
    ClampWhitelistLayout(width, height);
    const int appLeft = whitelistLeft_;
    const int appTop = whitelistTop_;
    const int appBottomLimit = height - 112;
    const int appRows = std::max(0, (appBottomLimit - appTop - 40) / (appButtonSize + appGap));
    const int appCapacity = std::max(0, std::min(kWhitelistButtonLimit - kWhitelistButtonBaseId, appRows * appColumns));
    const int appCount = std::min(static_cast<int>(whitelistEntries_.size()), appCapacity);
    whitelistBounds_ = RECT{ appLeft, appTop, appLeft + appGridWidth, appTop + 40 };

    for (int i = 0; i < appCount; ++i) {
        int row = i / appColumns;
        int column = i % appColumns;
        int left = appLeft + column * (appButtonSize + appGap);
        int top = appTop + 40 + row * (appButtonSize + appGap);
        UiButton app{};
        app.rect = RECT{ left, top, left + appButtonSize, top + appButtonSize };
        app.label = whitelistEntries_[i].label;
        app.iconPath = whitelistEntries_[i].iconPath;
        app.icon = whitelistEntries_[i].icon;
        app.id = kWhitelistButtonBaseId + i;
        app.primary = focusActive_;
        app.iconOnly = true;
        buttons_.push_back(app);

        if (i == 0) {
            whitelistBounds_.top = std::min(whitelistBounds_.top, static_cast<LONG>(top));
        }
        whitelistBounds_.left = std::min(whitelistBounds_.left, static_cast<LONG>(left));
        whitelistBounds_.right = std::max(whitelistBounds_.right, static_cast<LONG>(left + appButtonSize));
        whitelistBounds_.bottom = std::max(whitelistBounds_.bottom, static_cast<LONG>(top + appButtonSize));
    }
}

void FocusClockApp::HitTestAndClick(POINT pt) {
    if (panelOpen_) {
        RECT viewport = PanelContentViewport();
        for (const auto& button : buttons_) {
            if (button.id < kPanelButtonBaseId || !button.enabled || !PtInRect(&button.rect, pt)) {
                continue;
            }
            if (activePanelTab_ == kPanelTabScheduleId &&
                button.id >= kScheduleStartHourMinusId &&
                button.id < kScheduleDeleteButtonLimit &&
                !PtInRect(&viewport, pt)) {
                continue;
            }
            if (activePanelTab_ == kPanelTabWhitelistId &&
                button.id >= kWhitelistAddFileId &&
                button.id < kWhitelistDeleteButtonLimit &&
                !PtInRect(&viewport, pt)) {
                continue;
            }

            HandlePanelCommand(button.id);
            return;
        }

        return;
    }

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
            selectedMinutes_ = std::min(maxFocusMinutes_, selectedMinutes_ + 1);
            remainingSeconds_ = selectedMinutes_ * 60LL;
            RebuildLayout();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (button.id == kCustomPlusFiveId) {
            selectedMinutes_ = std::min(maxFocusMinutes_, selectedMinutes_ + 5);
            remainingSeconds_ = selectedMinutes_ * 60LL;
            RebuildLayout();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (button.id >= kWhitelistButtonBaseId && button.id < kWhitelistButtonLimit) {
            OpenWhitelistEntry(static_cast<size_t>(button.id - kWhitelistButtonBaseId));
        } else {
            selectedMinutes_ = std::clamp(button.id, kMinFocusMinutes, maxFocusMinutes_);
            remainingSeconds_ = selectedMinutes_ * 60LL;
            RebuildLayout();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
}

bool FocusClockApp::BeginWhitelistDrag(POINT pt) {
    if (!IsShiftDown() || !IsPointInWhitelistArea(pt)) {
        return false;
    }

    whitelistDragging_ = true;
    whitelistDragMoved_ = false;
    whitelistDragStartPt_ = pt;
    whitelistDragStartOrigin_ = POINT{ whitelistLeft_, whitelistTop_ };
    previousWhitelistBounds_ = whitelistBounds_;
    SetCapture(hwnd_);
    return true;
}

void FocusClockApp::UpdateWhitelistDrag(POINT pt) {
    if (!whitelistDragging_) {
        return;
    }

    int dx = pt.x - whitelistDragStartPt_.x;
    int dy = pt.y - whitelistDragStartPt_.y;
    if (std::abs(dx) >= 2 || std::abs(dy) >= 2) {
        whitelistDragMoved_ = true;
    }

    whitelistLeft_ = whitelistDragStartOrigin_.x + dx;
    whitelistTop_ = whitelistDragStartOrigin_.y + dy;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    ClampWhitelistLayout(rc.right - rc.left, rc.bottom - rc.top);
    MarkWhitelistLayoutChanged(false);
}

void FocusClockApp::EndWhitelistDrag() {
    if (!whitelistDragging_) {
        return;
    }

    whitelistDragging_ = false;
    ReleaseCapture();
    ProcessPendingWhitelistLayout(true);
}

bool FocusClockApp::IsShiftDown() const {
    return (GetKeyState(VK_SHIFT) & 0x8000) != 0;
}

bool FocusClockApp::IsPointInWhitelistArea(POINT pt) const {
    return whitelistBounds_.right > whitelistBounds_.left &&
        whitelistBounds_.bottom > whitelistBounds_.top &&
        PtInRect(&whitelistBounds_, pt);
}

void FocusClockApp::MoveWhitelistLayout(int dx, int dy) {
    whitelistLeft_ += dx;
    whitelistTop_ += dy;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    ClampWhitelistLayout(rc.right - rc.left, rc.bottom - rc.top);
    MarkWhitelistLayoutChanged(false);
    ProcessPendingWhitelistLayout();
}

void FocusClockApp::ResizeWhitelistLayout(int delta) {
    whitelistIconSize_ = std::clamp(whitelistIconSize_ + delta, kMinWhitelistIconSize, kMaxWhitelistIconSize);
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    ClampWhitelistLayout(rc.right - rc.left, rc.bottom - rc.top);
    MarkWhitelistLayoutChanged(true);
    ProcessPendingWhitelistLayout();
}

void FocusClockApp::RebuildWhitelistLayoutOnly() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    ClampWhitelistLayout(width, height);

    buttons_.erase(
        std::remove_if(buttons_.begin(), buttons_.end(), [](const UiButton& button) {
            return button.id >= kWhitelistButtonBaseId && button.id < kWhitelistButtonLimit;
        }),
        buttons_.end());

    const int appButtonSize = whitelistIconSize_;
    const int appColumns = 3;
    const int appGap = 14;
    const int appGridWidth = appColumns * appButtonSize + (appColumns - 1) * appGap;
    const int appLeft = whitelistLeft_;
    const int appTop = whitelistTop_;
    const int appBottomLimit = height - 112;
    const int appRows = std::max(0, (appBottomLimit - appTop - 40) / (appButtonSize + appGap));
    const int appCapacity = std::max(0, std::min(kWhitelistButtonLimit - kWhitelistButtonBaseId, appRows * appColumns));
    const int appCount = std::min(static_cast<int>(whitelistEntries_.size()), appCapacity);
    whitelistBounds_ = RECT{ appLeft, appTop, appLeft + appGridWidth, appTop + 40 };

    for (int i = 0; i < appCount; ++i) {
        int row = i / appColumns;
        int column = i % appColumns;
        int left = appLeft + column * (appButtonSize + appGap);
        int top = appTop + 40 + row * (appButtonSize + appGap);
        UiButton app{};
        app.rect = RECT{ left, top, left + appButtonSize, top + appButtonSize };
        app.label = whitelistEntries_[i].label;
        app.iconPath = whitelistEntries_[i].iconPath;
        app.icon = whitelistEntries_[i].icon;
        app.id = kWhitelistButtonBaseId + i;
        app.primary = focusActive_;
        app.iconOnly = true;
        buttons_.push_back(app);

        if (i == 0) {
            whitelistBounds_.top = std::min(whitelistBounds_.top, static_cast<LONG>(top));
        }
        whitelistBounds_.left = std::min(whitelistBounds_.left, static_cast<LONG>(left));
        whitelistBounds_.right = std::max(whitelistBounds_.right, static_cast<LONG>(left + appButtonSize));
        whitelistBounds_.bottom = std::max(whitelistBounds_.bottom, static_cast<LONG>(top + appButtonSize));
    }
}

void FocusClockApp::MarkWhitelistLayoutChanged(bool needsRebuild) {
    if (!whitelistLayoutDirty_) {
        previousWhitelistBounds_ = whitelistBounds_;
    }

    whitelistLayoutDirty_ = true;
    whitelistLayoutNeedsRebuild_ = whitelistLayoutNeedsRebuild_ || needsRebuild;
    whitelistLayoutSavePending_ = true;
    whitelistLayoutLastChangeTick_ = GetTickCount();
    SetTimer(hwnd_, kWhitelistLayoutTimer, kWhitelistLayoutTimerMs, nullptr);
}

void FocusClockApp::ProcessPendingWhitelistLayout(bool forceSave) {
    if (whitelistLayoutDirty_) {
        RECT oldBounds = previousWhitelistBounds_;
        if (whitelistLayoutNeedsRebuild_) {
            RebuildWhitelistLayoutOnly();
        } else {
            RECT rc{};
            GetClientRect(hwnd_, &rc);
            ClampWhitelistLayout(rc.right - rc.left, rc.bottom - rc.top);
            RebuildWhitelistLayoutOnly();
        }

        InvalidateWhitelistArea(oldBounds);
        whitelistLayoutDirty_ = false;
        whitelistLayoutNeedsRebuild_ = false;
    }

    bool shouldSave = forceSave;
    if (!shouldSave && whitelistLayoutSavePending_) {
        DWORD elapsed = GetTickCount() - whitelistLayoutLastChangeTick_;
        shouldSave = elapsed >= kWhitelistLayoutSaveDelayMs;
    }

    if (shouldSave && whitelistLayoutSavePending_) {
        SaveWhitelistLayoutSettings();
        whitelistLayoutSavePending_ = false;
    }

    if (!whitelistLayoutDirty_ && !whitelistLayoutSavePending_) {
        KillTimer(hwnd_, kWhitelistLayoutTimer);
    }
}

void FocusClockApp::InvalidateWhitelistArea(const RECT& previousBounds) {
    RECT oldRect = previousBounds;
    RECT newRect = whitelistBounds_;
    InflateRect(&oldRect, kMaxWhitelistIconSize + 64, 64);
    InflateRect(&newRect, kMaxWhitelistIconSize + 64, 64);
    InvalidateRect(hwnd_, &oldRect, FALSE);
    InvalidateRect(hwnd_, &newRect, FALSE);
}

void FocusClockApp::ClampWhitelistLayout(int clientWidth, int clientHeight) {
    whitelistIconSize_ = std::clamp(whitelistIconSize_, kMinWhitelistIconSize, kMaxWhitelistIconSize);

    const int appColumns = 3;
    const int appGap = 14;
    int gridWidth = appColumns * whitelistIconSize_ + (appColumns - 1) * appGap;
    int minLeft = 12;
    int minTop = 12;
    int maxLeft = std::max(minLeft, clientWidth - gridWidth - 12);
    int maxTop = std::max(minTop, clientHeight - whitelistIconSize_ - 52);

    if (whitelistLeft_ < 0) {
        whitelistLeft_ = maxLeft;
    }
    whitelistLeft_ = std::clamp(whitelistLeft_, minLeft, maxLeft);
    whitelistTop_ = std::clamp(whitelistTop_, minTop, maxTop);
}

void FocusClockApp::LoadWhitelistLayoutSettings() {
    std::wstring path = GetExecutableDirectory() + L"\\FocusClock.ini";
    whitelistLeft_ = GetPrivateProfileIntW(L"Whitelist", L"Left", -1, path.c_str());
    whitelistTop_ = GetPrivateProfileIntW(L"Whitelist", L"Top", 92, path.c_str());
    whitelistIconSize_ = GetPrivateProfileIntW(L"Whitelist", L"IconSize", kDefaultWhitelistIconSize, path.c_str());
    whitelistIconSize_ = std::clamp(whitelistIconSize_, kMinWhitelistIconSize, kMaxWhitelistIconSize);
}

void FocusClockApp::SaveWhitelistLayoutSettings() const {
    std::wstring path = GetExecutableDirectory() + L"\\FocusClock.ini";
    wchar_t buffer[32]{};

    swprintf_s(buffer, L"%d", whitelistLeft_);
    WritePrivateProfileStringW(L"Whitelist", L"Left", buffer, path.c_str());

    swprintf_s(buffer, L"%d", whitelistTop_);
    WritePrivateProfileStringW(L"Whitelist", L"Top", buffer, path.c_str());

    swprintf_s(buffer, L"%d", whitelistIconSize_);
    WritePrivateProfileStringW(L"Whitelist", L"IconSize", buffer, path.c_str());
}

void FocusClockApp::ScrollScheduleTab(int delta) {
    int maxScroll = ScheduleContentMaxScroll();
    int next = std::clamp(scheduleScrollOffset_ + delta, 0, maxScroll);
    if (next == scheduleScrollOffset_) {
        return;
    }

    scheduleScrollOffset_ = next;
    RebuildLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

int FocusClockApp::ScheduleContentMaxScroll() const {
    if (panelBounds_.bottom <= panelBounds_.top) {
        return 0;
    }

    int viewportHeight = std::max(1, static_cast<int>(panelBounds_.bottom - panelBounds_.top - 126));
    int contentHeight = 450 + static_cast<int>(scheduledTasks_.size()) * 34;
    return std::max(0, contentHeight - viewportHeight);
}

void FocusClockApp::ScrollWhitelistTab(int delta) {
    int maxScroll = WhitelistContentMaxScroll();
    int next = std::clamp(whitelistScrollOffset_ + delta, 0, maxScroll);
    if (next == whitelistScrollOffset_) {
        return;
    }

    whitelistScrollOffset_ = next;
    RebuildLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

int FocusClockApp::WhitelistContentMaxScroll() const {
    if (panelBounds_.bottom <= panelBounds_.top) {
        return 0;
    }

    int viewportHeight = std::max(1, static_cast<int>(panelBounds_.bottom - panelBounds_.top - 126));
    int contentHeight = 274 + static_cast<int>(whitelistEntries_.size()) * 36;
    return std::max(0, contentHeight - viewportHeight);
}

RECT FocusClockApp::PanelContentViewport() const {
    if (panelBounds_.bottom <= panelBounds_.top) {
        return RECT{};
    }

    return RECT{
        panelBounds_.left + 216,
        panelBounds_.top + 92,
        panelBounds_.right - 32,
        panelBounds_.bottom - 34
    };
}

} // namespace focus_clock
