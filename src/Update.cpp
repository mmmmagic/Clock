#include "FocusClockApp.h"
#include <wincrypt.h>

namespace focus_clock {

namespace {

std::wstring TrimDigestText(std::wstring value) {
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

std::wstring NormalizeSha256Digest(std::wstring value) {
    value = TrimDigestText(std::move(value));
    constexpr const wchar_t* prefix = L"sha256:";
    if (value.size() > 7 && _wcsnicmp(value.c_str(), prefix, 7) == 0) {
        value.erase(0, 7);
    }

    std::wstring normalized;
    normalized.reserve(value.size());
    for (wchar_t ch : value) {
        if (std::iswspace(ch)) {
            continue;
        }
        normalized.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }
    return normalized;
}

bool IsHexDigest(const std::wstring& value, size_t expectedLength) {
    if (value.size() != expectedLength) {
        return false;
    }

    return std::all_of(value.begin(), value.end(), [](wchar_t ch) {
        return (ch >= L'0' && ch <= L'9') ||
            (ch >= L'a' && ch <= L'f') ||
            (ch >= L'A' && ch <= L'F');
    });
}

std::wstring BytesToLowerHex(const BYTE* bytes, DWORD size) {
    static constexpr wchar_t kHex[] = L"0123456789abcdef";
    std::wstring hex;
    hex.reserve(static_cast<size_t>(size) * 2);
    for (DWORD i = 0; i < size; ++i) {
        hex.push_back(kHex[(bytes[i] >> 4) & 0x0F]);
        hex.push_back(kHex[bytes[i] & 0x0F]);
    }
    return hex;
}

bool ComputeFileSha256(const std::wstring& path, std::wstring& digest) {
    digest.clear();

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    bool ok = CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) != FALSE;
    if (ok) {
        ok = CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) != FALSE;
    }

    std::array<BYTE, 64 * 1024> buffer{};
    while (ok) {
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
            ok = false;
            break;
        }
        if (read == 0) {
            break;
        }
        ok = CryptHashData(hash, buffer.data(), read, 0) != FALSE;
    }

    if (ok) {
        DWORD hashSize = 32;
        std::array<BYTE, 32> hashBytes{};
        ok = CryptGetHashParam(hash, HP_HASHVAL, hashBytes.data(), &hashSize, 0) != FALSE && hashSize == hashBytes.size();
        if (ok) {
            digest = BytesToLowerHex(hashBytes.data(), hashSize);
        }
    }

    if (hash) {
        CryptDestroyHash(hash);
    }
    if (provider) {
        CryptReleaseContext(provider, 0);
    }
    CloseHandle(file);
    return ok;
}

bool VerifyDownloadedAsset(const std::wstring& path, const std::wstring& expectedDigest, std::wstring& actualDigest) {
    actualDigest.clear();
    std::wstring normalizedExpected = NormalizeSha256Digest(expectedDigest);
    if (!IsHexDigest(normalizedExpected, 64)) {
        return false;
    }
    if (!ComputeFileSha256(path, actualDigest)) {
        return false;
    }
    return NormalizeSha256Digest(actualDigest) == normalizedExpected;
}

constexpr std::array<UpdateSource, 5> kTrustedUpdateSources{ {
    { 1, L"gh-proxy.org", L"https://gh-proxy.org/" },
    { 2, L"GitHub", L"" },
    { 3, L"v4.gh-proxy.org", L"https://v4.gh-proxy.org/" },
    { 4, L"v6.gh-proxy.org", L"https://v6.gh-proxy.org/" },
    { 5, L"cdn.gh-proxy.org", L"https://cdn.gh-proxy.org/" }
} };

bool IsUsableRelease(const UpdateSourceProbe& probe) {
    return probe.response.ok &&
        probe.release.ok &&
        !probe.release.browserDownloadUrl.empty() &&
        IsHexDigest(NormalizeSha256Digest(probe.release.assetDigest), 64);
}

template <typename T>
bool PostOwnedPointer(HWND target, UINT message, WPARAM wparam, T* value) {
    if (target && IsWindow(target) && PostMessageW(target, message, wparam, reinterpret_cast<LPARAM>(value))) {
        return true;
    }

    delete value;
    return false;
}

} // namespace

