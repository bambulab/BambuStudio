#include "slic3r/GUI/Jobs/Job.hpp"

#include <wx/app.h>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// Console initialization supplies wx's event queues without constructing a
// wxApp, native windows, the Studio application, or any printer connection.
class DispatchTestApp : public wxAppConsole {};
wxIMPLEMENT_APP_NO_MAIN(DispatchTestApp);

namespace {

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct ErrorRecord {
    std::string message;
    int code;
    std::string description;
    std::string extra;
    std::thread::id thread;
};

struct Observations {
    std::vector<ErrorRecord> errors;
    std::vector<std::string> order;
    std::atomic<int> destructions{0};
};

class RecordingProgress final : public Slic3r::ProgressIndicator {
    Observations& m_observed;
public:
    explicit RecordingProgress(Observations& observed) : m_observed(observed) {}
    ~RecordingProgress() override { ++m_observed.destructions; }
    void clear_percent() override {}
    void show_error_info(wxString message, int code, wxString description, wxString extra) override
    {
        m_observed.errors.push_back({message.ToStdString(wxConvUTF8), code,
            description.ToStdString(wxConvUTF8), extra.ToStdString(wxConvUTF8),
            std::this_thread::get_id()});
        m_observed.order.push_back("error:" + std::to_string(code));
    }
    void set_range(int) override {}
    void set_cancel_callback(CancelFn = CancelFn()) override {}
    void set_progress(int) override {}
    void set_status_text(const char*) override {}
    int get_range() const override { return 100; }
};

class DispatchJob final : public Slic3r::GUI::Job {
public:
    explicit DispatchJob(std::shared_ptr<Slic3r::ProgressIndicator> progress)
        : Job(std::move(progress)) {}
    using Job::show_error_info;
private:
    void process() override {}
};

void drain_events()
{
    wxTheApp->ProcessPendingEvents();
}

void check_deferred_main_thread_unicode_and_fifo()
{
    Observations observed;
    auto progress = std::make_shared<RecordingProgress>(observed);
    DispatchJob job(progress);
    const auto main_thread = std::this_thread::get_id();
    // Preserve an embedded NUL as well as non-ASCII text in all three fields.
    constexpr char message_bytes[] = u8"Send failed — 測試 🚀\0trailing";
    const std::string expected_message(message_bytes, sizeof(message_bytes) - 1);
    const std::string expected_description = u8"Détails: connexion interrompue";
    const std::string expected_extra = u8"詳細 / retry ✓";

    std::thread worker([&] {
        // Construct all wxStrings here. No wxString is shared with the main
        // thread; the bundled wxWidgets uses non-atomic string refcounts.
        job.CallAfter([&] { observed.order.push_back("before"); });
        job.show_error_info(wxString::FromUTF8(expected_message.data(), expected_message.size()), -27,
            wxString::FromUTF8(expected_description.data(), expected_description.size()),
            wxString::FromUTF8(expected_extra.data(), expected_extra.size()));
        job.CallAfter([&] { observed.order.push_back("between"); });
        job.show_error_info(wxString::FromUTF8(u8"Second failure 失敗"), 42, wxString(), wxString());
        job.CallAfter([&] { observed.order.push_back("after"); });
    });
    worker.join();
    require(observed.errors.empty(), "error indicator called before main-thread dispatch");
    require(observed.order.empty(), "event callbacks ran before event pump");
    drain_events();
    require(observed.errors.size() == 2, "queued errors were lost or duplicated");
    for (const auto& error : observed.errors)
        require(error.thread == main_thread, "error indicator ran on worker thread");
    const auto& error = observed.errors.front();
    require(error.message == expected_message, "message Unicode/embedded NUL changed");
    require(error.code == -27, "error code changed");
    require(error.description == expected_description, "description Unicode changed");
    require(error.extra == expected_extra, "extra Unicode changed");
    require(observed.errors.back().message == u8"Second failure 失敗", "second message changed");
    require(observed.errors.back().description.empty() && observed.errors.back().extra.empty(),
        "empty error fields changed");
    require(observed.order == std::vector<std::string>{"before", "error:-27", "between", "error:42", "after"},
        "error delivery reordered relative to Job callbacks");
}

void check_cancellation_suppresses_pending_error()
{
    Observations observed;
    DispatchJob job(std::make_shared<RecordingProgress>(observed));
    std::thread worker([&] {
        job.show_error_info(wxString::FromUTF8("Pending failure"), 7, wxString(), wxString());
    });
    worker.join();
    job.cancel();
    drain_events();
    require(observed.errors.empty(), "canceled job displayed its pending error");
}

void check_destruction_drops_callback_and_releases_progress()
{
    Observations observed;
    auto progress = std::make_shared<RecordingProgress>(observed);
    std::weak_ptr<RecordingProgress> lifetime = progress;
    auto job = std::make_unique<DispatchJob>(progress);
    std::thread worker([&] {
        job->show_error_info(wxString::FromUTF8("Discarded failure"), 8, wxString(), wxString());
    });
    worker.join();
    require(observed.errors.empty(), "destruction case dispatched before event pump");
    progress.reset();
    require(!lifetime.expired(), "progress was not retained while Job owned pending callback");
    job.reset();
    require(lifetime.expired(), "destroyed Job left callback retaining progress");
    require(observed.destructions == 1, "progress was not destroyed exactly once");
    drain_events();
    require(observed.errors.empty(), "destroyed Job's callback was delivered");
}

} // namespace

int main(int argc, char** argv)
{
    if (!wxEntryStart(argc, argv)) {
        std::cerr << "wx console initialization failed\n";
        return 1;
    }
    int result = 0;
    try {
        require(wxTheApp->CallOnInit(), "wx console OnInit failed");
        check_deferred_main_thread_unicode_and_fifo();
        check_cancellation_suppresses_pending_error();
        check_destruction_drops_callback_and_releases_progress();
        std::cout << "PASS: deferred worker error, UI thread, Unicode, FIFO, cancellation, destruction/lifetime\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    }
    wxTheApp->OnExit();
    wxEntryCleanup();
    return result;
}
