#include "DesktopUpdater.hpp"
#include <array>
#include <cerrno>
#include <vector>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <openssl/evp.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif
#ifdef __APPLE__
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace Slic3r::DesktopUpdater {
namespace fs = std::filesystem;
struct StagedPackage::Impl {
    fs::path directory, package;
    std::ofstream stream;
    ReleaseUpdater::Release release;
    EVP_MD_CTX *hash = nullptr;
    std::uint64_t received = 0;
    bool complete = false;
    ~Impl() {
        stream.close();
        if (hash) EVP_MD_CTX_free(hash);
        if (!complete && !directory.empty()) {
            std::error_code ignored;
            fs::remove_all(directory, ignored);
        }
    }
};

StagedPackage::StagedPackage(const std::string &parent, const ReleaseUpdater::Release &release,
                             const std::string &filename) : impl(new Impl)
{
    ReleaseUpdater::validate_release(release);
    if (filename != "update.exe" && filename != "update.dmg")
        throw std::runtime_error("Unsupported update package");
    impl->release = release;
    std::random_device random;
    const auto base = fs::u8path(parent);
    for (int attempt = 0; attempt < 10; ++attempt) {
        std::string name = ".bambustudio-update-";
        for (int i = 0; i < 32; ++i) name += "0123456789abcdef"[random() & 15];
        const auto directory = base / name;
#ifdef _WIN32
        if (!fs::create_directory(directory)) continue;
#else
        if (mkdir(directory.c_str(), 0700) != 0) {
            if (errno == EEXIST) continue;
            throw std::runtime_error("Cannot stage the update beside the app. Move the app to a writable folder first.");
        }
#endif
        impl->directory = directory;
        break;
    }
    if (impl->directory.empty()) throw std::runtime_error("Cannot create a private update directory");
    impl->package = impl->directory / filename;
    impl->stream.open(impl->package, std::ios::binary | std::ios::trunc);
    if (!impl->stream) throw std::runtime_error("Cannot create the update package");
    impl->hash = EVP_MD_CTX_new();
    if (!impl->hash || EVP_DigestInit_ex(impl->hash, EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("Cannot initialize SHA-256 verification");
}
StagedPackage::~StagedPackage() = default;
void StagedPackage::append(const char *data, std::size_t size)
{
    if (impl->complete || size > impl->release.size - impl->received)
        throw std::runtime_error("The download exceeds the release size");
    impl->stream.write(data, static_cast<std::streamsize>(size));
    if (!impl->stream || EVP_DigestUpdate(impl->hash, data, size) != 1)
        throw std::runtime_error("Cannot save or verify the update");
    impl->received += size;
}
std::string StagedPackage::finish()
{
    if (impl->complete || impl->received != impl->release.size)
        throw std::runtime_error("The update download is incomplete");
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned length = 0;
    if (EVP_DigestFinal_ex(impl->hash, digest, &length) != 1 || length != 32)
        throw std::runtime_error("Cannot finish SHA-256 verification");
    std::string hex;
    for (unsigned i = 0; i < length; ++i) {
        hex += "0123456789abcdef"[digest[i] >> 4];
        hex += "0123456789abcdef"[digest[i] & 15];
    }
    if (hex != impl->release.sha256) throw std::runtime_error("The update failed SHA-256 verification");
    impl->stream.close();
    if (impl->stream.fail()) throw std::runtime_error("Cannot finish writing the update package");
    impl->complete = true;
    return impl->package.u8string();
}
std::string download_package(const std::string &parent, const ReleaseUpdater::Release &release,
                             const std::string &filename, const std::atomic<bool> &cancel, std::atomic<int> &percent)
{
    StagedPackage staged(parent, release, filename);
    ReleaseUpdater::download(release, cancel, percent, [&](const char *data, std::size_t size) { staged.append(data, size); });
    const auto path = staged.finish();
    percent = 100;
    return path;
}

const char *windows_install_script()
{
    return R"PS(param([int]$ParentId, [string]$Package, [string]$Application, [string]$DataDirectory)
$ErrorActionPreference = 'Stop'
try {
    # A PID may already have exited before the helper starts.
    $parent = Get-Process -Id $ParentId -ErrorAction SilentlyContinue
    if ($parent) { $parent | Wait-Process -Timeout 120 }
    # /D is NSIS's final, deliberately unquoted argument, including spaces.
    $installDir = [System.IO.Path]::GetDirectoryName($Application)
    $installer = Start-Process -FilePath $Package -ArgumentList ('/UPDATE /D=' + $installDir) -Wait -PassThru
    if ($installer.ExitCode -ne 0) { throw "Installer cancelled or failed ($($installer.ExitCode))." }
    if ($DataDirectory) {
        # Windows paths cannot contain quotes. Double trailing backslashes so
        # the native argument parser preserves them before the closing quote.
        $quotedDirectory = '"' + ($DataDirectory -replace '(\\+)$', '$1$1') + '"'
        Start-Process -FilePath $Application -ArgumentList ('--datadir ' + $quotedDirectory)
    } else {
        Start-Process -FilePath $Application
    }
} catch {
    Add-Type -AssemblyName PresentationFramework
    [System.Windows.MessageBox]::Show($_.Exception.Message, 'Bambu Studio update failed') | Out-Null
} finally {
    Remove-Item -LiteralPath ([System.IO.Path]::GetDirectoryName($Package)) -Recurse -Force -ErrorAction SilentlyContinue
}
)PS";
}

const char *mac_install_script()
{
    // All paths are positional arguments, never interpolated into shell source.
    return R"SH(i=0
while kill -0 "$1" 2>/dev/null; do
    i=$((i+1)); [ "$i" -lt 120 ] || exit 1
    sleep 1
done
app=$2
stage=$3
backup="$app.previous"
fail() {
    /usr/bin/osascript -e 'display alert "Bambu Studio update failed" message "The existing app or its .previous backup is still available. Please reopen it and try again."' >/dev/null 2>&1
    exit 1
}
lock="$app.update-lock"
/bin/mkdir "$lock" || fail
trap '/bin/rmdir "$lock"' EXIT
identity=$(/usr/bin/stat -f '%d:%i:%m' "$app") || fail
[ "$identity" = "$(/bin/cat "$stage/identity")" ] || fail
[ -d "$stage/new.app" ] && [ ! -L "$stage/new.app" ] && [ ! -L "$app" ] && [ -d "$app" ] || fail
[ ! -L "$backup" ] || fail
# Rotate a valid old backup only after the replacement has been verified.
if [ -e "$backup" ]; then
    [ -d "$backup" ] || fail
    /bin/mv "$backup" "$stage/older.app" || fail
fi
if ! /bin/mv "$app" "$backup"; then
    [ ! -d "$stage/older.app" ] || /bin/mv "$stage/older.app" "$backup"
    fail
fi
if ! /bin/mv "$stage/new.app" "$app"; then
    /bin/mv "$backup" "$app"
    [ ! -d "$stage/older.app" ] || /bin/mv "$stage/older.app" "$backup"
    fail
fi
# Preserve the old version if launch fails, too.
if [ -n "${4:-}" ]; then
    /usr/bin/open "$app" --args --datadir "$4" || fail
else
    /usr/bin/open "$app" || fail
fi
/bin/rm -rf "$stage"
)SH";
}

#ifdef __APPLE__
namespace {
void run(std::initializer_list<std::string> arguments)
{
    std::vector<std::string> values(arguments);
    std::vector<char *> argv;
    for (auto &value : values) argv.push_back(value.data());
    argv.push_back(nullptr);
    pid_t pid;
    if (posix_spawn(&pid, argv[0], nullptr, nullptr, argv.data(), environ) != 0)
        throw std::runtime_error("Cannot start the macOS update tool");
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) throw std::runtime_error("Cannot wait for the macOS update tool");
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error("macOS could not prepare the update; the installed app is unchanged");
}
}
void prepare_mac_bundle(const std::string &package, const std::string &application)
{
    const auto app = fs::u8path(application);
    if (fs::is_symlink(app) || !fs::is_directory(app) || access(app.c_str(), W_OK) != 0 ||
        access(app.parent_path().c_str(), W_OK) != 0)
        throw std::runtime_error("Move BambuStudio.app to a writable folder before updating");
    const auto backup = fs::u8path(application + ".previous");
    if (fs::is_symlink(backup) || (fs::exists(backup) && !fs::is_directory(backup)))
        throw std::runtime_error("The previous-version backup path is not a regular app directory");
    struct stat original = {};
    if (lstat(app.c_str(), &original) != 0 || original.st_uid != geteuid())
        throw std::runtime_error("The installed app must be owned by you");
    const auto directory = fs::u8path(package).parent_path();
    std::ofstream identity(directory / "identity");
    identity << original.st_dev << ':' << original.st_ino << ':' << original.st_mtime;
    identity.close();
    if (!identity) throw std::runtime_error("Cannot save the installation identity");
    const auto mount = directory / "mount";
    fs::create_directory(mount);
    run({"/usr/bin/hdiutil", "attach", "-readonly", "-nobrowse", "-noautoopen", "-mountpoint", mount.u8string(), package});
    try {
        const auto bundle = mount / "BambuStudio.app";
        if (fs::is_symlink(bundle) || !fs::is_regular_file(bundle / "Contents/MacOS/BambuStudio"))
            throw std::runtime_error("The disk image does not contain BambuStudio.app");
        run({"/usr/bin/codesign", "--verify", "--deep", "--strict", bundle.u8string()});
#ifdef __aarch64__
        const std::string architecture = "arm64";
#else
        const std::string architecture = "x86_64";
#endif
        run({"/usr/bin/lipo", (bundle / "Contents/MacOS/BambuStudio").u8string(), "-verify_arch", architecture});
        run({"/usr/bin/ditto", bundle.u8string(), (directory / "new.app").u8string()});
        run({"/usr/bin/codesign", "--verify", "--deep", "--strict", (directory / "new.app").u8string()});
    } catch (...) {
        try { run({"/usr/bin/hdiutil", "detach", "-force", mount.u8string()}); } catch (...) {}
        throw;
    }
    try { run({"/usr/bin/hdiutil", "detach", mount.u8string()}); }
    catch (...) { run({"/usr/bin/hdiutil", "detach", "-force", mount.u8string()}); }
}
#endif
} // namespace Slic3r::DesktopUpdater
