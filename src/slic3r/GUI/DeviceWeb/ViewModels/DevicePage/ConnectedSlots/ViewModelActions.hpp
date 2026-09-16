/*****************************************************************//**
 * \file   ViewModelActions.hpp
 * \brief  Command side of the Connected Slots ViewModel.
 *
 * Every `action` submod command (dry and the tray context menu: edit / load /
 * unload / read) is executed here, keeping ViewModelState.cpp free of device writes.
 *********************************************************************/
#ifndef CONNECTEDSLOTSVIEWMODELACTIONS_HPP
#define CONNECTEDSLOTSVIEWMODELACTIONS_HPP

#include <nlohmann/json.hpp>

#include <string>

namespace Slic3r {
class MachineObject;
}

namespace Slic3r { namespace GUI {

class ConnectedSlotsVM;

class ConnectedSlotsActions
{
public:
    /// Runs one Connected Slots action and answers with the refreshed state.
    static nlohmann::json HandleTrayAction(ConnectedSlotsVM&     vm,
                                           MachineObject*        obj,
                                           const std::string&    action,
                                           const nlohmann::json& payload);
};

}} // namespace Slic3r::GUI

#endif // CONNECTEDSLOTSVIEWMODELACTIONS_HPP
