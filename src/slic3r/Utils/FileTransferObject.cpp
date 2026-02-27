#include "FileTransferObject.hpp"

#include "FileTransferUtils.hpp"

#include <algorithm>
#include <mutex>
#include <utility>

#include "nlohmann/json.hpp"

namespace Slic3r {

struct FileTransferObject::Impl
{
    struct Pending
    {
        std::string mem_path;
        std::string target_path;
        ResultCb    cb;
    };

    enum class ConnState
    {
        Idle,
        Connecting,
        Connected,
        Failed
    };

    std::mutex                                        mutex;
    std::string                                       url;
    ConnState                                         state = ConnState::Idle;
    std::shared_ptr<FileTransferTunnel>               tunnel;
    std::vector<Pending>                              pending;
    std::vector<std::shared_ptr<FileTransferJob>>     active_jobs;
    uint64_t                                          request_seq = 0;
    uint64_t                                          active_request_id = 0;

    void reset_locked()
    {
        ++request_seq;
        active_request_id = request_seq;
        for (auto &job : active_jobs) {
            if (job) job->cancel();
        }
        active_jobs.clear();
        if (tunnel) tunnel->shutdown();
        tunnel.reset();
        state = ConnState::Idle;
        pending.clear();
    }

    void track_job_locked(const std::shared_ptr<FileTransferJob> &job)
    {
        if (!job) return;
        active_jobs.push_back(job);
    }

    void untrack_job_locked(const std::shared_ptr<FileTransferJob> &job)
    {
        if (!job) return;
        active_jobs.erase(std::remove(active_jobs.begin(), active_jobs.end(), job), active_jobs.end());
    }

    void fail_pending_locked(int ec, const std::string &msg)
    {
        for (auto &p : pending) {
            if (p.cb) p.cb(ec, ec, std::string("{\"error\":\"") + msg + "\"}", {});
        }
        pending.clear();
    }
};

FileTransferObject::FileTransferObject() : impl_(std::make_unique<Impl>()) {}

FileTransferObject::~FileTransferObject() = default;

void FileTransferObject::CancelAll()
{
    if (!impl_) return;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->reset_locked();
}

void FileTransferObject::SetTunnelUrl(std::string url)
{
    if (!impl_) return;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (url == impl_->url && impl_->tunnel) return;
    impl_->reset_locked();
    impl_->url = std::move(url);
}

void FileTransferObject::EnsureConnected(ConnectCb cb)
{
    if (!impl_) {
        if (cb) cb(false, FT_ESTATE, "impl is null");
        return;
    }

    FileTransferModule *mod = nullptr;
    try {
        mod = &module();
    } catch (const std::exception &e) {
        if (cb) cb(false, FT_ESTATE, std::string("module error: ") + e.what());
        return;
    }

    bool need_connect = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);

        if (impl_->url.empty()) {
            if (cb) cb(false, FT_EINVAL, "tunnel url not set");
            return;
        }

        // Already connected, return success immediately
        if (impl_->state == Impl::ConnState::Connected && impl_->tunnel) {
            if (cb) cb(true, 0, "");
            return;
        }

        // Connection in progress, wait for completion
        if (impl_->state == Impl::ConnState::Connecting) {
            impl_->pending.push_back({"", "", [cb](int ec, int, std::string msg, std::vector<std::byte>) {
                if (cb) cb(ec == 0, ec, msg);
            }});
            return;
        }

        // Need to establish new connection
        if (impl_->state == Impl::ConnState::Idle || impl_->state == Impl::ConnState::Failed) {
            try {
                impl_->tunnel = std::make_shared<FileTransferTunnel>(*mod, impl_->url);
                impl_->state  = Impl::ConnState::Connecting;
                need_connect = true;

                impl_->pending.push_back({"", "", [cb](int ec, int, std::string msg, std::vector<std::byte>) {
                    if (cb) cb(ec == 0, ec, msg);
                }});
            } catch (const std::exception &e) {
                impl_->state = Impl::ConnState::Failed;
                if (cb) cb(false, FT_EIO, e.what());
                return;
            }
        }
    }

    if (need_connect) {
        impl_->tunnel->on_connection([self = shared_from_this(), mod](bool ok, int ec, std::string msg) {
            if (!self || !self->impl_) return;

            std::vector<Impl::Pending> pending_copy;
            {
                std::lock_guard<std::mutex> lock(self->impl_->mutex);
                if (!ok) {
                    self->impl_->state = Impl::ConnState::Failed;
                    self->impl_->fail_pending_locked(ec, msg);
                    return;
                }
                self->impl_->state = Impl::ConnState::Connected;
                pending_copy = std::move(self->impl_->pending);
                self->impl_->pending.clear();
            }

            // Trigger all pending callbacks
            for (auto &p : pending_copy) {
                if (p.cb) p.cb(0, 0, "", {});
            }
        });

        try {
            impl_->tunnel->start_connect();
        } catch (const std::exception &e) {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->state = Impl::ConnState::Failed;
            impl_->fail_pending_locked(FT_EIO, e.what());
        }
    }
}

