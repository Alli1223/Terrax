// Update / install backend — see launcher_update.h.
//
// Phase 2 implements the GitHub release check (WinHTTP + a tiny hand-rolled JSON
// scan) and the install-path / logging helpers. The download + extract + launch
// flow (startInstall / launchGame) is filled in by Phase 3.

#include "launcher_update.h"

#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>

#include <string>
#include <fstream>
#include <cstdio>
#include <cctype>

#pragma comment(lib, "winhttp.lib")

// ---------------------------------------------------------------------------
// Encoding + path helpers
// ---------------------------------------------------------------------------
static std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// Optional env override so the launcher can be pointed at a fork / mirror / test
// repo (also lets the download flow be exercised against a public repo while the
// real Terrax repo is private). Falls back to the compiled-in repo.
static std::wstring repoComponent(const wchar_t* envName, const char* fallback) {
    wchar_t buf[256];
    DWORD n = GetEnvironmentVariableW(envName, buf, 256);
    if (n > 0 && n < 256) return std::wstring(buf);
    return utf8ToWide(fallback);
}

static bool endsWithCI(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    size_t off = s.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i)
        if (std::tolower((unsigned char)s[off + i]) != std::tolower((unsigned char)suffix[i]))
            return false;
    return true;
}

std::wstring Updater::baseDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    std::wstring base = (n > 0 && n < MAX_PATH) ? std::wstring(buf) : L".";
    return base + L"\\Terrax";
}
std::wstring Updater::installDir()  { return baseDir() + L"\\current"; }
std::wstring Updater::versionFile() { return baseDir() + L"\\installed_version.txt"; }

void launcherLog(const std::string& line) {
    CreateDirectoryW(Updater::baseDir().c_str(), nullptr);
    std::wstring path = Updater::baseDir() + L"\\launcher.log";
    std::ofstream f(path, std::ios::app);
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    char ts[32];
    std::snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d ",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    f << ts << line << "\n";
}

std::string Updater::readInstalledVersion() {
    std::ifstream f(versionFile());
    if (!f) return "";
    std::string v;
    std::getline(f, v);
    while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' ' || v.back() == '\t'))
        v.pop_back();
    size_t s = 0;
    while (s < v.size() && (v[s] == ' ' || v[s] == '\t')) ++s;
    return v.substr(s);
}

