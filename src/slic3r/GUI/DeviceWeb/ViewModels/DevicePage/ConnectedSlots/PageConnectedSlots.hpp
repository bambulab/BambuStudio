/*****************************************************************//**
 * \file   PageConnectedSlots.hpp
 * \brief  Modal host window for the Connected Slots page.
 *
 * Owns the DeviceWebHost that serves `/device_page/connected_slots` and the
 * refresh pump that keeps it in sync with the device.
 *********************************************************************/
#ifndef CONNECTEDSLOTSPAGECONNECTEDSLOTS_HPP
#define CONNECTEDSLOTSPAGECONNECTEDSLOTS_HPP

#include "slic3r/GUI/GUI_Utils.hpp"

#include <nlohmann/json.hpp>

#include <wx/timer.h>

namespace Slic3r { namespace GUI {

class ConnectedSlotsVM;

class ConnectedSlotsDialog : public DPIDialog
{
public:
    explicit ConnectedSlotsDialog(wxWindow* parent);
    ~ConnectedSlotsDialog() override;

    /// Nothing ticks a dialog-local DeviceWebHost - StatusPanel only pumps the
    /// hosts it owns - so without this pump the page would stay on the snapshot
    /// it received when the dialog opened.
    void StartRefreshPump(ConnectedSlotsVM* vm);

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;

private:
    void OnRefreshTimer(wxTimerEvent& evt);

    wxTimer           m_refresh_timer;
    ConnectedSlotsVM* m_vm{nullptr}; ///< Owned by the host's manager, not by us.
};

/// Opens the dialog for the hotend addressed by `payload["addr_id"]`, bound to
/// the machine currently selected in the device manager.
void open_connected_slots_dialog(const nlohmann::json& payload);

}} // namespace Slic3r::GUI

#endif // CONNECTEDSLOTSPAGECONNECTEDSLOTS_HPP
