#include "FocusClockApp.h"

namespace focus_clock {

bool FocusClockApp::LoadWhitelistIfNeeded(bool force) {
    std::wstring path = GetWhitelistPath();

    WIN32_FILE_ATTRIBUTE_DATA data{};
    bool exists = GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) != FALSE;
    if (!exists) {
        if (force || whitelistKnown_) {
            DestroyWhitelistIconCache();
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
                entry.icon = LoadIconForPath(entry.iconPath);
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

    DestroyWhitelistIconCache();
    whitelistEntries_ = std::move(entries);
    whitelistWriteTime_ = data.ftLastWriteTime;
    whitelistKnown_ = true;
    return true;
}

std::wstring FocusClockApp::GetWhitelistPath() const {
    return GetExecutableDirectory() + L"\\Whitelist.txt";
}

void FocusClockApp::DestroyWhitelistIconCache() {
    for (auto& entry : whitelistEntries_) {
        if (entry.icon) {
            DestroyIcon(entry.icon);
            entry.icon = nullptr;
        }
    }
}

bool FocusClockApp::SaveWhitelistEntries(const std::vector<std::wstring>& launchSpecs) {
    std::wstring text;
    text.reserve(launchSpecs.size() * 96);
    for (const auto& spec : launchSpecs) {
        std::wstring line = Trim(spec);
        if (line.empty()) {
            continue;
        }
        text += line;
        text += L"\r\n";
    }

    int byteCount = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (byteCount <= 0 && !text.empty()) {
        return false;
    }

    std::vector<char> bytes;
    bytes.reserve(text.empty() ? 0 : static_cast<size_t>(byteCount) + 3);
    if (byteCount > 0) {
        bytes.push_back(static_cast<char>(0xEF));
        bytes.push_back(static_cast<char>(0xBB));
        bytes.push_back(static_cast<char>(0xBF));
        size_t offset = bytes.size();
        bytes.resize(offset + static_cast<size_t>(byteCount));
        int writtenText = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), bytes.data() + offset, byteCount, nullptr, nullptr);
        if (writtenText != byteCount) {
            return false;
        }
    }

    std::wstring path = GetWhitelistPath();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    BOOL ok = bytes.empty() || WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == bytes.size();
}

bool FocusClockApp::IsWhitelistPathDuplicate(const std::wstring& path) const {
    std::wstring normalizedPath = ToLower(Trim(path));
    std::wstring resolvedPath = ToLower(ResolveLaunchPath(path));
    for (const auto& entry : whitelistEntries_) {
        if (entry.normalized == normalizedPath || ToLower(entry.iconPath) == resolvedPath) {
            return true;
        }
    }
    return false;
}