// ---------------------------------------------------------------------------
// WinHTTP GET (HTTPS). Follows redirects (default policy). Returns the body and
// sets *codeOut to the HTTP status; result is true only on HTTP 200.
// ---------------------------------------------------------------------------
static bool httpsGet(const wchar_t* host, const wchar_t* path,
                     std::string& out, std::string& err, DWORD* codeOut = nullptr) {
    out.clear();
    HINTERNET hSession = WinHttpOpen(L"TerraxLauncher/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { err = "WinHttpOpen failed"; return false; }
    WinHttpSetTimeouts(hSession, 10000, 15000, 30000, 30000);

    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { err = "WinHttpConnect failed"; WinHttpCloseHandle(hSession); return false; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        err = "WinHttpOpenRequest failed";
        WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return false;
    }

    // GitHub requires a User-Agent (set via WinHttpOpen) and likes an explicit
    // API version + Accept header.
    const wchar_t* headers =
        L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    WinHttpAddRequestHeaders(hRequest, headers, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    bool ok = false;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD code = 0, sz = sizeof(code);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
        if (codeOut) *codeOut = code;

        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
            std::string chunk((size_t)avail, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(hRequest, &chunk[0], avail, &read) || read == 0) break;
            chunk.resize(read);
            out += chunk;
        }
        if (code == 200) ok = true;
        else { err = "HTTP " + std::to_string(code); ok = false; }
    } else {
        err = "request failed (err " + std::to_string(GetLastError()) + ")";
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

// ---------------------------------------------------------------------------
// Minimal JSON string extraction. The launcher only needs a couple of string
// fields out of the releases/latest payload, so a full parser is overkill.
// ---------------------------------------------------------------------------
static std::string jsonReadStringAt(const std::string& body, size_t openQuote) {
    std::string out;
    size_t p = openQuote + 1;
    while (p < body.size()) {
        char c = body[p];
        if (c == '\\' && p + 1 < body.size()) {
            char n = body[p + 1];
            switch (n) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'n':  out += '\n'; break;
                case 't':  out += '\t'; break;
                case 'r':  out += '\r'; break;
                case 'u':  out += '?'; p += 6; continue;   // skip \uXXXX
                default:   out += n;    break;
            }
            p += 2; continue;
        }
        if (c == '"') break;
        out += c; ++p;
    }
    return out;
}

// Returns the index of the opening quote of the string value for `key`, or npos.
static size_t jsonFindValueQuote(const std::string& body, const std::string& key, size_t from = 0) {
    std::string pat = "\"" + key + "\"";
    size_t p = body.find(pat, from);
    if (p == std::string::npos) return std::string::npos;
    p = body.find(':', p + pat.size());
    if (p == std::string::npos) return std::string::npos;
    ++p;
    while (p < body.size() && (body[p] == ' ' || body[p] == '\t' || body[p] == '\n' || body[p] == '\r')) ++p;
    if (p >= body.size() || body[p] != '"') return std::string::npos;
    return p;
}

static std::string jsonString(const std::string& body, const std::string& key) {
    size_t q = jsonFindValueQuote(body, key);
    return q == std::string::npos ? std::string() : jsonReadStringAt(body, q);
}

// First browser_download_url that ends in "win64.zip" — the Windows release asset.
static std::string findWinZipUrl(const std::string& body) {
    const std::string key = "browser_download_url";
    size_t from = 0;
    for (;;) {
        size_t q = jsonFindValueQuote(body, key, from);
        if (q == std::string::npos) break;
        std::string url = jsonReadStringAt(body, q);
        from = q + 1 + (url.empty() ? 1 : url.size());
        if (endsWithCI(url, "win64.zip"))   // matches Terrax-*-win64.zip / *.WIN64.zip
            return url;
    }
    return "";
}

// ---------------------------------------------------------------------------
// GitHub release check
// ---------------------------------------------------------------------------
bool fetchLatestRelease(ReleaseInfo& out, std::string& err) {
    std::wstring path = L"/repos/" + repoComponent(L"TERRAX_REPO_OWNER", TERRAX_REPO_OWNER) + L"/" +
                        repoComponent(L"TERRAX_REPO_NAME", TERRAX_REPO_NAME) + L"/releases/latest";
    std::string body;
    DWORD code = 0;
    if (!httpsGet(L"api.github.com", path.c_str(), body, err, &code)) {
        launcherLog("release check failed: " + err);
        return false;
    }
    out.tag       = jsonString(body, "tag_name");
    out.winZipUrl = findWinZipUrl(body);
    if (out.tag.empty()) {
        err = "could not parse release tag";
        launcherLog(err);
        return false;
    }
    launcherLog("latest tag=" + out.tag +
                " winzip=" + (out.winZipUrl.empty() ? "<none>" : out.winZipUrl));
    return true;
}

// ---------------------------------------------------------------------------
// Updater
// ---------------------------------------------------------------------------
Updater::~Updater() { joinWorker(); }

void Updater::joinWorker() { if (worker_.joinable()) worker_.join(); }

void Updater::publish(Status st, const std::string& line) {
    std::lock_guard<std::mutex> lk(m_);
    status_ = st;
    statusLine_ = line;
}

void Updater::publishProgress(float p) {
    std::lock_guard<std::mutex> lk(m_);
    progress_ = p;
}

Updater::Snapshot Updater::snapshot() {
    std::lock_guard<std::mutex> lk(m_);
    Snapshot s;
    s.status = status_;
    s.installedVersion = installed_;
    s.latestVersion = latest_;
    s.statusLine = statusLine_;
    s.progress = progress_;
    return s;
}

void Updater::startCheck() {
    if (running_.load()) return;
    joinWorker();
    running_ = true;
    {
        std::lock_guard<std::mutex> lk(m_);
        status_ = Status::Checking;
        statusLine_ = "Checking for updates\xE2\x80\xA6";
        progress_ = 0.0f;
    }
    worker_ = std::thread([this] {
        std::string installed = readInstalledVersion();
        ReleaseInfo info;
        std::string err;
        bool ok = fetchLatestRelease(info, err);

        std::lock_guard<std::mutex> lk(m_);
        installed_ = installed;
        if (!ok) {
            status_ = Status::Error;
            // A 404 on releases/latest almost always means the repo is private
            // or has no published release yet, rather than a transient outage.
            if (err.find("404") != std::string::npos)
                statusLine_ = "No public Terrax release found (is the repo public?).";
            else
                statusLine_ = "Couldn't reach GitHub (" + err + ").";
        } else {
            latest_ = info.tag;
            downloadUrl_ = info.winZipUrl;
            if (installed.empty()) {
                if (info.winZipUrl.empty()) {
                    status_ = Status::Error;
                    statusLine_ = "Latest release has no Windows build.";
                } else {
                    status_ = Status::NotInstalled;
                    statusLine_ = "Ready to install " + info.tag + ".";
                }
            } else if (installed != info.tag) {
                if (info.winZipUrl.empty()) {
                    status_ = Status::Error;
                    statusLine_ = "Update " + info.tag + " has no Windows build.";
                } else {
                    status_ = Status::UpdateAvailable;
                    statusLine_ = "Update available: " + installed + " -> " + info.tag + ".";
                }
            } else {
                status_ = Status::UpToDate;
                statusLine_ = "You're up to date - " + info.tag + ".";
            }
        }
        running_ = false;
    });
}

// ---------------------------------------------------------------------------
// Download + install helpers
// ---------------------------------------------------------------------------

// Stream an HTTPS URL straight to a file, following redirects (GitHub's
// browser_download_url 302s to a CDN host) and reporting progress as it goes.
static bool downloadToFile(const std::string& url, const std::wstring& dest,
                           Updater* self, std::string& err) {
    std::wstring wurl = utf8ToWide(url);
    URL_COMPONENTS uc;
    ZeroMemory(&uc, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = (DWORD)-1;
    uc.dwHostNameLength = (DWORD)-1;
    uc.dwUrlPathLength = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc)) { err = "bad url"; return false; }
    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    bool secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    HINTERNET hSession = WinHttpOpen(L"TerraxLauncher/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { err = "WinHttpOpen failed"; return false; }
    WinHttpSetTimeouts(hSession, 10000, 15000, 30000, 60000);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), uc.nPort, 0);
    if (!hConnect) { err = "connect failed"; WinHttpCloseHandle(hSession); return false; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) { err = "open request failed"; WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    bool ok = false;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD code = 0, sz = sizeof(code);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
        if (code != 200) {
            err = "HTTP " + std::to_string(code);
        } else {
            DWORD total = 0, csz = sizeof(total);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &total, &csz, WINHTTP_NO_HEADER_INDEX);
            hFile = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile == INVALID_HANDLE_VALUE) {
                err = "cannot create file";
            } else {
                static char buf[65536];
                ULONGLONG done = 0;
                bool failed = false;
                for (;;) {
                    DWORD avail = 0;
                    if (!WinHttpQueryDataAvailable(hRequest, &avail)) { failed = true; err = "read failed"; break; }
                    if (avail == 0) break;
                    DWORD toRead = avail < sizeof(buf) ? avail : (DWORD)sizeof(buf);
                    DWORD got = 0;
                    if (!WinHttpReadData(hRequest, buf, toRead, &got) || got == 0) { failed = true; err = "read failed"; break; }
                    DWORD wrote = 0;
                    if (!WriteFile(hFile, buf, got, &wrote, nullptr) || wrote != got) { failed = true; err = "disk write failed"; break; }
                    done += got;
                    if (self) self->publishProgress(total ? (float)((double)done / (double)total) : 0.0f);
                }
                ok = !failed;
            }
        }
    } else {
        err = "request failed (err " + std::to_string(GetLastError()) + ")";
    }

    if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