void FileTransferObject::DownloadMemFile(std::string mem_path, std::string target_path, ResultCb cb)
{
    // Use FTDownloadFiles implementation, which already includes streaming reception and MD5 verification
    if (!impl_) {
        if (cb) cb(FT_ESTATE, FT_ESTATE, "{\"error\":\"impl is null\"}", {});
        return;
    }

    auto self = shared_from_this();
    EnsureConnected([self, mem_path = std::move(mem_path), target_path = std::move(target_path), cb = std::move(cb)](bool ok, int ec, std::string msg) mutable {
        if (!ok) {
            if (cb) cb(ec, ec, std::string("{\"error\":\"connection failed: ") + msg + "\"}", {});
            return;
        }

        // Build JSON parameters for FTDownloadFiles
        nlohmann::json params;
        params["cmd_type"] = 4; // FTCmd::kFileDownload
        params["path"] = mem_path;
        params["is_mem_file"] = true;
        params["target_path"] = target_path;

        try {
            FileTransferModule *mod = nullptr;
            try {
                mod = &module();
            } catch (const std::exception &e) {
                {
                    std::lock_guard<std::mutex> lock(self->impl_->mutex);
                    self->impl_->reset_locked();
                }
                if (cb) cb(FT_ESTATE, FT_ESTATE, std::string("{\"error\":\"") + e.what() + "\"}", {});
                return;
            }

            std::shared_ptr<FileTransferTunnel> tunnel;
            uint64_t request_id = 0;
            {
                std::lock_guard<std::mutex> lock(self->impl_->mutex);

                if (self->impl_->state != Impl::ConnState::Connected || !self->impl_->tunnel) {
                    if (cb) cb(FT_ESTATE, FT_ESTATE, "{\"error\":\"tunnel not connected\"}", {});
                    return;
                }

                tunnel = self->impl_->tunnel;
                request_id = ++self->impl_->request_seq;
                self->impl_->active_request_id = request_id;
            }

            // Create FTDownloadFiles job (via JSON parameters)
            auto job = std::make_shared<FileTransferJob>(*mod, params.dump());
            {
                std::lock_guard<std::mutex> lock(self->impl_->mutex);
                self->impl_->track_job_locked(job);
            }

            job->on_result([self, job, request_id, cb = std::move(cb)](int ec2, int resp_ec, std::string json, std::vector<std::byte> bin) mutable {
                if (cb) cb(ec2, resp_ec, std::move(json), std::move(bin));

                // Cleanup resources after callback completes (one-shot design)
                {
                    std::lock_guard<std::mutex> lock(self->impl_->mutex);
                    self->impl_->untrack_job_locked(job);
                    if (request_id == self->impl_->active_request_id) {
                        self->impl_->reset_locked();
                    }
                }
            });

            {
                std::lock_guard<std::mutex> lock(self->impl_->mutex);
                if (tunnel) {
                    job->start_on(*tunnel);
                } else {
                    self->impl_->reset_locked();
                    if (cb) cb(FT_ESTATE, FT_ESTATE, "{\"error\":\"tunnel released\"}", {});
                }
            }
        } catch (const std::exception &e) {
            {
                std::lock_guard<std::mutex> lock(self->impl_->mutex);
                self->impl_->reset_locked();
            }
            if (cb) cb(FT_EIO, FT_EIO, std::string("{\"error\":\"") + e.what() + "\"}", {});
        }
    });
}

} // namespace Slic3r
