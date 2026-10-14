#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#include <boost/nowide/convert.hpp>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Slic3r { namespace GUI {
inline bool mcp_gcode_paths_distinct(const std::string& source, const std::string& destination) {
#ifdef _WIN32
    if (std::filesystem::weakly_canonical(std::filesystem::u8path(source)) ==
        std::filesystem::weakly_canonical(std::filesystem::u8path(destination))) return false;
    std::error_code error;
    const bool same = std::filesystem::equivalent(std::filesystem::u8path(source), std::filesystem::u8path(destination), error);
#else
    if (std::filesystem::weakly_canonical(source) == std::filesystem::weakly_canonical(destination)) return false;
    std::error_code error;
    const bool same = std::filesystem::equivalent(source, destination, error);
#endif
    return !same;
}
#ifdef _WIN32
inline bool mcp_gcode_file_info(HANDLE file, BY_HANDLE_FILE_INFORMATION& info) {
    return GetFileInformationByHandle(file, &info) &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
}
inline std::string mcp_gcode_file_identity(const BY_HANDLE_FILE_INFORMATION& info) {
    return std::to_string(info.dwVolumeSerialNumber) + ":" + std::to_string(info.nFileIndexHigh) + ":" +
        std::to_string(info.nFileIndexLow) + ":" + std::to_string(info.nFileSizeHigh) + ":" +
        std::to_string(info.nFileSizeLow) + ":" + std::to_string(info.ftLastWriteTime.dwHighDateTime) + ":" +
        std::to_string(info.ftLastWriteTime.dwLowDateTime);
}
#endif
inline std::string mcp_gcode_identity(const std::string& path) {
#ifdef _WIN32
    const auto wide = boost::nowide::widen(path);
    HANDLE file = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    BY_HANDLE_FILE_INFORMATION info{};
    const bool valid = mcp_gcode_file_info(file, info) &&
        (info.nFileSizeHigh || info.nFileSizeLow) && info.nFileSizeHigh == 0 && info.nFileSizeLow <= 1024U * 1024U * 1024U;
    CloseHandle(file);
    return valid ? mcp_gcode_file_identity(info) : std::string();
#else
    struct stat info;
    if (::lstat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0) return {};
    std::string value = std::to_string(info.st_dev) + ":" + std::to_string(info.st_ino) + ":" +
        std::to_string(info.st_size) + ":" + std::to_string(info.st_mtime) + ":" + std::to_string(info.st_ctime);
#ifdef __APPLE__
    return value + ":" + std::to_string(info.st_mtimespec.tv_nsec) + ":" + std::to_string(info.st_ctimespec.tv_nsec);
#else
    return value + ":" + std::to_string(info.st_mtim.tv_nsec) + ":" + std::to_string(info.st_ctim.tv_nsec);
#endif
#endif
}
inline std::uint64_t mcp_copy_gcode(const std::string& source, const std::string& temporary,
                                  const std::string& destination, bool overwrite, const std::function<bool()>& current) {
#ifdef _WIN32
    if (!mcp_gcode_paths_distinct(source, destination)) throw std::runtime_error("Export destination aliases the native G-code source");
    const auto source_wide = boost::nowide::widen(source);
    const auto temporary_wide = boost::nowide::widen(temporary);
    const auto destination_wide = boost::nowide::widen(destination);
    HANDLE input = CreateFileW(source_wide.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (input == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open finalized G-code");
    struct InputGuard { HANDLE file; ~InputGuard() { CloseHandle(file); } } input_guard{input};
    BY_HANDLE_FILE_INFORMATION before{};
    if (!mcp_gcode_file_info(input, before) || before.nFileSizeHigh || before.nFileSizeLow == 0 ||
        before.nFileSizeLow > 1024U * 1024U * 1024U)
        throw std::runtime_error("Finalized G-code must be a nonempty regular file below 1 GiB");
    HANDLE output = CreateFileW(temporary_wide.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create exclusive export temporary file");
    struct OutputGuard {
        HANDLE file; const std::wstring& path;
        ~OutputGuard() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); DeleteFileW(path.c_str()); }
    } output_guard{output, temporary_wide};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::uint64_t bytes = 0;
    char buffer[65536];
    for (;;) {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("G-code copy exceeded ten-second limit");
        DWORD size = 0;
        if (!ReadFile(input, buffer, DWORD(sizeof(buffer)), &size, nullptr)) throw std::runtime_error("Cannot read finalized G-code");
        if (size == 0) break;
        DWORD offset = 0;
        while (offset < size) {
            DWORD written = 0;
            if (!WriteFile(output, buffer + offset, size - offset, &written, nullptr) || written == 0)
                throw std::runtime_error("Cannot write G-code export");
            offset += written;
        }
        bytes += size;
    }
    if (!FlushFileBuffers(output)) throw std::runtime_error("Cannot flush G-code export");
    if (!CloseHandle(output)) { output_guard.file = INVALID_HANDLE_VALUE; throw std::runtime_error("Cannot close G-code export"); }
    output_guard.file = INVALID_HANDLE_VALUE;
    if (!current()) throw std::runtime_error("Slice result changed during export");
    BY_HANDLE_FILE_INFORMATION after{}, named{};
    HANDLE source_name = CreateFileW(source_wide.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (source_name == INVALID_HANDLE_VALUE) throw std::runtime_error("Finalized G-code changed during export");
    const bool unchanged = mcp_gcode_file_info(input, after) && mcp_gcode_file_info(source_name, named) &&
        mcp_gcode_file_identity(before) == mcp_gcode_file_identity(after) &&
        mcp_gcode_file_identity(before) == mcp_gcode_file_identity(named) && bytes == before.nFileSizeLow;
    CloseHandle(source_name);
    if (!unchanged) throw std::runtime_error("Finalized G-code or slice result changed during export");
    if (overwrite) {
        if (!MoveFileExW(temporary_wide.c_str(), destination_wide.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot atomically replace G-code destination");
    } else if (!MoveFileExW(temporary_wide.c_str(), destination_wide.c_str(), MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = GetLastError();
        throw std::runtime_error(error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS ? "G-code destination already exists" :
                                 "Cannot atomically create G-code destination");
    }
    return bytes;
#else
    if (!mcp_gcode_paths_distinct(source, destination)) throw std::runtime_error("Export destination aliases the native G-code source");
    const int input = ::open(source.c_str(), O_RDONLY | O_NOFOLLOW);
    if (input < 0) throw std::runtime_error("Cannot open finalized G-code");
    struct InputGuard { int fd; ~InputGuard() { ::close(fd); } } input_guard{input};
    struct stat before;
    if (::fstat(input, &before) != 0 || !S_ISREG(before.st_mode) || before.st_size <= 0 || before.st_size > 1024LL * 1024 * 1024)
        throw std::runtime_error("Finalized G-code must be a nonempty regular file below 1 GiB");
    const int output = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (output < 0) throw std::runtime_error("Cannot create exclusive export temporary file");
    struct OutputGuard {
        int fd; const std::string& path;
        ~OutputGuard() { if (fd >= 0) ::close(fd); ::unlink(path.c_str()); }
    } output_guard{output, temporary};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::uint64_t bytes = 0;
    char buffer[65536];
    for (;;) {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("G-code copy exceeded ten-second limit");
        const ssize_t size = ::read(input, buffer, sizeof(buffer));
        if (size < 0) { if (errno == EINTR) continue; throw std::runtime_error("Cannot read finalized G-code"); }
        if (size == 0) break;
        ssize_t offset = 0;
        while (offset < size) {
            const ssize_t written = ::write(output, buffer + offset, size - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) throw std::runtime_error("Cannot write G-code export");
            offset += written;
        }
        bytes += size;
    }
    if (::fsync(output) != 0) throw std::runtime_error("Cannot flush G-code export");
    if (::close(output) != 0) { output_guard.fd = -1; throw std::runtime_error("Cannot close G-code export"); }
    output_guard.fd = -1;
    if (!current()) throw std::runtime_error("Slice result changed during export");
    struct stat after, named;
    if (::fstat(input, &after) != 0 || ::lstat(source.c_str(), &named) != 0 || !S_ISREG(named.st_mode) ||
        before.st_dev != named.st_dev || before.st_ino != named.st_ino || before.st_size != after.st_size ||
        before.st_mtime != after.st_mtime || before.st_ctime != after.st_ctime || bytes != static_cast<std::uint64_t>(before.st_size))
        throw std::runtime_error("Finalized G-code or slice result changed during export");
#ifdef __APPLE__
    if (before.st_mtimespec.tv_nsec != after.st_mtimespec.tv_nsec || before.st_ctimespec.tv_nsec != after.st_ctimespec.tv_nsec)
        throw std::runtime_error("Finalized G-code changed during export");
#else
    if (before.st_mtim.tv_nsec != after.st_mtim.tv_nsec || before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
        throw std::runtime_error("Finalized G-code changed during export");
#endif
    if (overwrite) {
        if (::rename(temporary.c_str(), destination.c_str()) != 0) throw std::runtime_error("Cannot atomically replace G-code destination");
    } else if (::link(temporary.c_str(), destination.c_str()) != 0) {
        throw std::runtime_error(errno == EEXIST ? "G-code destination already exists" : "Cannot atomically create G-code destination");
    }
    return bytes;
#endif
}
}}
