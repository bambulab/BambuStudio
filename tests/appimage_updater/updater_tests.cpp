#define CATCH_CONFIG_MAIN
#define CATCH_CONFIG_NO_POSIX_SIGNALS
#include <catch2/catch.hpp>
#include "slic3r/Utils/AppImageUpdater.hpp"
#include "slic3r/Utils/DesktopUpdater.hpp"
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <chrono>
#include <thread>
#include <fcntl.h>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace Slic3r::AppImageUpdater;
using nlohmann::json;

namespace {
const std::string repo = "example/BambuStudio";
const std::string asset = "BambuStudio_linux-x86_64.AppImage";
std::string image_bytes(char payload)
{
    std::string bytes(4096, payload);
    bytes.replace(0, 4, "\177ELF");
    bytes[4] = 2; bytes[5] = 1;
    bytes[8] = 'A'; bytes[9] = 'I'; bytes[10] = 2;
    bytes[18] = 62; bytes[19] = 0;
    return bytes;
}
std::string sha256(const std::string &data)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned size = 0;
    REQUIRE(EVP_Digest(data.data(), data.size(), digest, &size, EVP_sha256(), nullptr) == 1);
    std::string hex;
    for (unsigned i = 0; i < size; ++i) {
        hex += "0123456789abcdef"[digest[i] >> 4];
        hex += "0123456789abcdef"[digest[i] & 15];
    }
    return hex;
}
Release release_for(const std::string &bytes)
{
    return {"02.09.00.01", "release notes", "https://example.invalid/update", sha256(bytes), bytes.size()};
}
json release_json(const std::string &tag, bool prerelease = false)
{
    const auto release = release_for(image_bytes('n'));
    return {{"tag_name", tag}, {"body", "notes"}, {"draft", false}, {"prerelease", prerelease},
            {"assets", json::array({{{"name", asset}, {"size", release.size}, {"digest", "sha256:" + release.sha256},
                {"browser_download_url", "https://github.com/" + repo + "/releases/download/" + tag + "/" + asset}}})}};
}
struct Fixture {
    fs::path dir, image;
    const std::string old_bytes = image_bytes('o'), new_bytes = image_bytes('n');
    Fixture()
    {
        std::string pattern = (fs::temp_directory_path() / "bambu-updater-XXXXXX").string();
        auto *name = mkdtemp(pattern.data());
        if (!name) throw std::runtime_error("mkdtemp failed");
        dir = name;
        // Spaces, quotes, $, and a non-ASCII character exercise filesystem paths.
        image = dir / "Bambu 'Studio' $é.AppImage";
        write(image, old_bytes);
        chmod(image.c_str(), 0755);
    }
    ~Fixture() { fs::remove_all(dir); }
    static void write(const fs::path &path, const std::string &bytes)
    {
        std::ofstream stream(path, std::ios::binary);
        stream.write(bytes.data(), bytes.size());
    }
    static std::string read(const fs::path &path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }
    bool no_staging() const
    {
        for (const auto &entry : fs::directory_iterator(dir))
            if (entry.path().filename().string().find(".update-") != std::string::npos) return false;
        return true;
    }
};
}

TEST_CASE("Release selection respects numeric versions and the beta preference")
{
    const auto releases = json::array({release_json("v02.09.00.01", true), release_json("v02.08.10.50"), release_json("v02.08.02.61")});
    REQUIRE(select_release(releases.dump(), repo, asset, "02.08.04.57", false)->version == "02.08.10.50");
    REQUIRE(select_release(releases.dump(), repo, asset, "02.08.04.57", true)->version == "02.09.00.01");
    REQUIRE_FALSE(select_release(releases.dump(), repo, asset, "02.09.00.01", true));
    auto draft = release_json("v03.00.00.01");
    draft["draft"] = true;
    REQUIRE_FALSE(select_release(json::array({draft}).dump(), repo, asset, "02.08.04.57", true));
}

