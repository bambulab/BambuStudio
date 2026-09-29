#ifndef FILAMENT_NOZZLE_POPUP_HPP
#define FILAMENT_NOZZLE_POPUP_HPP

#include <set>
#include <string>
#include <utility>
#include <vector>

#include <wx/string.h>

#include "Widgets/CascadeMenu.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r { namespace GUI {

// Transient two-level menu for a filament slot's nozzle: diameters at the first level, each opening
// the flow types that diameter is made in. The pair is picked in one gesture, so a slot can never be
// left on a combination no nozzle exists in -- which the flat "0.2 Standard / 0.4 High Flow / ..."
// list this replaced could only avoid by enumerating the rack and hiding everything else.
//
// Both levels list what the printer could have rather than only what is installed, because a row the
// user cannot pick is what tells them a nozzle is missing; silently omitting it reads as the option
// not existing. Two kinds of unavailable row say which is which: a combination no nozzle is made in
// (or this filament cannot print with) is greyed, while one the printer supports but the rack does
// not carry is faded out and says so on hover.
//
// Rebuilt on every popup, so swapping the printer's nozzles is picked up straight away. Picking a
// row writes back both the diameter and the flow type immediately and closes both levels.
// Dismissed by clicking outside it (default wxPopupTransientWindow behavior).
class FilamentNozzlePopup : public CascadeMenuPopup
{
public:
    explicit FilamentNozzlePopup(wxWindow *parent);

    // Rebuilds the menu for the given filament and shows it.
    // Caller is responsible for positioning it (SetPosition) beforehand.
    void tryPopup(int filament_idx);

private:
    // One flow type offered for a diameter. Both flags have to hold for the row to be pickable, and
    // which one failed decides what the row says on hover -- see the class comment.
    struct FlowRow
    {
        NozzleVolumeType volume{nvtStandard};
        wxString         label;
        bool             installed{true}; // the nozzle settings carry one of these in this diameter
        bool             usable{true};    // this slot's filament can print with it
    };

    void                 read_slot_state();
    void                 build_menu();
    void                 open_flow_menu(const std::string &diameter, const wxWindow *anchor);
    std::vector<FlowRow> flow_rows_for(const std::string &diameter) const;
    void                 apply_pick(const std::string &diameter, NozzleVolumeType volume);
    void                 close_menus();

    int              m_filament_idx = -1;
    std::string      m_diameter;             // the diameter this slot is on right now
    NozzleVolumeType m_volume{nvtStandard};  // and its flow type

    // What the printer is sold with, what it currently carries, and what this slot's filament can
    // print with -- read once per popup so the row builders do not each re-derive them.
    std::vector<std::string>                          m_printer_diameters;
    std::set<std::pair<std::string, NozzleVolumeType>> m_rack;
    std::vector<std::string>                          m_filament_diameters;
    std::vector<std::string>                          m_filament_variants;
    bool                                              m_filament_variants_complete = false;

    CascadeMenuPopup *m_sub_menu = nullptr;
};

// 按 project_config 里的逐槽口径 / 流量，重新推导三个 map 写回全局和各个盘。
// 用户改了逐槽口径 / 流量 / 预设后调用，且必须紧跟 sync_custom_map_mode_with_diameter_mode()：
// 它要按盘的模式决定写不写。
void apply_nozzle_manual_maps_to_plates();

// 「自定义」在多口径模式下写作 fmmNozzleManual，其余情况写作 fmmManual，本函数在两者间双向转换。
// 幂等，只看当前状态、不依赖开关翻转事件：打开 3mf 时开关状态是直接读进来的，没有事件可挂。
void sync_custom_map_mode_with_diameter_mode();

}} // namespace Slic3r::GUI

#endif
