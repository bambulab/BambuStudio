/*****************************************************************//**
 * \file   ViewModel.hpp
 * \brief  ViewModel for the Connected Slots dialog (M2Y filament-hotend).
 *
 * Serializes the per-hotend layout tree (top_slots_area.left_slot /
 * top_slots_area.right_slots / bottom_hotend) into the
 * `device_page_connected_slots` JSON contract.
 *
 * Implementation is split by concern: ViewModel.cpp dispatches commands and
 * reports state, ViewModelState.cpp builds the payload, ViewModelActions.cpp
 * executes the `action` submod commands.
 *********************************************************************/
#ifndef CONNECTEDSLOTSVM_HPP
#define CONNECTEDSLOTSVM_HPP

#include "slic3r/GUI/DeviceWeb/IViewModel.hpp"
#include "Schema.hpp"

#include <cstdint>
#include <string>

namespace Slic3r {
class MachineObject;
namespace GUI {

class ConnectedSlotsActions;

class ConnectedSlotsVM : public IViewModel {
    friend class ConnectedSlotsActions;

public:
    std::string GetModule() const override { return "device_page_connected_slots"; }

    nlohmann::json OnCommand(const std::string& submod,
                             const std::string& action,
                             const nlohmann::json& payload) override;
    void ReportState(const std::string& submod, const std::string& action) override;
    void OnSysColorChanged() override;

    /// Called by the host dialog before it is shown to push the latest snapshot
    /// into the (possibly still hidden) WebView, mirroring AutoRefill / mapping.
    void Refresh(MachineObject* obj, int addr_id);

    /// Heartbeat from the host dialog. Re-binds the machine (it may have been
    /// switched or dropped while the dialog was open) and pushes the state, but
    /// keeps addr_id and the user's source selection. ReportState() de-dupes on
    /// the serialized state, so a tick with no device change sends nothing.
    void UpdateByMachine(MachineObject* obj);

    std::uint32_t SessionSeq() const { return m_session_seq; }

private:
    nlohmann::json BuildState() const;

    void ReportStateImpl(const std::string& submod, const std::string& action, bool force);

    MachineObject* m_obj{nullptr};
    int            m_addr_id{-1};
    std::string    m_last_report_state;
    std::uint32_t  m_session_seq{0};
    /// 1-based index into right_slots.schematic.items chosen by the user; -1 keeps
    /// following whatever source currently feeds the nozzle.
    int            m_selected_ext_index{-1};
};

}} // namespace Slic3r::GUI

#endif // CONNECTEDSLOTSVM_HPP
