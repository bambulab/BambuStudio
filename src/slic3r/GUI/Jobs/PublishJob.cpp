#include "PublishJob.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "bambu_networking.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"

namespace Slic3r {
namespace GUI {


static wxString failed_to_publish_str = _L("Failed to publish your project. Please try agian!");

PublishJob::PublishJob(std::shared_ptr<ProgressIndicator> pri, Plater* plater)
: PlaterJob{ std::move(pri), plater }
{
    m_publish_job_completed_id = plater->get_publish_finished_event();
}

void PublishJob::prepare()
{
    ;
}

void PublishJob::on_exception(const std::exception_ptr &eptr)
{
    try {
        if (eptr)
            std::rethrow_exception(eptr);
    } catch (std::exception &e) {
        PlaterJob::on_exception(eptr);
    }
}

void PublishJob::on_success(std::function<void()> success)
{
    m_success_fun = success;
}

wxString PublishJob::get_http_error_msg(unsigned int status, std::string body)
{
    int code = 0;
    std::string error;
    std::string message;
    wxString result;
    if (status >= 400 && status < 500)
        try {
        json j = json::parse(body);
        if (j.contains("code")) {
            if (!j["code"].is_null())
                code = j["code"].get<int>();
        }
        if (j.contains("error")) {
            if (!j["error"].is_null())
                error = j["error"].get<std::string>();
        }
        if (j.contains("message")) {
            if (!j["message"].is_null())
                message = j["message"].get<std::string>();
        }
        switch (status) {
            ;
        }
    }
    catch (...) {
        ;
    }
    else if (status == 503) {
        return _L("Service Unavailable");
    }
    else {
        wxString unkown_text = _L("Unkown Error.");
        unkown_text += wxString::Format("status=%u, body=%s", status, body);
        return unkown_text;
    }

    BOOST_LOG_TRIVIAL(error) << "http_error: status=" << status << ", code=" << code << ", error=" << error;

    result = wxString::Format("code=%u, error=%s", code, from_u8(error));
    return result;
}

void PublishJob::process()
{
    /* display info */
    wxString msg;
    int curr_percent = 75;
    NetworkAgent* m_agent = wxGetApp().getAgent();

    if (!m_agent)
        return;
    
    int result = -1;
    unsigned int http_code;
    std::string http_body;

    wxString error_text;
    wxString msg_text;

    auto update_fn = [this, &msg, &curr_percent, &error_text](int stage, int code, std::string info) {
                        if (stage == BBL::PublishingStage::PublishingCreate) {
                            msg = _L("Publishing");
                            curr_percent = 75;
                        }
                        else if (stage == BBL::PublishingStage::PublishingUpload) {
                            msg = _L("Publishing");
                            curr_percent = 80;
                        }
                        else if (stage == BBL::PublishingStage::PublishingWaiting) {
                            msg = _L("Publishing");
                            curr_percent = 85;
                        }
                        if (code != 0) {
                            error_text = this->get_http_error_msg(code, info);
                            msg += wxString::Format("[%s]", error_text);
                        }
                        this->update_status(curr_percent, msg);
                    };

    auto cancel_fn = [this]() {
            return was_canceled();
        };

    std::string url;
    result = m_agent->start_publish(publish_params, update_fn, cancel_fn, &url);

    if (was_canceled()) {
        update_status(curr_percent, _L("Publishing is canceled"));
        return;
    }

    if (result < 0) {
        BOOST_LOG_TRIVIAL(error) << "publish_job: failed, result = " << result;
        msg_text = _L("Publishing Failed");
    } else {
        BOOST_LOG_TRIVIAL(error) << "publish_job: publish ok.";
        wxCommandEvent* evt = new wxCommandEvent(m_publish_job_completed_id);
        evt->SetString(url);
        wxQueueEvent(m_plater, evt);
        m_job_finished = true;
    }
}

void PublishJob::finalize() {
    if (was_canceled()) return;

    Job::finalize();
}

}} // namespace Slic3r::GUI
