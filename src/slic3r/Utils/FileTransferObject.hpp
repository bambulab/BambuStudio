#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Slic3r {

class FileTransferObject : public std::enable_shared_from_this<FileTransferObject>
{
public:
    using ResultCb = std::function<void(int ec, int resp_ec, std::string json, std::vector<std::byte> data)>;

    FileTransferObject();
    ~FileTransferObject();

    FileTransferObject(const FileTransferObject &)            = delete;
    FileTransferObject &operator=(const FileTransferObject &) = delete;

    void SetTunnelUrl(std::string url);

    // Ensure connection is established (async), callback parameters: (success?, error_code, error_msg)
    using ConnectCb = std::function<void(bool ok, int ec, std::string msg)>;
    void EnsureConnected(ConnectCb cb);

    // Download device memory file (using built-in FTDownloadFiles implementation with streaming and MD5 verification)
    // mem_path: e.g. "mem:/26"
    // target_path: file path on device, e.g. "Metadata/xxx/xxx.jpg"
    void DownloadMemFile(std::string mem_path, std::string target_path, ResultCb cb);

    void CancelAll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Slic3r
