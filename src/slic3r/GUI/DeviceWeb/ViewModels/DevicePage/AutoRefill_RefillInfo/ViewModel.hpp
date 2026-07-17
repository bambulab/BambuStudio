/*****************************************************************//**
 * \file   ViewModel.hpp
 * \brief  ViewModel for the RefillVersion::RefillInfo auto-refill dialog.
 *
 * Serializes the ordered refill groups (kept by DevFilaAutoRefillMgr) into the
 * `auto_refill_refill_info` JSON contract for the React dialog. Phase 1 is
 * read-only: it reports state and answers state queries but issues no device
 * command. Reorder / add / remove are reserved for a later phase once a
 * write-back protocol exists.
 *********************************************************************/
#ifndef AUTOREFILLREFILLINFOVM_HPP
#define AUTOREFILLREFILLINFOVM_HPP

#include "slic3r/GUI/DeviceWeb/IViewModel.hpp"
#include "Schema.hpp"

#include <cstdint>
#include <string>

namespace Slic3r {
class MachineObject;
struct DevFilaAutoRefillGroup;
struct DevAmsRefillSlot;
namespace GUI {

class AutoRefillRefillInfoVM : public IViewModel {
public:
    std::string GetModule() const override { return "device_page_auto_refill_refill_info"; }

    nlohmann::json OnCommand(const std::string& submod,
                             const std::string& action,
                             const nlohmann::json& payload) override;
    void ReportState(const std::string& submod, const std::string& action) override;
    void OnSysColorChanged() override;

    // Called by the host dialog before it is shown to push the latest snapshot
    // into the (possibly still hidden) WebView, mirroring the mapping popup.
    void Refresh(MachineObject* obj);

    std::uint32_t SessionSeq() const { return m_session_seq; }

private:
    // Build the typed state tree from the current machine, then serialize it
    // through the Schema `format` structs (single source of truth for the wire
    // shape).
    nlohmann::json BuildState() const;
    AutoRefillRefillInfoSchema::format::RefillGroup BuildGroup(const DevFilaAutoRefillGroup& group) const;
    AutoRefillRefillInfoSchema::format::RefillSlot  BuildSlot(const DevAmsRefillSlot& refill_slot) const;

    // Shared body for ReportState(). `force` bypasses the host visibility gate
    // so Refresh() can pre-render into a hidden dialog WebView.
    void ReportStateImpl(const std::string& submod, const std::string& action, bool force);

    MachineObject* m_obj{nullptr};
    std::string    m_last_report_state;
    std::uint32_t  m_session_seq{0};
};

}} // namespace Slic3r::GUI

#endif // AUTOREFILLREFILLINFOVM_HPP
