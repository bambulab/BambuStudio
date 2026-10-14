#include "McpCredentials.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>
#include <openssl/rand.h>

#ifdef _WIN32
#include <windows.h>
#include <aclapi.h>
#include <boost/nowide/convert.hpp>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Slic3r::GUI {
namespace {

constexpr size_t token_bytes = 32;
constexpr size_t token_chars = token_bytes * 2;

std::string token_path(const std::string &profile_dir)
{
    return (boost::filesystem::path(profile_dir) / "mcp.token").string();
}

bool new_token(std::string &token)
{
    std::array<unsigned char, token_bytes> bytes{};
    if (RAND_bytes(bytes.data(), int(bytes.size())) != 1)
        return false;
    static constexpr char hex[] = "0123456789abcdef";
    token.resize(token_chars);
    for (size_t i = 0; i < bytes.size(); ++i) {
        token[i * 2]     = hex[bytes[i] >> 4];
        token[i * 2 + 1] = hex[bytes[i] & 15];
    }
    return true;
}

bool valid_token(const std::string &token)
{
    if (token.size() != token_chars)
        return false;
    for (char c : token)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}

#ifdef _WIN32

struct WindowsAcl {
    HANDLE user_token = nullptr;
    PACL acl = nullptr;
    std::vector<BYTE> user_info;
    SECURITY_DESCRIPTOR descriptor{};
    SECURITY_ATTRIBUTES attributes{};

    ~WindowsAcl()
    {
        if (acl) LocalFree(acl);
        if (user_token) CloseHandle(user_token);
    }

    bool init()
    {
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &user_token)) return false;
        DWORD size = 0;
        GetTokenInformation(user_token, TokenUser, nullptr, 0, &size);
        if (!size) return false;
        user_info.resize(size);
        if (!GetTokenInformation(user_token, TokenUser, user_info.data(), size, &size)) return false;

        EXPLICIT_ACCESSW access{};
        access.grfAccessPermissions = FILE_ALL_ACCESS;
        access.grfAccessMode = SET_ACCESS;
        access.grfInheritance = NO_INHERITANCE;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.TrusteeType = TRUSTEE_IS_USER;
        access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(reinterpret_cast<TOKEN_USER *>(user_info.data())->User.Sid);
        if (SetEntriesInAclW(1, &access, nullptr, &acl) != ERROR_SUCCESS) return false;
        if (!InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION)) return false;
        if (!SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE)) return false;
        if (!SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED)) return false;
        attributes.nLength = sizeof(attributes);
        attributes.lpSecurityDescriptor = &descriptor;
        return true;
    }
};

bool read_token(const std::string &path, std::string &token, std::string &error)
{
    WindowsAcl security;
    if (!security.init()) { error = "Cannot secure the MCP credential file"; return false; }
    const std::wstring wide = boost::nowide::widen(path);
    HANDLE file = CreateFileW(wide.c_str(), GENERIC_READ | READ_CONTROL | WRITE_DAC, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = "Cannot open the MCP credential file"; return false; }
    BY_HANDLE_FILE_INFORMATION info{};
    const bool regular = GetFileInformationByHandle(file, &info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                         !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    const bool secured = regular && SetSecurityInfo(file, SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, security.acl, nullptr) == ERROR_SUCCESS;
    char buffer[token_chars + 2]{};
    DWORD size = 0;
    const bool readable = secured && ReadFile(file, buffer, DWORD(sizeof(buffer)), &size, nullptr);
    CloseHandle(file);
    if (!readable || (size != token_chars && !(size == token_chars + 1 && buffer[token_chars] == '\n'))) {
        error = "The MCP credential file is invalid or cannot be secured";
        return false;
    }
    token.assign(buffer, token_chars);
    if (!valid_token(token)) { error = "The MCP credential file is invalid"; return false; }
    return true;
}

bool write_new_token(const std::string &path, const std::string &token, std::string &error)
{
    WindowsAcl security;
    if (!security.init()) { error = "Cannot secure the MCP credential file"; return false; }
    const std::wstring wide = boost::nowide::widen(path);
    HANDLE file = CreateFileW(wide.c_str(), GENERIC_WRITE, 0, &security.attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = "Cannot create the MCP credential file"; return false; }
    const std::string content = token + "\n";
    DWORD size = 0;
    const bool written = WriteFile(file, content.data(), DWORD(content.size()), &size, nullptr) && size == content.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!written) {
        DeleteFileW(wide.c_str());
        error = "Cannot write the MCP credential file";
    }
    return written;
}

