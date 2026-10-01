#include "LiveViewTrackContext.h"

#include <boost/log/trivial.hpp>

#include "../GUI_App.hpp"
#include "../../Utils/NetworkAgent.hpp"

namespace BambuLiveViewTrack {

LiveViewTrackContext& LiveViewTrackContext::instance()
{
    static LiveViewTrackContext s_instance;
    return s_instance;
}

void LiveViewTrackContext::init_client(const ClientInfo& info)
{
    std::lock_guard<std::mutex> lock(mutex_);
    client_ = info;
}

void LiveViewTrackContext::init_session(const SessionInfo& info)
{
    std::lock_guard<std::mutex> lock(mutex_);
    session_ = info;
}

void LiveViewTrackContext::emit(const char* event_name,
                                const EmitParams& params,
                                const ChannelInfo* channel)
{
    if (event_name == nullptr || *event_name == '\0') {
        return;
    }

    std::string envelope;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        envelope = build_envelope_json(event_name, client_, session_, channel, params).dump();
    }
    sink_emit(event_name, envelope);
}

void LiveViewTrackContext::sink_emit(const char* event_name,
                                     const std::string& envelope_json)
{
    BOOST_LOG_TRIVIAL(info) << "[liveview_track] " << event_name
                             << " " << envelope_json;

    if (auto* agent = Slic3r::GUI::wxGetApp().getAgent()) {
        agent->track_event(event_name, envelope_json);
    }
}

}  // namespace BambuLiveViewTrack
