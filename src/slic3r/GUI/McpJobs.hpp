#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace Slic3r { namespace GUI {
struct McpWorkerLifetime {
    std::atomic<unsigned int> orphans{0};
    void stop() { std::lock_guard<std::mutex> lock(mutex); stopping = true; }
    void publish(const std::function<void()>& callback) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!stopping) callback();
    }
private:
    std::mutex mutex;
    bool stopping = false;
};
struct McpSliceJob {
    std::uint64_t run = 0;
    std::string id, kind = "slice", state = "running", fingerprint, input_guard, error_code, error_message, result_id, path;
    std::uint64_t bytes = 0;
    int plate_index = -1, progress = 0;
    bool cancellation_requested = false, completion_observed = false, reused = false, worker_pending = true;
};

class McpSliceJobs {
public:
    McpSliceJob& begin(const std::string& session, int plate, std::string fingerprint) {
        if (active()) throw std::runtime_error("A bridge operation is still active");
        while (m_jobs.size() >= 64) {
            auto it = std::find_if(m_jobs.begin(), m_jobs.end(), [](const McpSliceJob& j) { return !j.worker_pending; });
            if (it == m_jobs.end()) throw std::runtime_error("Native job history is full");
            m_jobs.erase(it);
        }
        McpSliceJob job;
        job.run = ++m_next_run;
        job.id = session + ":slice:" + std::to_string(job.run);
        job.plate_index = plate;
        job.fingerprint = std::move(fingerprint);
        m_jobs.push_back(std::move(job));
        return m_jobs.back();
    }
    McpSliceJob* find(const std::string& id) {
        for (auto& job : m_jobs) if (job.id == id) return &job;
        return nullptr;
    }
    McpSliceJob* find(std::uint64_t run) {
        for (auto& job : m_jobs) if (job.run == run) return &job;
        return nullptr;
    }
    McpSliceJob* active() {
        for (auto& job : m_jobs) if (job.worker_pending) return &job;
        return nullptr;
    }
    bool accepts_event(std::uint64_t run) {
        const auto* job = active();
        return !job || job->kind != "slice" || job->run == run;
    }
    void progress(std::uint64_t run, int percent) {
        if (auto* job = find(run))
            if (job->worker_pending && percent >= 0)
                job->progress = std::max(job->progress, std::min(percent, 99));
    }
    void finish(std::uint64_t run, const std::string& state, std::string code = {}, std::string message = {}, bool observed = true) {
        if (auto* job = find(run)) {
            if (!job->worker_pending) return;
            job->worker_pending = false;
            job->completion_observed = observed;
            job->state = state;
            job->error_code = std::move(code);
            job->error_message = std::move(message);
            if (state == "succeeded") job->progress = 100;
        }
    }
    void invalidate(std::uint64_t run) {
        if (auto* job = find(run)) {
            job->state = "stale";
            job->result_id.clear();
            job->error_code = "STALE_REFERENCE";
            job->error_message = "Native slice inputs or result changed";
        }
    }
private:
    std::uint64_t m_next_run = 0;
    std::deque<McpSliceJob> m_jobs;
};
}}