// Extract a .zip with the bundled bsdtar (tar.exe, present on Windows 10 1803+).
static bool extractZip(const std::wstring& zip, const std::wstring& destDir, std::string& err) {
    wchar_t sysbuf[MAX_PATH];
    UINT n = GetSystemDirectoryW(sysbuf, MAX_PATH);
    std::wstring tarExe = (n > 0 && n < MAX_PATH) ? std::wstring(sysbuf) + L"\\tar.exe" : L"tar.exe";
    std::wstring cmd = L"\"" + tarExe + L"\" -xf \"" + zip + L"\" -C \"" + destDir + L"\"";

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
        err = "could not run tar.exe (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ec = 1;
    GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (ec != 0) { err = "tar exited " + std::to_string(ec); return false; }
    return true;
}

// Best-effort recursive delete (so an update doesn't leave files from the old
// version behind). Silent: a locked file just survives and is overwritten.
static void removeDirRecursive(const std::wstring& dir) {
    if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    std::wstring from = dir;
    from.push_back(L'\0');                 // SHFileOperation wants a double-null list
    SHFILEOPSTRUCTW op;
    ZeroMemory(&op, sizeof(op));
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NO_UI;
    SHFileOperationW(&op);
}

static bool writeInstalledVersion(const std::string& tag) {
    std::ofstream f(Updater::versionFile(), std::ios::trunc);
    if (!f) return false;
    f << tag << "\n";
    return true;
}