TEST_CASE("Release metadata must name the configured asset and trusted repository")
{
    auto release = release_json("v02.09.00.01");
    SECTION("Other platform") { release["assets"][0]["name"] = "Other.AppImage";
        REQUIRE_FALSE(select_release(json::array({release}).dump(), repo, asset, "02.08.04.57", false)); return; }
    SECTION("Other host") { release["assets"][0]["browser_download_url"] = "https://evil.example/payload"; }
    SECTION("No digest") { release["assets"][0].erase("digest"); }
    SECTION("Null digest") { release["assets"][0]["digest"] = nullptr; }
    SECTION("Invalid hash") { release["assets"][0]["digest"] = "sha256:123"; }
    SECTION("Oversized image") { release["assets"][0]["size"] = 3ULL * 1024 * 1024 * 1024; }
    SECTION("Negative size") { release["assets"][0]["size"] = -1; }
    REQUIRE_THROWS(select_release(json::array({release}).dump(), repo, asset, "02.08.04.57", false));
}

TEST_CASE("Malformed responses and invalid build configuration are rejected")
{
    REQUIRE_THROWS(select_release("{", repo, asset, "02.08.04.57", false));
    REQUIRE_THROWS(select_release("{}", repo, asset, "02.08.04.57", false));
    REQUIRE_THROWS(select_release("[]", "example/repo/../other", asset, "02.08.04.57", false));
    REQUIRE_THROWS(select_release("[]", repo, "../payload.AppImage", "02.08.04.57", false));
    auto invalid = release_json("not-a-version");
    REQUIRE_FALSE(select_release(json::array({invalid}).dump(), repo, asset, "02.08.04.57", false));
}

TEST_CASE("Verified update replaces the image atomically and keeps an executable backup")
{
    Fixture f;
    const int running = open(f.image.c_str(), O_RDONLY);
    REQUIRE(running >= 0);
    {
        StagedUpdate update(f.image.string(), release_for(f.new_bytes));
        update.append(f.new_bytes.data(), 1000);
        REQUIRE(Fixture::read(f.image) == f.old_bytes);
        update.append(f.new_bytes.data() + 1000, f.new_bytes.size() - 1000);
        update.commit();
    }
    REQUIRE(Fixture::read(f.image) == f.new_bytes);
    REQUIRE(Fixture::read(f.image.string() + ".previous") == f.old_bytes);
    std::string running_bytes(f.old_bytes.size(), '\0');
    REQUIRE(pread(running, running_bytes.data(), running_bytes.size(), 0) == static_cast<ssize_t>(running_bytes.size()));
    close(running);
    REQUIRE(running_bytes == f.old_bytes);
    REQUIRE(access(f.image.c_str(), X_OK) == 0);
    REQUIRE(access((f.image.string() + ".previous").c_str(), X_OK) == 0);
    REQUIRE(f.no_staging());
}

TEST_CASE("Interrupted, corrupted, oversized and incompatible updates preserve the installed image")
{
    Fixture f;
    auto bytes = f.new_bytes;
    auto release = release_for(bytes);
    {
        SECTION("Interrupted") {
            StagedUpdate update(f.image.string(), release);
            update.append(bytes.data(), 100);
            REQUIRE_THROWS(update.commit());
        }
        SECTION("Corrupted") {
            StagedUpdate update(f.image.string(), release);
            bytes[100] ^= 1;
            update.append(bytes.data(), bytes.size());
            REQUIRE_THROWS(update.commit());
        }
        SECTION("Oversized") {
            StagedUpdate update(f.image.string(), release);
            REQUIRE_THROWS(update.append(bytes.data(), bytes.size() + 1));
        }
        SECTION("Wrong architecture") {
            bytes[18] = static_cast<char>(183);
            StagedUpdate update(f.image.string(), release_for(bytes));
            update.append(bytes.data(), bytes.size());
            REQUIRE_THROWS(update.commit());
        }
        SECTION("HTML instead of AppImage") {
            bytes.replace(0, 4, "html");
            StagedUpdate update(f.image.string(), release_for(bytes));
            update.append(bytes.data(), bytes.size());
            REQUIRE_THROWS(update.commit());
        }
    }
    REQUIRE(Fixture::read(f.image) == f.old_bytes);
    REQUIRE_FALSE(fs::exists(f.image.string() + ".previous"));
    REQUIRE(f.no_staging());
}