void FocusClockApp::StartUpdateCheck(bool force) {
    if (updateCheckInProgress_ || (!force && updateCheckStarted_)) {
        return;
    }

    updateCheckStarted_ = true;
    updateCheckInProgress_ = true;
    updateAvailable_ = false;
    updateDownloadUrl_.clear();
    updateAssetName_ = L"FocusClock.exe";
    updateAssetDigest_.clear();
    updateMessage_ = L"正在获取版本信息...";
    updateMessageIsError_ = false;
    RebuildLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);

    HWND target = hwnd_;
    std::thread([target]() {
        std::vector<std::future<UpdateSourceProbe>> probes;
        probes.reserve(kTrustedUpdateSources.size());
        for (const auto& source : kTrustedUpdateSources) {
            probes.push_back(std::async(std::launch::async, [source]() {
                UpdateSourceProbe probe{};
                probe.source = source;
                std::wstring apiUrl = std::wstring(source.prefix) + kLatestReleaseUrl;
                probe.response = HttpGetText(apiUrl, kUpdateApiTimeoutMs);
                if (probe.response.ok) {
                    probe.release = ParseReleaseInfo(probe.response.body);
                }
                return probe;
            }));
        }

        DWORD fastestMs = std::numeric_limits<DWORD>::max();
        ReleaseInfo selectedRelease{};
        for (auto& probeFuture : probes) {
            UpdateSourceProbe probe = probeFuture.get();
            if (!IsUsableRelease(probe)) {
                continue;
            }

            if (probe.response.elapsedMs < fastestMs) {
                fastestMs = probe.response.elapsedMs;
                selectedRelease = std::move(probe.release);
            }
        }

        ReleaseInfo* release = new ReleaseInfo(std::move(selectedRelease));
        PostOwnedPointer(target, kUpdateCheckFinishedMessage, 0, release);
    }).detach();
}

