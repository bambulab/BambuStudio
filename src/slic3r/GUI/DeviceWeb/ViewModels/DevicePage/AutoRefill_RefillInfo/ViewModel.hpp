/*****************************************************************//**
 * \file   ViewModel.hpp
 * \brief  Host ViewModel for the RefillInfo auto-refill dialog.
 *
 * Dispatches per-page Schema / Builder / Action:
 *   refill_info/            - management page (submod `state` / `action`)
 *   add_refill_filaments/   - add-refill-filaments picker (submod `add_refill_filaments`)
 *********************************************************************/
#ifndef AUTOREFILLREFILLINFOVM_HPP
#define AUTOREFILLREFILLINFOVM_HPP

#include "slic3r/GUI/DeviceWeb/IViewModel.hpp"
#include "slic3r/GUI/DeviceCore/DevFilaAutoRefillEdit.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r {
class DevCtrlToken;
class MachineObject;
namespace GUI {

class AutoRefillRefillInfoActions;
class AutoRefillAddRefillFilamentsActions;

class AutoRefillRefillInfoVM : public IViewModel {
    friend class AutoRefillRefillInfoActions;
    friend class AutoRefillAddRefillFilamentsActions;

public:
    ~AutoRefillRefillInfoVM() override;

    std::string GetModule() const override { return "device_page_auto_refill_refill_info"; }

    nlohmann::json OnCommand(const std::string& submod,
                             const std::string& action,
                             const nlohmann::json& payload) override;
    void ReportState(const std::string& submod, const std::string& action) override;
    void OnSysColorChanged() override;

    void Refresh(MachineObject* obj);
    /* Re-reports the state from the current device data without starting a new
     * session. Driven by the host dialog's refresh pump so the page follows the
     * `RefillInfo` pushes that land after an edit is acknowledged; the report
     * itself is deduplicated, so unchanged ticks cost nothing. */
    void UpdateByMachine(MachineObject* obj);

    std::uint32_t SessionSeq() const { return m_session_seq; }

    /* Add-refill-filaments picker.
     * The slot list depends on MQTT refill_command 5 (Query), whose reply is
     * asynchronous, so the picker state cannot be returned as the response to
     * the web request that starts it. StartAddRefillQuery() only publishes the
     * command; the parsed result (or the failure) is pushed later as a report
     * on the `add_refill_filaments` submod. */
    bool StartAddRefillQuery();
    /* Same round-trip for refill_command 6 (QueryNext), sent once the user has
     * picked the first slot of the group. `picker_payload` is the picker
     * request that triggered it and is replayed into the reported state, so
     * the selection survives the round-trip. */
    bool StartAddRefillQueryNext(const DevAmsSlotId& to, const nlohmann::json& picker_payload);
    void ClearAddRefillQuery();
    // Creates the group the user just picked. The first slot heads it and the
    // whole creation settles on one ACK.
    bool StartConfirmAddRefill(const std::vector<DevAmsSlotId>& slots);
    // Appends the selected slots to an existing group, waiting for each ACK
    // before publishing the next operation.
    bool StartAddRefillToGroup(const DevAmsSlotId& group_member, const std::vector<DevAmsSlotId>& slots);

    // Management-page writes. Reset targets a chain by its tag (chain_fila /
    // filament_id). Remove unlinks one visible external slot.
    bool StartResetGroup(const std::string& group_tag);
    /* Settles the native reset confirmation, same asynchronous shape as
     * FinishRemoveConfirmation below. The success toast is fixed here because a
     * reset always drops the same thing: the manually added members. */
    void FinishResetConfirmation(const std::string& group_tag, bool confirmed);
    bool StartRemoveSlot(const DevAmsSlotId& slot);
    /* Settles the native removal confirmation. The dialog cannot run inside the
     * bridge request (a nested modal would re-enter the WebView2 message
     * handler), so the answer comes back here from a CallAfter and is pushed to
     * the page as a report. `toast_key` is the untranslated success text the
     * page shows once the printer acknowledges the removal. */
    void FinishRemoveConfirmation(const DevAmsSlotId& slot, bool confirmed, const std::string& toast_key);
    // Relinks `slot` onto `member`. `as_head` makes `slot` the new chain head.
    bool StartMoveSlot(const DevAmsSlotId& slot, const DevAmsSlotId& member, bool as_head);

private:
    /* Diagnostics: the parsed Query / QueryNext reply next to the schemas built
     * from it, so a wrong card can be traced back to the device payload.
     * Debug builds only - the page's affordance is gated the same way. */
    nlohmann::json BuildDebugSnapshot() const;
    void ReportStateImpl(const std::string& submod, const std::string& action, bool force);
    void ReportAddRefillQuery(const std::string& action, int code, const std::string& msg);
    // Adopts `token` as the pending query and reports its outcome.
    bool WatchAddRefillQuery(std::shared_ptr<DevCtrlToken> token);
    bool WatchRefillEdit(std::shared_ptr<DevCtrlToken> token, const std::string& action);
    bool ContinueAddRefillToGroup();
    void ClearRefillEdit();
    void ReportRefillEdit(const std::string& action, int code, const std::string& msg);

    MachineObject* m_obj{nullptr};
    std::string    m_last_report_state;
    std::uint32_t  m_session_seq{0};

    std::shared_ptr<DevCtrlToken>           m_add_refill_query_token;
    std::optional<DevFilaRefillQueryResult> m_add_refill_query_result;
    // Raw refill_command 5 / 6 reply, kept for the debug snapshot only. Stored
    // before parsing, so an unparsable reply can still be inspected.
    nlohmann::json                          m_add_refill_query_reply = nlohmann::json::object();
    // Picker request behind the pending query; empty for the initial Query.
    nlohmann::json                          m_add_refill_query_payload = nlohmann::json::object();
    std::shared_ptr<DevCtrlToken>           m_refill_edit_token;
    std::optional<DevAmsSlotId>             m_add_to_group_member;
    std::vector<DevAmsSlotId>               m_add_to_group_pending;
    std::shared_ptr<bool>                    m_alive = std::make_shared<bool>(true);
};

}} // namespace Slic3r::GUI

#endif // AUTOREFILLREFILLINFOVM_HPP