TEST_CASE("Concurrent updates and installation changes cannot overwrite each other")
{
    Fixture f;
    {
        StagedUpdate update(f.image.string(), release_for(f.new_bytes));
        REQUIRE_THROWS(StagedUpdate(f.image.string(), release_for(f.new_bytes)));
        update.append(f.new_bytes.data(), f.new_bytes.size());
        fs::rename(f.image, f.dir / "moved.AppImage");
        Fixture::write(f.image, "externally replaced");
        REQUIRE_THROWS(update.commit());
    }
    REQUIRE(Fixture::read(f.image) == "externally replaced");
    REQUIRE(f.no_staging());
}

TEST_CASE("Symlinks and unsafe backup paths are rejected")
{
    Fixture f;
    SECTION("Read-only installation") {
        chmod(f.image.c_str(), 0555);
        REQUIRE_THROWS(StagedUpdate(f.image.string(), release_for(f.new_bytes)));
    }
    SECTION("Symlink installation") {
        const auto alias = f.dir / "alias.AppImage";
        fs::create_symlink(f.image, alias);
        REQUIRE_THROWS(StagedUpdate(alias.string(), release_for(f.new_bytes)));
    }
    SECTION("Symlink backup") {
        const auto other = f.dir / "other";
        Fixture::write(other, "keep me");
        fs::create_symlink(other, f.image.string() + ".previous");
        {
            StagedUpdate update(f.image.string(), release_for(f.new_bytes));
            update.append(f.new_bytes.data(), f.new_bytes.size());
            REQUIRE_THROWS(update.commit());
        }
        REQUIRE(Fixture::read(other) == "keep me");
    }
    REQUIRE(Fixture::read(f.image) == f.old_bytes);
    REQUIRE(f.no_staging());
}

TEST_CASE("A subsequent update replaces only the previous-version backup")
{
    Fixture f;
    for (const auto &bytes : {f.new_bytes, image_bytes('3')}) {
        StagedUpdate update(f.image.string(), release_for(bytes));
        update.append(bytes.data(), bytes.size());
        update.commit();
    }
    REQUIRE(Fixture::read(f.image) == image_bytes('3'));
    REQUIRE(Fixture::read(f.image.string() + ".previous") == f.new_bytes);
    REQUIRE(f.no_staging());
}

TEST_CASE("Download cancellation and forbidden protocols leave no partial installation")
{
    Fixture f;
    auto release = release_for(f.new_bytes);
    release.url = "file:///etc/passwd";
    std::atomic<bool> cancel{false};
    std::atomic<int> percent{0};
    REQUIRE_THROWS(download_and_install(f.image.string(), release, cancel, percent));
    cancel = true;
    REQUIRE_THROWS(download_and_install(f.image.string(), release, cancel, percent));
    REQUIRE(Fixture::read(f.image) == f.old_bytes);
    REQUIRE(f.no_staging());
}

