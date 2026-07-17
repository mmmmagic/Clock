#include "FocusClockApp.h"

namespace {
std::wstring CurrentExecutableDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    while (length == path.size()) {
        path.resize(path.size() * 2);
        length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    }

    if (length == 0) {
        return L".";
    }

    path.resize(length);
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return L".";
    }
    return path.substr(0, slash);
}

long long CurrentUnixSeconds() {
    FILETIME nowFileTime{};
    GetSystemTimeAsFileTime(&nowFileTime);
    ULARGE_INTEGER value{};
    value.LowPart = nowFileTime.dwLowDateTime;
    value.HighPart = nowFileTime.dwHighDateTime;
    constexpr ULONGLONG unixEpochOffset = 116444736000000000ULL;
    if (value.QuadPart < unixEpochOffset) {
        return 0;
    }
    return static_cast<long long>((value.QuadPart - unixEpochOffset) / 10000000ULL);
}

bool HasCommandLineSwitch(const wchar_t* target) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        return false;
    }

    bool found = false;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], target) == 0) {
            found = true;
            break;
        }
    }

    LocalFree(argv);
    return found;
}

long long RerunRemainingSecondsFromSettings() {
    std::wstring settingsPath = CurrentExecutableDirectory() + L"\\FocusClock.ini";
    int active = GetPrivateProfileIntW(L"FocusSession", L"Active", 0, settingsPath.c_str());
    if (active != 1) {
        return 0;
    }

    wchar_t buffer[64]{};
    GetPrivateProfileStringW(L"FocusSession", L"StartUnix", L"0", buffer, static_cast<DWORD>(std::size(buffer)), settingsPath.c_str());
    long long startUnix = _wtoi64(buffer);

    GetPrivateProfileStringW(L"FocusSession", L"EndUnix", L"0", buffer, static_cast<DWORD>(std::size(buffer)), settingsPath.c_str());
    long long endUnix = _wtoi64(buffer);

    long long now = CurrentUnixSeconds();
    if (startUnix <= 0 || endUnix <= startUnix || now < startUnix || now >= endUnix) {
        return 0;
    }

    return std::max(1LL, endUnix - now);
}

bool IsValidStoredScheduleRange(int startMinute, int endMinute) {
    return startMinute >= 0 && startMinute < 24 * 60 &&
        endMinute >= 0 && endMinute < 24 * 60 &&
        startMinute != endMinute;
}

bool IsMinuteInStoredScheduleRange(int minuteOfDay, int startMinute, int endMinute) {
    if (endMinute > startMinute) {
        return minuteOfDay >= startMinute && minuteOfDay < endMinute;
    }
    if (endMinute < startMinute) {
        return minuteOfDay >= startMinute || minuteOfDay < endMinute;
    }
    return false;
}

bool HasActiveScheduledFocusRange() {
    std::wstring settingsPath = CurrentExecutableDirectory() + L"\\FocusClock.ini";
    int count = GetPrivateProfileIntW(L"Schedule", L"Count", 0, settingsPath.c_str());
    count = std::clamp(count, 0, 64);

    SYSTEMTIME now{};
    GetLocalTime(&now);
    int minuteOfDay = now.wHour * 60 + now.wMinute;

    for (int i = 0; i < count; ++i) {
        std::wstring key = L"Task" + std::to_wstring(i);
        wchar_t buffer[64]{};
        GetPrivateProfileStringW(L"Schedule", key.c_str(), L"", buffer, static_cast<DWORD>(std::size(buffer)), settingsPath.c_str());

        std::wstring value = buffer;
        size_t comma = value.find(L',');
        if (comma == std::wstring::npos) {
            continue;
        }

        size_t secondComma = value.find(L',', comma + 1);
        int startMinute = _wtoi(value.substr(0, comma).c_str());
        int endMinute = _wtoi(value.substr(comma + 1, secondComma == std::wstring::npos ? std::wstring::npos : secondComma - comma - 1).c_str());
        if (IsValidStoredScheduleRange(startMinute, endMinute) &&
            IsMinuteInStoredScheduleRange(minuteOfDay, startMinute, endMinute)) {
            return true;
        }
    }

    return false;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware();

    HANDLE singleInstance = CreateMutexW(nullptr, FALSE, L"Local\\FocusClock.SingleInstance");
    if (!singleInstance) {
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(singleInstance);
        return 0;
    }

    long long rerunResumeSeconds = 0;
    if (HasCommandLineSwitch(L"-rerun")) {
        rerunResumeSeconds = RerunRemainingSecondsFromSettings();
        if (rerunResumeSeconds <= 0 && !HasActiveScheduledFocusRange()) {
            CloseHandle(singleInstance);
            return 0;
        }
    }

    focus_clock::FocusClockApp app;
    int result = app.Run(instance, show, rerunResumeSeconds);
    CloseHandle(singleInstance);
    return result;
}