void FocusClockApp::HandleUpdateCheckResult(ReleaseInfo* release) {
    std::unique_ptr<ReleaseInfo> result(release);
    updateCheckInProgress_ = false;
    updateAvailable_ = false;
    updateDownloadUrl_.clear();

    if (!result || !result->ok) {
        updateMessage_ = L"版本获取失败，请检查更新源或发布资产校验信息";
        updateMessageIsError_ = true;
    } else {
        std::wstring executablePath(MAX_PATH, L'\0');
        DWORD length = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
        while (length == executablePath.size()) {
            executablePath.resize(executablePath.size() * 2);
            length = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
        }

        WIN32_FILE_ATTRIBUTE_DATA executableData{};
        bool hasExecutableWriteTime = length > 0;
        if (hasExecutableWriteTime) {
            executablePath.resize(length);
            hasExecutableWriteTime = GetFileAttributesExW(executablePath.c_str(), GetFileExInfoStandard, &executableData) != FALSE;
        }

        if (!hasExecutableWriteTime) {
            updateMessage_ = L"版本检查失败：无法读取本地程序修改时间";
            updateMessageIsError_ = true;
        } else if (result->publishedUnix > FileTimeToUnixSeconds(executableData.ftLastWriteTime)) {
            std::wstring normalizedDigest = NormalizeSha256Digest(result->assetDigest);
            if (!IsHexDigest(normalizedDigest, 64)) {
                updateMessage_ = L"发现新版本，但发布资产缺少 SHA-256 校验信息，已阻止自动更新";
                updateMessageIsError_ = true;
            } else {
                updateAvailable_ = true;
                updateDownloadUrl_ = result->browserDownloadUrl;
                updateAssetName_ = result->assetName.empty() ? L"FocusClock.exe" : result->assetName;
                updateAssetDigest_ = normalizedDigest;
                updateMessage_ = result->body.empty() ? L"发现新版本" : result->body;
                updateMessageIsError_ = false;
            }
        } else {
            updateMessage_ = L"您已是最新版本";
            updateMessageIsError_ = false;
        }
    }

    RebuildLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void FocusClockApp::StartUpdateDownload() {
    if (!updateAvailable_ || updateDownloadInProgress_ || updateDownloadUrl_.empty()) {
        return;
    }

    updateDownloadInProgress_ = true;
    updateMessage_ = L"正在选择最快加载源...";
    updateMessageIsError_ = false;
    RebuildLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);

    HWND target = hwnd_;
    std::wstring assetName = updateAssetName_;
    std::wstring downloadUrl = updateDownloadUrl_;
    std::wstring expectedDigest = updateAssetDigest_;
    std::wstring exeDirectory = GetExecutableDirectory();
    std::thread([target, assetName = std::move(assetName), downloadUrl = std::move(downloadUrl), expectedDigest = std::move(expectedDigest), exeDirectory = std::move(exeDirectory)]() {
        UpdateDownloadResult* result = new UpdateDownloadResult();
        auto postLog = [target](const std::wstring& message, bool isError = false) {
            PostOwnedPointer(target, kUpdateLogMessage, isError ? 1 : 0, new std::wstring(message));
        };

        std::wstring normalizedExpectedDigest = NormalizeSha256Digest(expectedDigest);
        if (!IsHexDigest(normalizedExpectedDigest, 64)) {
            result->ok = false;
            result->message = L"更新被取消：发布资产缺少 SHA-256 校验信息";
        PostOwnedPointer(target, kUpdateDownloadFinishedMessage, 0, result);
            return;
        }

        postLog(L"开始测速更新源...");

        std::vector<std::future<UpdateSourceProbe>> probes;
        probes.reserve(kTrustedUpdateSources.size());
        for (const auto& source : kTrustedUpdateSources) {
            probes.push_back(std::async(std::launch::async, [source]() {
                UpdateSourceProbe probe{};
                probe.source = source;
                std::wstring apiUrl = std::wstring(source.prefix) + kLatestReleaseUrl;
                probe.response = HttpGetText(apiUrl, kUpdateApiTimeoutMs);
                if (probe.response.ok) {
                    probe.release = ParseReleaseInfo(probe.response.body);
                }
                return probe;
            }));
        }

        DWORD fastestMs = std::numeric_limits<DWORD>::max();
        UpdateSource fastestSource{};
        bool hasFastestSource = false;
        ReleaseInfo fastestRelease{};

        for (auto& probeFuture : probes) {
            UpdateSourceProbe probe = probeFuture.get();
            if (!IsUsableRelease(probe)) {
                postLog(L"线路" + std::to_wstring(probe.source.order) + L"测速失败：" + probe.source.name);
                continue;
            }

            postLog(L"线路" + std::to_wstring(probe.source.order) + L"测速完成，响应 " + std::to_wstring(probe.response.elapsedMs) + L"ms");

            if (probe.response.elapsedMs < fastestMs) {
                fastestMs = probe.response.elapsedMs;
                fastestSource = probe.source;
                hasFastestSource = true;
                fastestRelease = std::move(probe.release);
            }
        }

        if (!hasFastestSource) {
            result->ok = false;
            result->message = L"版本获取失败，请检查网络";
            PostOwnedPointer(target, kUpdateDownloadFinishedMessage, 0, result);
            return;
        }

        postLog(L"测速完成，选择线路" + std::to_wstring(fastestSource.order) + L"（" + fastestSource.name + L"）");

        std::wstring selectedAssetName = assetName;
        selectedAssetName = SanitizeFileName(selectedAssetName);
        if (selectedAssetName.empty()) {
            selectedAssetName = L"FocusClock.exe";
        }

        std::wstring currentExe = JoinPath(exeDirectory, L"FocusClock.exe");
        std::wstring outputName = selectedAssetName;
        if (_wcsicmp(JoinPath(exeDirectory, outputName).c_str(), currentExe.c_str()) == 0) {
            outputName = StripExtension(outputName) + L".new.exe";
        }

        std::wstring outputPath = JoinPath(exeDirectory, outputName);
        std::wstring backupPath = JoinPath(exeDirectory, L"FocusClock.old.exe");

        std::vector<std::wstring> downloadPrefixes;
        auto addDownloadPrefix = [&downloadPrefixes](const wchar_t* prefix) {
            std::wstring value = prefix ? prefix : L"";
            if (std::find(downloadPrefixes.begin(), downloadPrefixes.end(), value) == downloadPrefixes.end()) {
                downloadPrefixes.push_back(value);
            }
        };
        addDownloadPrefix(L"https://gh-proxy.org/");
        addDownloadPrefix(fastestSource.prefix);
        for (const auto& source : kTrustedUpdateSources) {
            addDownloadPrefix(source.prefix);
        }

        bool downloaded = false;
        std::wstring lastDigest;
        for (const auto& prefix : downloadPrefixes) {
            DeleteFileW(outputPath.c_str());
            std::wstring selectedDownloadUrl = prefix + downloadUrl;
            std::wstring lineName = prefix.empty() ? L"GitHub" : (prefix == L"https://gh-proxy.org/" ? L"线路1" : prefix);
            postLog(L"开始下载：" + lineName);
            std::wstring curlCommand =
                L"curl.exe -L --fail --connect-timeout 15 --max-time 300 -o " +
                QuoteCommandArgument(outputPath) + L" " +
                QuoteCommandArgument(selectedDownloadUrl);

            DWORD curlExitCode = 1;
            if (TryRunCommand(curlCommand, kUpdateDownloadTimeoutMs, curlExitCode) && curlExitCode == 0) {
                postLog(L"下载完成，正在校验文件...");
                if (!VerifyDownloadedAsset(outputPath, normalizedExpectedDigest, lastDigest)) {
                    DeleteFileW(outputPath.c_str());
                    postLog(lastDigest.empty() ? L"校验失败，尝试下一条线路" : L"SHA-256 不匹配，尝试下一条线路", true);
                    continue;
                }

                downloaded = true;
                postLog(L"校验通过，准备替换程序...");
                break;
            }
            postLog(L"下载失败，尝试下一条线路", true);
        }

        if (!downloaded) {
            DeleteFileW(outputPath.c_str());
            result->ok = false;
            result->message = L"下载失败，请检查网络";
            PostOwnedPointer(target, kUpdateDownloadFinishedMessage, 0, result);
            return;
        }

        std::wstring powerShell =
            L"$old=" + QuotePowerShellString(currentExe) + L";"
            L"$new=" + QuotePowerShellString(outputPath) + L";"
            L"$bak=" + QuotePowerShellString(backupPath) + L";"
            L"Start-Sleep -Seconds 2;"
            L"Remove-Item -LiteralPath $bak -Force -ErrorAction SilentlyContinue;"
            L"$replaced=$false;"
            L"for($i=0;$i -lt 15 -and -not $replaced;$i++){"
            L"try{"
            L"Move-Item -LiteralPath $old -Destination $bak -Force -ErrorAction Stop;"
            L"Move-Item -LiteralPath $new -Destination $old -Force -ErrorAction Stop;"
            L"$replaced=$true"
            L"}catch{"
            L"if((Test-Path -LiteralPath $bak) -and -not (Test-Path -LiteralPath $old)){"
            L"Move-Item -LiteralPath $bak -Destination $old -Force -ErrorAction SilentlyContinue"
            L"}"
            L"Start-Sleep -Seconds 1"
            L"}"
            L"}"
            L"if($replaced){"
            L"Start-Process -FilePath $old;"
            L"for($i=0;$i -lt 30 -and (Test-Path -LiteralPath $bak);$i++){"
            L"Remove-Item -LiteralPath $bak -Force -ErrorAction SilentlyContinue;"
            L"if(Test-Path -LiteralPath $bak){Start-Sleep -Seconds 1}"
            L"}"
            L"}";
        std::wstring powershellExe = L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
        std::wstring powershellArguments =
            L"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command " +
            QuoteCommandArgument(powerShell);
        if (!LaunchDetachedProcess(powershellExe, powershellArguments)) {
            result->ok = false;
            result->message = L"更新脚本启动失败";
            PostOwnedPointer(target, kUpdateDownloadFinishedMessage, 0, result);
            return;
        }

        result->ok = true;
        result->message = L"下载完成，正在替换程序...";
        PostOwnedPointer(target, kUpdateDownloadFinishedMessage, 0, result);
    }).detach();
}