void FocusClockApp::AddWhitelistPath(const std::wstring& path) {
    std::wstring trimmed = Trim(path);
    if (trimmed.empty()) {
        return;
    }

    if (IsWhitelistPathDuplicate(trimmed)) {
        whitelistMessage_ = L"添加失败：该程序已在白名单中。";
        whitelistMessageIsError_ = true;
    } else {
        std::vector<std::wstring> specs;
        specs.reserve(whitelistEntries_.size() + 1);
        for (const auto& entry : whitelistEntries_) {
            specs.push_back(entry.launchSpec);
        }
        specs.push_back(trimmed);

        if (SaveWhitelistEntries(specs)) {
            LoadWhitelistIfNeeded(true);
            whitelistMessage_ = L"已添加：" + BaseName(trimmed);
            whitelistMessageIsError_ = false;
        } else {
            whitelistMessage_ = L"添加失败：无法写入 Whitelist.txt。";
            whitelistMessageIsError_ = true;
        }
    }

    RebuildLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void FocusClockApp::AddWhitelistFolder(const std::wstring& folder) {
    std::wstring trimmed = Trim(folder);
    if (trimmed.empty()) {
        return;
    }

    DWORD attributes = GetFileAttributesW(trimmed.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        whitelistMessage_ = L"添加失败：文件夹不可用。";
        whitelistMessageIsError_ = true;
        RebuildLayout();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    std::vector<std::wstring> executablePaths = EnumerateExecutableFilesInDirectory(trimmed);
    if (executablePaths.empty()) {
        whitelistMessage_ = L"添加失败：该文件夹下没有找到 .exe 程序。";
        whitelistMessageIsError_ = true;
        RebuildLayout();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    std::vector<std::wstring> specs;
    specs.reserve(whitelistEntries_.size() + executablePaths.size());
    std::map<std::wstring, bool> seen;
    for (const auto& entry : whitelistEntries_) {
        specs.push_back(entry.launchSpec);
        seen[entry.normalized] = true;
        seen[ToLower(entry.iconPath)] = true;
    }

    int added = 0;
    for (const auto& path : executablePaths) {
        std::wstring normalized = ToLower(path);
        if (seen.find(normalized) != seen.end()) {
            continue;
        }

        specs.push_back(path);
        seen[normalized] = true;
        ++added;
    }

    if (added == 0) {
        whitelistMessage_ = L"添加失败：文件夹内程序已在白名单中。";
        whitelistMessageIsError_ = true;
    } else if (SaveWhitelistEntries(specs)) {
        LoadWhitelistIfNeeded(true);
        whitelistMessage_ = L"已从文件夹添加 " + std::to_wstring(added) + L" 个程序。";
        whitelistMessageIsError_ = false;
    } else {
        whitelistMessage_ = L"添加失败：无法写入 Whitelist.txt。";
        whitelistMessageIsError_ = true;
    }

    RebuildLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void FocusClockApp::AddWhitelistFromFileDialog() {
    wchar_t path[32768]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"程序文件 (*.exe)\0*.exe\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = static_cast<DWORD>(std::size(path));
    ofn.lpstrTitle = L"选择要加入白名单的程序";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) {
        AddWhitelistPath(path);
    }
}

void FocusClockApp::AddWhitelistFromFolderDialog() {
    HRESULT oleResult = OleInitialize(nullptr);
    bool shouldUninitializeOle = SUCCEEDED(oleResult);

    BROWSEINFOW browse{};
    browse.hwndOwner = hwnd_;
    browse.lpszTitle = L"选择要自动加入白名单的文件夹";
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_NONEWFOLDERBUTTON;

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&browse);
    if (!pidl) {
        if (shouldUninitializeOle) {
            OleUninitialize();
        }
        return;
    }

    wchar_t folder[MAX_PATH]{};
    bool ok = SHGetPathFromIDListW(pidl, folder) != FALSE;
    CoTaskMemFree(pidl);
    if (shouldUninitializeOle) {
        OleUninitialize();
    }

    if (ok) {
        AddWhitelistFolder(folder);
    }
}

std::vector<std::wstring> FocusClockApp::EnumerateExecutableFilesInDirectory(const std::wstring& folder) const {
    std::vector<std::wstring> executablePaths;
    std::vector<std::wstring> pending{ folder };
    std::map<std::wstring, bool> visitedDirectories;

    while (!pending.empty()) {
        std::wstring current = pending.back();
        pending.pop_back();

        std::wstring normalizedDirectory = ToLower(current);
        if (visitedDirectories.find(normalizedDirectory) != visitedDirectories.end()) {
            continue;
        }
        visitedDirectories[normalizedDirectory] = true;

        WIN32_FIND_DATAW data{};
        std::wstring searchPattern = JoinPath(current, L"*");
        HANDLE find = FindFirstFileW(searchPattern.c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) {
            continue;
        }

        do {
            std::wstring name = data.cFileName;
            if (name == L"." || name == L"..") {
                continue;
            }

            std::wstring path = JoinPath(current, name);
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                    pending.push_back(path);
                }
            } else if (IsExecutableFileName(name)) {
                executablePaths.push_back(path);
            }
        } while (FindNextFileW(find, &data));

        FindClose(find);
    }

    std::sort(executablePaths.begin(), executablePaths.end(), [](const std::wstring& left, const std::wstring& right) {
        return ToLower(left) < ToLower(right);
    });
    return executablePaths;
}

