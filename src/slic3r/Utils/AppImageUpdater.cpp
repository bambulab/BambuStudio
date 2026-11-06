#include "AppImageUpdater.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

#include <openssl/evp.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Slic3r::AppImageUpdater {
namespace {

[[noreturn]] void system_error(const char *operation)
{
    throw std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
}

bool same_file(const struct stat &a, const struct stat &b)
{
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_size == b.st_size &&
           a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec;
}

void validate_image(int fd)
{
    unsigned char header[64] = {};
    if (pread(fd, header, sizeof(header), 0) != sizeof(header) ||
        std::memcmp(header, "\177ELF", 4) != 0 || header[4] != 2 || header[5] != 1 ||
        header[8] != 'A' || header[9] != 'I' || header[10] != 2 ||
        header[18] != 62 || header[19] != 0)
        throw std::runtime_error("The update is not an x86-64 type-2 AppImage");
}
} // namespace

const char *restart_script()
{
    return R"(i=0; while kill -0 "$1" 2>/dev/null; do
    i=$((i+1)); [ "$i" -lt 120 ] || exit 1; sleep 1
    done
    unset APPIMAGE APPDIR ARGV0 OWD LD_LIBRARY_PATH LD_PRELOAD
    if [ -n "${3:-}" ]; then exec "$2" --datadir "$3"; fi
    exec "$2")";
}

std::string current_appimage()
{
    const char *value = std::getenv("APPIMAGE");
    if (!value || !*value || std::getenv("FLATPAK_ID"))
        return {};
    char *resolved = realpath(value, nullptr);
    if (!resolved)
        return {};
    std::string image(resolved);
    std::free(resolved);
    return image;
}

struct StagedUpdate::Impl {
    std::string image, directory, staged, backup;
    Release release;
    int original_fd = -1, fd = -1;
    struct stat original = {};
    std::uint64_t received = 0;
    EVP_MD_CTX *hash = nullptr;
    bool committed = false;

    ~Impl()
    {
        if (fd >= 0) close(fd);
        if (original_fd >= 0) close(original_fd);
        if (hash) EVP_MD_CTX_free(hash);
        if (!staged.empty()) unlink(staged.c_str());
        if (!backup.empty()) unlink(backup.c_str());
        if (!directory.empty()) rmdir(directory.c_str());
    }
};

StagedUpdate::StagedUpdate(const std::string &image, const Release &release) : impl(new Impl)
{
    validate_release(release);
    impl->image = image;
    impl->release = release;
    impl->original_fd = open(image.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (impl->original_fd < 0) system_error("Open installed AppImage");
    if (fstat(impl->original_fd, &impl->original) != 0) system_error("Inspect installed AppImage");
    if (!S_ISREG(impl->original.st_mode) || impl->original.st_uid != geteuid() ||
        (impl->original.st_mode & (S_ISUID | S_ISGID)) || !(impl->original.st_mode & S_IWUSR) ||
        access(image.c_str(), W_OK) != 0)
        throw std::runtime_error("The installed AppImage must be a writable file owned by you");
    validate_image(impl->original_fd);
    if (flock(impl->original_fd, LOCK_EX | LOCK_NB) != 0)
        throw std::runtime_error("Another update is already in progress");
    std::string pattern = image + ".update-XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back('\0');
    if (!mkdtemp(name.data())) system_error("Create update directory beside AppImage");
    impl->directory = name.data();
    impl->staged = impl->directory + "/new.AppImage";
    impl->backup = impl->directory + "/previous.AppImage";
    impl->fd = open(impl->staged.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (impl->fd < 0) system_error("Create update file");
    impl->hash = EVP_MD_CTX_new();
    if (!impl->hash || EVP_DigestInit_ex(impl->hash, EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("Could not initialize SHA-256 verification");
}

StagedUpdate::~StagedUpdate() = default;

void StagedUpdate::append(const char *data, std::size_t size)
{
    if (impl->committed || size > impl->release.size - impl->received)
        throw std::runtime_error("The download exceeds the release size");
    const auto total = size;
    const char *cursor = data;
    while (size) {
        const auto written = write(impl->fd, cursor, size);
        if (written < 0) {
            if (errno == EINTR) continue;
            system_error("Write update file");
        }
        if (written == 0) throw std::runtime_error("Could not write update file");
        cursor += written;
        size -= written;
    }
    if (EVP_DigestUpdate(impl->hash, data, total) != 1)
        throw std::runtime_error("Could not verify update data");
    impl->received += total;
}

void StagedUpdate::commit()
{
    if (impl->committed || impl->received != impl->release.size)
        throw std::runtime_error("The update download is incomplete");
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned length = 0;
    if (EVP_DigestFinal_ex(impl->hash, digest, &length) != 1 || length != 32)
        throw std::runtime_error("Could not finish SHA-256 verification");
    std::string hex;
    for (unsigned i = 0; i < length; ++i) {
        hex += "0123456789abcdef"[digest[i] >> 4];
        hex += "0123456789abcdef"[digest[i] & 15];
    }
    if (hex != impl->release.sha256)
        throw std::runtime_error("The update failed SHA-256 verification");
    validate_image(impl->fd);
    if (fchmod(impl->fd, (impl->original.st_mode & 0777) | S_IXUSR) != 0 || fsync(impl->fd) != 0)
        system_error("Save verified update");
    struct stat current = {};
    if (lstat(impl->image.c_str(), &current) != 0 || !same_file(impl->original, current))
        throw std::runtime_error("The installed AppImage changed during the download; retry the update");
    const std::string previous = impl->image + ".previous";
    struct stat backup_stat = {};
    if (lstat(previous.c_str(), &backup_stat) == 0) {
        if (!S_ISREG(backup_stat.st_mode) || backup_stat.st_uid != geteuid())
            throw std::runtime_error("The previous-version backup path is not a file owned by you");
    } else if (errno != ENOENT) system_error("Inspect previous-version backup");
    if (link(impl->image.c_str(), impl->backup.c_str()) != 0 ||
        rename(impl->backup.c_str(), previous.c_str()) != 0)
        system_error("Keep previous AppImage");
    if (rename(impl->staged.c_str(), impl->image.c_str()) != 0)
        system_error("Install verified AppImage");
    impl->committed = true;
    // A successful rename is the commit point. Do not report a failed install
    // after it: the new image is already in place even if directory fsync fails.
    const auto slash = impl->image.find_last_of('/');
    const auto parent = slash == std::string::npos ? "." : impl->image.substr(0, slash);
    const int parent_fd = open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (parent_fd >= 0) { fsync(parent_fd); close(parent_fd); }
}

void download_and_install(const std::string &image, const Release &release,
                          const std::atomic<bool> &cancel, std::atomic<int> &percent)
{
    StagedUpdate staged(image, release);
    download(release, cancel, percent, [&](const char *data, std::size_t size) { staged.append(data, size); });
    staged.commit();
    percent = 100;
}

} // namespace Slic3r::AppImageUpdater