void FocusClockApp::HandleUpdateDownloadResult(UpdateDownloadResult* result) {
    std::unique_ptr<UpdateDownloadResult> downloadResult(result);
    updateDownloadInProgress_ = false;

    if (downloadResult && downloadResult->ok) {
        updateMessage_ = downloadResult->message;
        updateMessageIsError_ = false;
        DestroyWindow(hwnd_);
        return;
    }

    updateMessage_ = downloadResult ? downloadResult->message : L"下载失败，请检查网络";
    updateMessageIsError_ = true;
    RebuildLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void FocusClockApp::AppendUpdateLog(const std::wstring& message, bool isError) {
    if (message.empty()) {
        return;
    }

    constexpr size_t kMaxLogLines = 10;
    std::wstring combined = updateMessage_;
    if (!combined.empty() && combined.back() != L'\n' && combined.back() != L'\r') {
        combined += L"\r\n";
    }
    combined += message;

    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= combined.size()) {
        size_t end = combined.find(L'\n', start);
        std::wstring line = combined.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            lines.push_back(std::move(line));
        }
        if (end == std::wstring::npos) {
            break;
        }
        start = end + 1;
    }

    if (lines.size() > kMaxLogLines) {
        lines.erase(lines.begin(), lines.end() - kMaxLogLines);
    }

    updateMessage_.clear();
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            updateMessage_ += L"\r\n";
        }
        updateMessage_ += lines[i];
    }
    updateMessageIsError_ = isError;
    RebuildLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void FocusClockApp::CleanupUpdateArtifacts() const {
    std::wstring backupPath = JoinPath(GetExecutableDirectory(), L"FocusClock.old.exe");
    for (int i = 0; i < 3 && GetFileAttributesW(backupPath.c_str()) != INVALID_FILE_ATTRIBUTES; ++i) {
        if (DeleteFileW(backupPath.c_str())) {
            break;
        }
        Sleep(100);
    }
}

