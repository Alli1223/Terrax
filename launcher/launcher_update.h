// Update / install backend for the Terrax launcher.
//
// Updater owns a single background worker thread that does all the blocking work
// (the GitHub release query in Phase 2; the download + extract in Phase 3) and
// publishes its progress through a mutex-guarded Snapshot the UI thread reads
// once per frame. The UI never blocks and never touches the network directly.

#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <atomic>

// Where the launcher looks for a Windows release on GitHub.
#define TERRAX_REPO_OWNER "Alli1223"
#define TERRAX_REPO_NAME  "Terrax"

enum class Status {
    Checking,          // querying GitHub
    UpToDate,          // installed == latest
    UpdateAvailable,   // installed != latest
    NotInstalled,      // nothing installed yet
    Downloading,       // pulling the release zip
    Installing,        // extracting / writing files
    Error              // network or install failure (statusLine has detail)
};

// Parsed subset of the GitHub releases/latest payload.
struct ReleaseInfo {
    std::string tag;         // e.g. "v0.1.0"
    std::string winZipUrl;   // browser_download_url of the *-win64.zip asset
};

class Updater {
public:
    // Immutable view of launcher state, copied under the lock for the UI thread.
    struct Snapshot {
        Status      status = Status::Checking;
        std::string installedVersion;   // "v0.1.0" or empty
        std::string latestVersion;      // "v0.1.0" or empty
        std::string statusLine = "Checking for updates\xE2\x80\xA6";
        float       progress = 0.0f;    // 0..1 while downloading
    };

    ~Updater();

    void startCheck();      // kick a background GitHub release check
    void startInstall();    // kick a background download + extract + record version
    bool launchGame();      // run the installed Terrax.exe; true if it started

    Snapshot snapshot();
    bool busy() const { return running_.load(); }

    // Thread-safe progress setter (0..1), called by the background downloader.
    void publishProgress(float p);

    // Install locations (wide for Win32 file APIs; robust to non-ASCII paths).
    static std::wstring baseDir();       // %LOCALAPPDATA%\Terrax
    static std::wstring installDir();    // baseDir()\current
    static std::wstring versionFile();   // baseDir()\installed_version.txt
    static std::string  readInstalledVersion();   // tag string, or "" if none

private:
    void publish(Status st, const std::string& line);
    void joinWorker();

    std::thread        worker_;
    mutable std::mutex m_;
    Status             status_ = Status::Checking;
    std::string        installed_;
    std::string        latest_;
    std::string        downloadUrl_;
    std::string        statusLine_ = "Checking for updates\xE2\x80\xA6";
    float              progress_ = 0.0f;
    std::atomic<bool>  running_{false};
};

// Lower-level building blocks (also used by the install flow in Phase 3).
bool fetchLatestRelease(ReleaseInfo& out, std::string& err);
void launcherLog(const std::string& line);

// Synchronous install used by the silent `--install` CLI path: check the latest
// release, download the Windows zip, extract it, and record the version. Returns
// true on success; tagOut receives the installed tag.
bool runInstall(std::string& tagOut, std::string& err);