TEST_CASE("Restart waits for exit, clears AppImage environment and preserves literal profile paths")
{
    Fixture f;
    const auto launcher = f.dir / "app ' $(touch INJECTED) é.AppImage";
    Fixture::write(launcher, R"(#!/bin/sh
if [ -n "${APPIMAGE+x}${APPDIR+x}${LD_LIBRARY_PATH+x}${LD_PRELOAD+x}" ]; then exit 3; fi
printf started > "$0.started"
printf '%s\n' "$@" > "$0.arguments"
)");
    chmod(launcher.c_str(), 0755);
    const pid_t old_app = fork();
    REQUIRE(old_app >= 0);
    if (old_app == 0) { pause(); _exit(0); }
    const pid_t helper = fork();
    if (helper < 0) { kill(old_app, SIGTERM); waitpid(old_app, nullptr, 0); FAIL("fork failed"); }
    if (helper == 0) {
        chdir(f.dir.c_str());
        setenv("APPIMAGE", "/old/image", 1); setenv("APPDIR", "/old/mount", 1);
        setenv("LD_LIBRARY_PATH", "/old/mount/lib", 1);
        const auto pid = std::to_string(old_app);
        const auto profile = (f.dir / "profile ' $(touch INJECTED) é").string();
        execl("/bin/sh", "/bin/sh", "-c", restart_script(), "restart-test", pid.c_str(), launcher.c_str(), profile.c_str(), nullptr);
        _exit(127);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const bool launched_early = fs::exists(launcher.string() + ".started");
    kill(old_app, SIGTERM);
    waitpid(old_app, nullptr, 0);
    int status = 0;
    bool exited = false;
    for (int i = 0; i < 40; ++i) {
        if (waitpid(helper, &status, WNOHANG) == helper) { exited = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!exited) { kill(helper, SIGTERM); waitpid(helper, nullptr, 0); }
    REQUIRE_FALSE(launched_early);
    REQUIRE(exited);
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == 0);
    REQUIRE(Fixture::read(launcher.string() + ".started") == "started");
    REQUIRE(Fixture::read(launcher.string() + ".arguments") == "--datadir\n" + (f.dir / "profile ' $(touch INJECTED) é").string() + "\n");
    REQUIRE_FALSE(fs::exists(f.dir / "INJECTED"));
}

TEST_CASE("macOS replacement helper rotates backups, detects changed installs and rolls back")
{
    using Slic3r::DesktopUpdater::mac_install_script;
    Fixture f;
    const auto app = f.dir / "Bambu 'Studio' $.app";
    const auto stage = f.dir / "stage with spaces";
    const auto backup = fs::path(app.string() + ".previous");
    fs::create_directories(app);
    fs::create_directories(stage / "new.app");
    Fixture::write(app / "version", "old");
    Fixture::write(stage / "new.app/version", "new");
    fs::create_directories(backup);
    Fixture::write(backup / "version", "older");
    struct stat original = {};
    REQUIRE(stat(app.c_str(), &original) == 0);
    const auto identity = std::to_string(original.st_dev) + ":" + std::to_string(original.st_ino) + ":" + std::to_string(original.st_mtime);
    Fixture::write(stage / "identity", identity);
    bool succeeds = true;
    bool restore = false;
    SECTION("Successful replacement") {}
    SECTION("Installation changed") { Fixture::write(stage / "identity", "other"); succeeds = false; }
    SECTION("Unsafe backup") { fs::remove_all(backup); fs::create_directory_symlink(app, backup); succeeds = false; }
    SECTION("Staged app missing") { fs::remove_all(stage / "new.app"); succeeds = false; }
    SECTION("Replacement fails after moving old app") { restore = true; succeeds = false; }
    auto script = std::string(mac_install_script());
    const auto replace = [&](const std::string &from, const std::string &to) {
        auto pos = script.find(from);
        REQUIRE(pos != std::string::npos);
        // Both optional-profile launch branches use the same native tool.
        while (pos != std::string::npos) {
            script.replace(pos, from.size(), to);
            pos = script.find(from, pos + to.size());
        }
    };
    // Run the real transaction on Linux; replace only macOS-specific tools.
    replace("/usr/bin/stat -f '%d:%i:%m'", "stat -c '%d:%i:%Y'");
    replace("/usr/bin/open", "/bin/true");
    replace("/usr/bin/osascript", "/bin/true");
    if (restore) replace("/bin/mv \"$stage/new.app\" \"$app\"", "/bin/false");
    const auto pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        execl("/bin/sh", "/bin/sh", "-c", script.c_str(), "update-test", "999999999", app.c_str(), stage.c_str(), nullptr);
        _exit(127);
    }
    int status = 0;
    REQUIRE(waitpid(pid, &status, 0) == pid);
    REQUIRE(WIFEXITED(status));
    REQUIRE((WEXITSTATUS(status) == 0) == succeeds);
    REQUIRE(Fixture::read(app / "version") == (succeeds ? "new" : "old"));
    REQUIRE_FALSE(fs::exists(app.string() + ".update-lock"));
    if (succeeds) { REQUIRE(Fixture::read(backup / "version") == "old"); REQUIRE_FALSE(fs::exists(stage)); }
    if (restore) REQUIRE(Fixture::read(backup / "version") == "older");
}