HttpTextResult FocusClockApp::HttpGetText(const std::wstring& url, DWORD timeoutMs) {
    HttpTextResult result{};
    auto started = std::chrono::steady_clock::now();

    URL_COMPONENTSW parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts)) {
        return result;
    }

    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength > 0) {
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }
    if (path.empty()) {
        path = L"/";
    }

    HINTERNET session = WinHttpOpen(
        L"FocusClock/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session) {
        return result;
    }

    DWORD timeout = timeoutMs;
    WinHttpSetTimeouts(session, timeout, timeout, timeout, timeout);

    HINTERNET connection = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        return result;
    }

    DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(
        connection,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);
    if (!request) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    LPCWSTR headers = L"Accept: application/vnd.github+json\r\nUser-Agent: FocusClock\r\n";
    BOOL ok = WinHttpSendRequest(
        request,
        headers,
        static_cast<DWORD>(-1),
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0);
    if (ok) {
        ok = WinHttpReceiveResponse(request, nullptr);
    }

    if (ok) {
        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &statusSize,
            WINHTTP_NO_HEADER_INDEX);
        result.statusCode = statusCode;

        std::vector<char> bytes;
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
            size_t oldSize = bytes.size();
            bytes.resize(oldSize + available);
            DWORD read = 0;
            if (!WinHttpReadData(request, bytes.data() + oldSize, available, &read)) {
                break;
            }
            bytes.resize(oldSize + read);
            if (read == 0) {
                break;
            }
        }

        result.body = DecodeTextFile(bytes);
        result.ok = statusCode >= 200 && statusCode < 300 && !result.body.empty();
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    result.elapsedMs = elapsed < 0 ? 0 : static_cast<DWORD>(std::min<long long>(elapsed, std::numeric_limits<DWORD>::max()));
    return result;
}

