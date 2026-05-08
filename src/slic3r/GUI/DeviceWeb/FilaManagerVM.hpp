// src/slic3r/GUI/DeviceWeb/FilaManagerVM.hpp
#ifndef FILAMANAGERVM_HPP
#define FILAMANAGERVM_HPP

#include "IViewModel.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace Slic3r { namespace GUI {

class FilaManagerVM : public IViewModel {
public:
    FilaManagerVM();
    ~FilaManagerVM() override;

    std::string GetModule() const override { return "filament"; }

    nlohmann::json OnCommand(
        const std::string& submod,
        const std::string& action,
        const nlohmann::json& payload
    ) override;

    void ReportState(const std::string& submod, const std::string& action) override;

    void OnSysColorChanged() override;

private:
    nlohmann::json MakeResp(const std::string& submod, const std::string& action,
        int code, const std::string& msg = "",
        const nlohmann::json& payload = nlohmann::json::object());

    // Resource handlers
    nlohmann::json HandleSpool(const std::string& action, const nlohmann::json& payload);
    nlohmann::json HandlePreset(const std::string& action, const nlohmann::json& payload);
    nlohmann::json HandleMachine(const std::string& action, const nlohmann::json& payload);
    nlohmann::json HandleAms(const std::string& action, const nlohmann::json& payload);
    nlohmann::json HandleInit(const std::string& action, const nlohmann::json& payload);
    nlohmann::json HandleSync(const std::string& action, const nlohmann::json& payload);
    nlohmann::json HandleConfig(const std::string& action, const nlohmann::json& payload);

    // Data builders (migrated from wgtFilaManagerPanel)
    nlohmann::json build_spool_list();
    nlohmann::json build_preset_options();
    nlohmann::json build_machine_list();
    nlohmann::json build_ams_data();

    // Cloud state helpers
    nlohmann::json build_sync_state() const;
    void publish_sync_state();
    void publish_pull_done(int added, int updated);
    void publish_push_failed(const std::string& spool_id,
                             const std::string& op,
                             int code,
                             const std::string& message);
    // F4.5 self-test follow-up: front-end needs an explicit success signal so
    // it can confirm the cloud round-trip rather than inferring it from
    // `sync/state` timestamps alone.
    void publish_push_done(const std::string& spool_id, const std::string& op);
    void publish_debug_log(const std::string& category,
                           const std::string& level,
                           const std::string& title,
                           const std::string& summary,
                           const nlohmann::json& detail = nlohmann::json::object());

    // Cached cloud filament config (brands / types) — populated by
    // HandleConfig("fetch") and returned verbatim on subsequent calls.
    nlohmann::json m_cached_cloud_config = nlohmann::json::object();

    // STUDIO-17956: counter of "create" push ops that have succeeded since the
    // last pull_done, used to correct publish_pull_done's `added`/`updated`.
    // Dispatcher may insert an accepted create response before the follow-up
    // pull, so the pull's size-diff estimate can be 0. Each create push_done
    // that already has a cloud id bumps this counter by 1; the next
    // publish_pull_done consumes and clears it, so the toast shows "+1 added".
    // Only touched on the wx UI thread (dispatcher callbacks are UI-thread
    // CallAfter), so no locking is needed.
    int m_pending_pull_added_hint = 0;
};

}} // namespace Slic3r::GUI

#endif // FILAMANAGERVM_HPP
