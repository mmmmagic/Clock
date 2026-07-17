#include "FocusClockApp.h"

namespace focus_clock {

void FocusClockApp::LoadAppSettings() {
    std::wstring path = GetSettingsPath();
    maxFocusMinutes_ = GetPrivateProfileIntW(L"Focus", L"MaxMinutes", kDefaultMaxFocusMinutes, path.c_str());
    maxFocusMinutes_ = std::clamp(maxFocusMinutes_, kMinFocusMinutes, kAbsoluteMaxFocusMinutes);

    defaultFocusMinutes_ = GetPrivateProfileIntW(L"Focus", L"DefaultMinutes", kDefaultFocusMinutes, path.c_str());
    defaultFocusMinutes_ = std::clamp(defaultFocusMinutes_, kMinFocusMinutes, maxFocusMinutes_);

    selectedMinutes_ = defaultFocusMinutes_;
    remainingSeconds_ = selectedMinutes_ * 60LL;
    focusTotalSeconds_ = remainingSeconds_;
}

void FocusClockApp::SaveAppSettings() const {
    std::wstring path = GetSettingsPath();
    wchar_t buffer[32]{};

    swprintf_s(buffer, L"%d", maxFocusMinutes_);
    WritePrivateProfileStringW(L"Focus", L"MaxMinutes", buffer, path.c_str());

    swprintf_s(buffer, L"%d", defaultFocusMinutes_);
    WritePrivateProfileStringW(L"Focus", L"DefaultMinutes", buffer, path.c_str());
}

void FocusClockApp::SaveFocusSessionSettings(long long durationSeconds, long long totalSeconds) const {
    FILETIME startUtc{};
    GetSystemTimeAsFileTime(&startUtc);

    ULARGE_INTEGER endValue{};
    endValue.LowPart = startUtc.dwLowDateTime;
    endValue.HighPart = startUtc.dwHighDateTime;
    endValue.QuadPart += static_cast<ULONGLONG>(std::max(1LL, durationSeconds)) * 10000000ULL;

    FILETIME endUtc{};
    endUtc.dwLowDateTime = endValue.LowPart;
    endUtc.dwHighDateTime = endValue.HighPart;

    FILETIME startLocalFileTime{};
    FILETIME endLocalFileTime{};
    SYSTEMTIME startLocal{};
    SYSTEMTIME endLocal{};
    FileTimeToLocalFileTime(&startUtc, &startLocalFileTime);
    FileTimeToLocalFileTime(&endUtc, &endLocalFileTime);
    FileTimeToSystemTime(&startLocalFileTime, &startLocal);
    FileTimeToSystemTime(&endLocalFileTime, &endLocal);

    std::wstring path = GetSettingsPath();
    wchar_t buffer[64]{};

    WritePrivateProfileStringW(L"FocusSession", L"Active", L"1", path.c_str());

    swprintf_s(buffer, L"%lld", durationSeconds);
    WritePrivateProfileStringW(L"FocusSession", L"DurationSeconds", buffer, path.c_str());

    swprintf_s(buffer, L"%lld", (durationSeconds + 59) / 60);
    WritePrivateProfileStringW(L"FocusSession", L"DurationMinutes", buffer, path.c_str());

    long long normalizedTotalSeconds = std::max(std::max(1LL, durationSeconds), totalSeconds);
    swprintf_s(buffer, L"%lld", normalizedTotalSeconds);
    WritePrivateProfileStringW(L"FocusSession", L"TotalSeconds", buffer, path.c_str());

    swprintf_s(buffer, L"%lld", FileTimeToUnixSeconds(startUtc));
    WritePrivateProfileStringW(L"FocusSession", L"StartUnix", buffer, path.c_str());

    swprintf_s(buffer, L"%lld", FileTimeToUnixSeconds(endUtc));
    WritePrivateProfileStringW(L"FocusSession", L"EndUnix", buffer, path.c_str());

    WritePrivateProfileStringW(L"FocusSession", L"StartLocal", FormatConfigDateTime(startLocal).c_str(), path.c_str());
    WritePrivateProfileStringW(L"FocusSession", L"EndLocal", FormatConfigDateTime(endLocal).c_str(), path.c_str());
}

void FocusClockApp::ClearFocusSessionSettings() const {
    WritePrivateProfileStringW(L"FocusSession", nullptr, nullptr, GetSettingsPath().c_str());
}

void FocusClockApp::ResetAppSettings() {
    SetMaxFocusMinutes(kDefaultMaxFocusMinutes);
    SetDefaultFocusMinutes(kDefaultFocusMinutes);
}

void FocusClockApp::SetMaxFocusMinutes(int minutes) {
    maxFocusMinutes_ = std::clamp(minutes, kMinFocusMinutes, kAbsoluteMaxFocusMinutes);
    defaultFocusMinutes_ = std::clamp(defaultFocusMinutes_, kMinFocusMinutes, maxFocusMinutes_);
    selectedMinutes_ = std::clamp(selectedMinutes_, kMinFocusMinutes, maxFocusMinutes_);
    remainingSeconds_ = selectedMinutes_ * 60LL;
    SaveAppSettings();
    RebuildLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void FocusClockApp::SetDefaultFocusMinutes(int minutes) {
    defaultFocusMinutes_ = std::clamp(minutes, kMinFocusMinutes, maxFocusMinutes_);
    selectedMinutes_ = defaultFocusMinutes_;
    remainingSeconds_ = selectedMinutes_ * 60LL;
    SaveAppSettings();
    RebuildLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::wstring FocusClockApp::GetSettingsPath() const {
    return GetExecutableDirectory() + L"\\FocusClock.ini";
}

} // namespace focus_clock