ReleaseInfo FocusClockApp::ParseReleaseInfo(const std::wstring& text) {
    ReleaseInfo info{};
    if (text.empty()) {
        return info;
    }

    TryParseJsonStringField(text, L"body", info.body);
    TryParseJsonStringField(text, L"published_at", info.publishedAt);
    TryParseJsonStringField(text, L"browser_download_url", info.browserDownloadUrl);
    if (info.browserDownloadUrl.empty()) {
        return info;
    }

    size_t urlPos = text.find(L"browser_download_url");
    if (urlPos != std::wstring::npos) {
        size_t objectStart = text.rfind(L'{', urlPos);
        if (objectStart != std::wstring::npos) {
            TryParseJsonStringField(text, L"name", info.assetName, objectStart);
            TryParseJsonStringField(text, L"digest", info.assetDigest, objectStart);
        }
    }

    if (info.assetName.empty()) {
        info.assetName = BaseName(info.browserDownloadUrl);
    }

    info.ok = TryParseGithubDateTime(info.publishedAt, info.publishedUnix);
    return info;
}

bool FocusClockApp::TryParseGithubDateTime(const std::wstring& value, long long& unixSeconds) {
    if (value.size() < 20) {
        return false;
    }

    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(_wtoi(value.substr(0, 4).c_str()));
    st.wMonth = static_cast<WORD>(_wtoi(value.substr(5, 2).c_str()));
    st.wDay = static_cast<WORD>(_wtoi(value.substr(8, 2).c_str()));
    st.wHour = static_cast<WORD>(_wtoi(value.substr(11, 2).c_str()));
    st.wMinute = static_cast<WORD>(_wtoi(value.substr(14, 2).c_str()));
    st.wSecond = static_cast<WORD>(_wtoi(value.substr(17, 2).c_str()));
    st.wMilliseconds = 0;

    FILETIME ft{};
    if (!SystemTimeToFileTime(&st, &ft)) {
        return false;
    }

    unixSeconds = FileTimeToUnixSeconds(ft);
    return unixSeconds > 0;
}

bool FocusClockApp::TryParseJsonStringField(const std::wstring& json, const std::wstring& key, std::wstring& value, size_t startAt) {
    std::wstring pattern = L"\"" + key + L"\"";
    size_t keyPos = json.find(pattern, startAt);
    if (keyPos == std::wstring::npos) {
        return false;
    }

    size_t colon = json.find(L':', keyPos + pattern.size());
    if (colon == std::wstring::npos) {
        return false;
    }

    size_t quote = json.find(L'"', colon + 1);
    if (quote == std::wstring::npos) {
        return false;
    }

    std::wstring parsed;
    for (size_t i = quote + 1; i < json.size(); ++i) {
        wchar_t ch = json[i];
        if (ch == L'"') {
            value = parsed;
            return true;
        }
        if (ch != L'\\') {
            parsed.push_back(ch);
            continue;
        }

        if (++i >= json.size()) {
            break;
        }
        wchar_t escaped = json[i];
        switch (escaped) {
        case L'"':
        case L'\\':
        case L'/':
            parsed.push_back(escaped);
            break;
        case L'b':
            parsed.push_back(L'\b');
            break;
        case L'f':
            parsed.push_back(L'\f');
            break;
        case L'n':
            parsed.push_back(L'\n');
            break;
        case L'r':
            parsed.push_back(L'\r');
            break;
        case L't':
            parsed.push_back(L'\t');
            break;
        case L'u':
            if (i + 4 < json.size()) {
                unsigned int code = 0;
                for (int n = 0; n < 4; ++n) {
                    wchar_t hex = json[++i];
                    code <<= 4;
                    if (hex >= L'0' && hex <= L'9') {
                        code += hex - L'0';
                    } else if (hex >= L'a' && hex <= L'f') {
                        code += hex - L'a' + 10;
                    } else if (hex >= L'A' && hex <= L'F') {
                        code += hex - L'A' + 10;
                    }
                }
                parsed.push_back(static_cast<wchar_t>(code));
            }
            break;
        default:
            parsed.push_back(escaped);
            break;
        }
    }

    return false;
}

} // namespace focus_clock