bool replace_token(const std::string &temp, const std::string &path, std::string &error)
{
    const std::wstring temp_wide = boost::nowide::widen(temp);
    const std::wstring path_wide = boost::nowide::widen(path);
    if (MoveFileExW(temp_wide.c_str(), path_wide.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    DeleteFileW(temp_wide.c_str());
    error = "Cannot replace the MCP credential file";
    return false;
}

#else

bool read_token(const std::string &path, std::string &token, std::string &error)
{
    int fd = open(path.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) { error = "Cannot open the MCP credential file"; return false; }
    struct stat info{};
    const bool secured = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_uid == geteuid() &&
                         fchmod(fd, S_IRUSR | S_IWUSR) == 0;
    char buffer[token_chars + 2]{};
    const ssize_t size = secured ? read(fd, buffer, sizeof(buffer)) : -1;
    close(fd);
    if (size != token_chars && !(size == token_chars + 1 && buffer[token_chars] == '\n')) {
        error = "The MCP credential file is invalid or cannot be secured";
        return false;
    }
    token.assign(buffer, token_chars);
    if (!valid_token(token)) { error = "The MCP credential file is invalid"; return false; }
    return true;
}

bool write_new_token(const std::string &path, const std::string &token, std::string &error)
{
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    if (fd < 0) { error = "Cannot create the MCP credential file"; return false; }
    const std::string content = token + "\n";
    const bool written = write(fd, content.data(), content.size()) == ssize_t(content.size()) && fsync(fd) == 0;
    close(fd);
    if (!written) {
        unlink(path.c_str());
        error = "Cannot write the MCP credential file";
    }
    return written;
}

bool replace_token(const std::string &temp, const std::string &path, std::string &error)
{
    if (rename(temp.c_str(), path.c_str()) == 0) return true;
    unlink(temp.c_str());
    error = "Cannot replace the MCP credential file";
    return false;
}

#endif

}

bool load_or_create_mcp_token(const std::string &profile_dir, std::string &token, std::string &error)
{
    error.clear();
    if (profile_dir.empty()) { error = "The MCP profile directory is unavailable"; return false; }
    const std::string path = token_path(profile_dir);
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(boost::nowide::widen(path).c_str());
    const bool exists = attributes != INVALID_FILE_ATTRIBUTES;
#else
    struct stat info{};
    const bool exists = lstat(path.c_str(), &info) == 0;
#endif
    if (exists) return read_token(path, token, error);
    if (!new_token(token)) { error = "Cannot generate an MCP credential"; return false; }
    if (write_new_token(path, token, error)) return true;
    if (!read_token(path, token, error)) return false;
    error.clear();
    return true;
}

bool regenerate_mcp_token(const std::string &profile_dir, std::string &token, std::string &error)
{
    error.clear();
    if (profile_dir.empty()) { error = "The MCP profile directory is unavailable"; return false; }
    std::string suffix;
    if (!new_token(token) || !new_token(suffix)) { error = "Cannot generate an MCP credential"; return false; }
    const std::string path = token_path(profile_dir);
    const std::string temp = path + "." + suffix.substr(0, 16) + ".tmp";
    if (!write_new_token(temp, token, error)) return false;
    return replace_token(temp, path, error);
}

}