std::vector<WindowPathChoice> FocusClockApp::EnumerateSelectableWindows() const {
    std::vector<WindowPathChoice> choices;
    WindowPathChoiceContext context{};
    context.app = this;
    context.choices = &choices;
    EnumWindows(EnumSelectableWindows, reinterpret_cast<LPARAM>(&context));

    std::sort(choices.begin(), choices.end(), [](const WindowPathChoice& left, const WindowPathChoice& right) {
        std::wstring leftTitle = ToLower(left.title);
        std::wstring rightTitle = ToLower(right.title);
        if (leftTitle != rightTitle) {
            return leftTitle < rightTitle;
        }

        std::wstring leftName = ToLower(left.exeName);
        std::wstring rightName = ToLower(right.exeName);
        if (leftName != rightName) {
            return leftName < rightName;
        }

        if (left.pid != right.pid) {
            return left.pid < right.pid;
        }

        return reinterpret_cast<UINT_PTR>(left.window) < reinterpret_cast<UINT_PTR>(right.window);
    });
    return choices;
}

void FocusClockApp::AddWhitelistFromWindow() {
    std::vector<WindowPathChoice> choices = EnumerateSelectableWindows();
    if (choices.empty()) {
        whitelistMessage_ = L"添加失败：没有可选择的窗口。";
        whitelistMessageIsError_ = true;
        RebuildLayout();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    constexpr size_t maxChoices = 160;
    size_t count = std::min(choices.size(), maxChoices);
    for (size_t i = 0; i < count; ++i) {
        std::wstring title = choices[i].title;
        if (title.size() > 64) {
            title = title.substr(0, 61) + L"...";
        }

        std::wstring label = title + L"  -  " + choices[i].exeName;
        AppendMenuW(menu, MF_STRING, kWindowPopupBaseId + static_cast<UINT>(i), label.c_str());
    }

    POINT pt{};
    GetCursorPos(&pt);
    UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (command >= kWindowPopupBaseId && command < kWindowPopupBaseId + count) {
        AddWhitelistPath(choices[command - kWindowPopupBaseId].path);
    }
}

void FocusClockApp::DeleteWhitelistEntry(size_t index) {
    if (index >= whitelistEntries_.size()) {
        return;
    }

    std::wstring deleted = whitelistEntries_[index].label;
    std::vector<std::wstring> specs;
    specs.reserve(whitelistEntries_.size() - 1);
    for (size_t i = 0; i < whitelistEntries_.size(); ++i) {
        if (i != index) {
            specs.push_back(whitelistEntries_[i].launchSpec);
        }
    }

    if (SaveWhitelistEntries(specs)) {
        LoadWhitelistIfNeeded(true);
        whitelistMessage_ = L"已删除：" + deleted;
        whitelistMessageIsError_ = false;
    } else {
        whitelistMessage_ = L"删除失败：无法写入 Whitelist.txt。";
        whitelistMessageIsError_ = true;
    }

    RebuildLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void FocusClockApp::OpenWhitelistEntry(size_t index) {
    if (index >= whitelistEntries_.size()) {
        return;
    }

    const WhitelistEntry& entry = whitelistEntries_[index];
    whitelistYieldUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    pendingWhitelistIndex_ = focusActive_ ? -1 : static_cast<int>(index);

    if (!focusActive_) {
        HWND running = FindRunningWhitelistWindow(entry);
        if (running) {
            pendingWhitelistIndex_ = -1;
            activeWhitelistWindow_ = running;
            BringWindowToFront(running);
            EnterFullscreenNotTopmost();
            return;
        }
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

    if (focusActive_) {
        PromoteWhitelistWindow(target, true);
    }

    if (IsIconic(target)) {
        ShowWindow(target, SW_RESTORE);
    } else {
        ShowWindow(target, SW_SHOWNORMAL);
    }

    SetWindowPos(
        target,
        focusActive_ ? HWND_TOPMOST : HWND_TOP,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(target);
}

void FocusClockApp::PromoteWhitelistWindow(HWND target, bool reorderExisting) {
    if (!target) {
        return;
    }

    bool newlyPromoted = promotedWindows_.find(target) == promotedWindows_.end();
    if (newlyPromoted) {
        promotedWindows_[target] = GetWindowLongPtrW(target, GWL_EXSTYLE);
    }

    LONG_PTR exStyle = GetWindowLongPtrW(target, GWL_EXSTYLE);
    SetWindowLongPtrW(target, GWL_EXSTYLE, exStyle | WS_EX_TOPMOST);
    if (newlyPromoted || reorderExisting) {
        SetWindowPos(target, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        SetWindowPos(target, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
}

HWND FocusClockApp::PromoteVisibleWhitelistedWindows() {
    std::vector<HWND> windows;
    WhitelistedWindowListContext context{};
    context.app = this;
    context.windows = &windows;
    EnumWindows(EnumVisibleWhitelistedWindows, reinterpret_cast<LPARAM>(&context));

    bool hasNewWindow = std::any_of(windows.begin(), windows.end(), [this](HWND window) {
        return promotedWindows_.find(window) == promotedWindows_.end();
    });

    if (hasNewWindow) {
        for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
            PromoteWhitelistWindow(*it, true);
        }
    }
    return windows.empty() ? nullptr : windows.back();
}

void FocusClockApp::RestorePromotedWhitelistWindows() {
    for (auto const& [window, style] : promotedWindows_) {
        RestoreWhitelistWindow(window, style);
    }
    promotedWindows_.clear();
}

void FocusClockApp::RestoreWhitelistWindow(HWND target, LONG_PTR savedStyle) {
    if (!target || !IsWindow(target)) {
        return;
    }

    LONG_PTR nextStyle = savedStyle;
    SetWindowLongPtrW(target, GWL_EXSTYLE, nextStyle);
    HWND insertAfter = (nextStyle & WS_EX_TOPMOST) != 0 ? HWND_TOPMOST : HWND_NOTOPMOST;
    SetWindowPos(
        target,
        insertAfter,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
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

BOOL CALLBACK FocusClockApp::EnumSelectableWindows(HWND window, LPARAM param) {
    auto* context = reinterpret_cast<WindowPathChoiceContext*>(param);
    if (!context || !context->app || !context->choices || !window || window == context->app->hwnd_) {
        return TRUE;
    }

    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER)) {
        return TRUE;
    }

    int titleLength = GetWindowTextLengthW(window);
    if (titleLength <= 0) {
        return TRUE;
    }

    std::wstring title(static_cast<size_t>(titleLength) + 1, L'\0');
    int copied = GetWindowTextW(window, title.data(), titleLength + 1);
    if (copied <= 0) {
        return TRUE;
    }
    title.resize(static_cast<size_t>(copied));
    title = Trim(title);
    if (title.empty()) {
        return TRUE;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == 0 || pid == GetCurrentProcessId()) {
        return TRUE;
    }

    std::wstring path = context->app->GetProcessImagePath(window);
    if (path.empty()) {
        return TRUE;
    }

    context->choices->push_back(WindowPathChoice{
        window,
        pid,
        title,
        path,
        BaseName(path)
    });
    return TRUE;
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

BOOL CALLBACK FocusClockApp::EnumVisibleWhitelistedWindows(HWND window, LPARAM param) {
    auto* context = reinterpret_cast<WhitelistedWindowListContext*>(param);
    if (!context || !context->app || !context->windows || !window || window == context->app->hwnd_) {
        return TRUE;
    }

    if (!IsWindowVisible(window) || IsIconic(window) || GetWindow(window, GW_OWNER)) {
        return TRUE;
    }

    std::wstring path = context->app->GetProcessImagePath(window);
    if (path.empty() || !context->app->IsExecutableWhitelisted(path)) {
        return TRUE;
    }

    context->windows->push_back(window);
    return TRUE;
}

BOOL CALLBACK FocusClockApp::EnumWhitelistedWindowsForRestore(HWND window, LPARAM param) {
    auto* app = reinterpret_cast<FocusClockApp*>(param);
    if (!app || !window || window == app->hwnd_) {
        return TRUE;
    }

    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER)) {
        return TRUE;
    }

    std::wstring path = app->GetProcessImagePath(window);
    if (path.empty() || !app->IsExecutableWhitelisted(path)) {
        return TRUE;
    }

    auto saved = app->promotedWindows_.find(window);
    LONG_PTR savedStyle = saved == app->promotedWindows_.end() ? 0 : saved->second;
    app->RestoreWhitelistWindow(window, savedStyle);
    return TRUE;
}

} // namespace

namespace {

} // namespace focus_clock
