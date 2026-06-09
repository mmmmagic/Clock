#include "FocusClockApp.h"

namespace focus_clock {

void FocusClockApp::TogglePanel() {
    if (focusActive_) {
        return;
    }

    panelOpen_ = !panelOpen_;
    if (panelOpen_ && activePanelTab_ == kPanelTabRerunId) {
        CheckRerunStartupTaskStatus();
    } else if (panelOpen_ && activePanelTab_ == kPanelTabUpdateId) {
        StartUpdateCheck(true);
    }
    RebuildLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void FocusClockApp::ClosePanel() {
    if (!panelOpen_) {
        return;
    }

    panelOpen_ = false;
    RebuildLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void FocusClockApp::AddPanelButton(int id, const RECT& rect, const std::wstring& label, bool selected, bool enabled, bool primary, bool danger) {
    UiButton button{};
    button.rect = rect;
    button.label = label;
    button.id = id;
    button.selected = selected;
    button.enabled = enabled;
    button.primary = primary;
    button.danger = danger;
    buttons_.push_back(button);
}

void FocusClockApp::AddStepperButtons(int minusFiveId, int minusOneId, int displayId, int plusOneId, int plusFiveId, int left, int top, int value, const std::wstring& suffix, bool canDecrease, bool canIncrease) {
    const int buttonHeight = 44;
    const int smallWidth = 58;
    const int displayWidth = 170;
    const int gap = 10;

    AddPanelButton(minusFiveId, RECT{ left, top, left + smallWidth, top + buttonHeight }, L"-5", false, canDecrease);
    left += smallWidth + gap;
    AddPanelButton(minusOneId, RECT{ left, top, left + smallWidth, top + buttonHeight }, L"-1", false, canDecrease);
    left += smallWidth + gap;
    AddPanelButton(displayId, RECT{ left, top, left + displayWidth, top + buttonHeight }, std::to_wstring(value) + suffix, true, false);
    left += displayWidth + gap;
    AddPanelButton(plusOneId, RECT{ left, top, left + smallWidth, top + buttonHeight }, L"+1", false, canIncrease);
    left += smallWidth + gap;
    AddPanelButton(plusFiveId, RECT{ left, top, left + smallWidth, top + buttonHeight }, L"+5", false, canIncrease);
}

void FocusClockApp::AddTimeStepperButtons(int minusId, int displayId, int plusId, int left, int top, int value, int maxValue, const std::wstring& suffix) {
    const int buttonHeight = 44;
    const int smallWidth = 42;
    const int displayWidth = 112;
    const int gap = 8;

    AddPanelButton(minusId, RECT{ left, top, left + smallWidth, top + buttonHeight }, L"-", false, true);
    left += smallWidth + gap;

    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%02d%s", std::clamp(value, 0, maxValue), suffix.c_str());
    AddPanelButton(displayId, RECT{ left, top, left + displayWidth, top + buttonHeight }, buffer, true, false);
    left += displayWidth + gap;

    AddPanelButton(plusId, RECT{ left, top, left + smallWidth, top + buttonHeight }, L"+", false, true);
}

void FocusClockApp::HandlePanelCommand(int id) {
    if (id == kPanelCloseButtonId) {
        ClosePanel();
    } else if (id >= kPanelTabButtonBaseId && id < kSettingMaxMinusFiveId) {
        activePanelTab_ = id - kPanelTabButtonBaseId;
        if (activePanelTab_ == kPanelTabRerunId) {
            CheckRerunStartupTaskStatus();
        } else if (activePanelTab_ == kPanelTabUpdateId) {
            StartUpdateCheck(true);
        }
        RebuildLayout();
        InvalidateRect(hwnd_, nullptr, FALSE);
    } else if (id == kPanelResetSettingsId) {
        ResetAppSettings();
    } else if (id == kSettingMaxMinusFiveId) {
        SetMaxFocusMinutes(maxFocusMinutes_ - 5);
    } else if (id == kSettingMaxMinusOneId) {
        SetMaxFocusMinutes(maxFocusMinutes_ - 1);
    } else if (id == kSettingMaxPlusOneId) {
        SetMaxFocusMinutes(maxFocusMinutes_ + 1);
    } else if (id == kSettingMaxPlusFiveId) {
        SetMaxFocusMinutes(maxFocusMinutes_ + 5);
    } else if (id == kSettingDefaultMinusFiveId) {
        SetDefaultFocusMinutes(defaultFocusMinutes_ - 5);
    } else if (id == kSettingDefaultMinusOneId) {
        SetDefaultFocusMinutes(defaultFocusMinutes_ - 1);
    } else if (id == kSettingDefaultPlusOneId) {
        SetDefaultFocusMinutes(defaultFocusMinutes_ + 1);
    } else if (id == kSettingDefaultPlusFiveId) {
        SetDefaultFocusMinutes(defaultFocusMinutes_ + 5);
    } else if (id >= kScheduleStartHourMinusId && id <= kScheduleEndMinutePlusId) {
        AdjustScheduleDraft(id);
    } else if (id == kScheduleAddTaskId) {
        AddScheduledFocusTask();
    } else if (id >= kScheduleDeleteButtonBaseId && id < kScheduleDeleteButtonLimit) {
        DeleteScheduledFocusTask(static_cast<size_t>(id - kScheduleDeleteButtonBaseId));
    } else if (id == kRerunCreateTaskId) {
        CreateRerunStartupTask();
    } else if (id == kUpdateNowButtonId) {
        StartUpdateDownload();
    } else if (id == kWhitelistAddFileId) {
        AddWhitelistFromFileDialog();
    } else if (id == kWhitelistAddFolderId) {
        AddWhitelistFromFolderDialog();
    } else if (id == kWhitelistAddWindowId) {
        AddWhitelistFromWindow();
    } else if (id >= kWhitelistDeleteButtonBaseId && id < kWhitelistDeleteButtonLimit) {
        DeleteWhitelistEntry(static_cast<size_t>(id - kWhitelistDeleteButtonBaseId));
    }
}

} // namespace focus_clock