// ---------------------------------------------------------------------------
// Install / launch
// ---------------------------------------------------------------------------
void Updater::startInstall() {
    if (running_.load()) return;
    joinWorker();

    std::string url, tag;
    {
        std::lock_guard<std::mutex> lk(m_);
        url = downloadUrl_;
        tag = latest_;
    }
    if (url.empty()) { publish(Status::Error, "No Windows download available."); return; }

    running_ = true;
    {
        std::lock_guard<std::mutex> lk(m_);
        status_ = Status::Downloading;
        statusLine_ = "Downloading " + tag + "\xE2\x80\xA6";
        progress_ = 0.0f;
    }

    worker_ = std::thread([this, url, tag] {
        std::string err;
        CreateDirectoryW(baseDir().c_str(), nullptr);
        std::wstring zip = baseDir() + L"\\download.zip";

        launcherLog("download " + tag + " <- " + url);
        if (!downloadToFile(url, zip, this, err)) {
            launcherLog("download failed: " + err);
            publish(Status::Error, "Download failed (" + err + ").");
            running_ = false;
            return;
        }

        publish(Status::Installing, "Installing " + tag + "\xE2\x80\xA6");
        publishProgress(1.0f);

        removeDirRecursive(installDir());
        CreateDirectoryW(installDir().c_str(), nullptr);
        if (!extractZip(zip, installDir(), err)) {
            launcherLog("extract failed: " + err);
            publish(Status::Error, "Install failed (" + err + ").");
            running_ = false;
            return;
        }
        DeleteFileW(zip.c_str());
        writeInstalledVersion(tag);

        {
            std::lock_guard<std::mutex> lk(m_);
            installed_ = tag;
            latest_ = tag;
            status_ = Status::UpToDate;
            statusLine_ = "Installed " + tag + " - ready to play.";
            progress_ = 0.0f;
        }
        launcherLog("installed " + tag);
        running_ = false;
    });
}

bool Updater::launchGame() {
    std::wstring exe = installDir() + L"\\Terrax.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        publish(Status::Error, "Terrax.exe not found - try reinstalling.");
        return false;
    }
    // Working directory = install dir so the game finds its shaders\ folder
    // (the release zip ships shaders next to Terrax.exe).
    HINSTANCE r = ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr,
                                installDir().c_str(), SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) {
        publish(Status::Error, "Could not start Terrax.exe.");
        return false;
    }
    launcherLog("launched game");
    return true;
}

// Synchronous install for the silent `--install` CLI path (no window). Reuses the
// same download/extract building blocks as the interactive flow.
bool runInstall(std::string& tagOut, std::string& err) {
    ReleaseInfo info;
    if (!fetchLatestRelease(info, err)) return false;
    if (info.winZipUrl.empty()) { err = "no Windows asset in latest release"; return false; }
    tagOut = info.tag;

    CreateDirectoryW(Updater::baseDir().c_str(), nullptr);
    std::wstring zip = Updater::baseDir() + L"\\download.zip";
    if (!downloadToFile(info.winZipUrl, zip, nullptr, err)) return false;

    removeDirRecursive(Updater::installDir());
    CreateDirectoryW(Updater::installDir().c_str(), nullptr);
    if (!extractZip(zip, Updater::installDir(), err)) return false;
    DeleteFileW(zip.c_str());
    writeInstalledVersion(info.tag);
    launcherLog("silent install complete: " + info.tag);
    return true;
}
