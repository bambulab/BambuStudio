/*****************************************************************//**
 * \file   DevMappingHelpers.cpp
 * \brief  Implementations of shared AMS / hotend mapping helpers.
 *
 * \author xin.zhang
 * \date   May 2026
 *********************************************************************/
#include "DevMappingHelpers.h"

#include <boost/log/trivial.hpp>

#include "GUI_App.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Utils.hpp"

#include "DeviceCore/DevDefs.h"
#include "DeviceCore/DevFilaSystem.h"
#include "DeviceCore/DevManager.h"
#include "DeviceCore/DevNozzleRack.h"
#include "DeviceCore/DevNozzleSystem.h"

namespace Slic3r { namespace GUI {

ExpectedNozzleSpec ResolveExpectedNozzleSpec(int filament_id)
{
    ExpectedNozzleSpec spec;

    auto* preset_bundle = wxGetApp().preset_bundle;
    if (!preset_bundle) return spec;

    const auto& project_config = preset_bundle->project_config;
    const auto& full_config    = preset_bundle->full_config();

    int extruder_idx = filament_id;
    const auto* opt_fmap = project_config.option<ConfigOptionInts>("filament_map");
    if (opt_fmap && filament_id >= 0 && (size_t)filament_id < opt_fmap->size()) {
        extruder_idx = opt_fmap->get_at(filament_id) - 1;
    }
    if (extruder_idx < 0) return spec;

    const auto* opt_dia = full_config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
    if (!opt_dia || (size_t)extruder_idx >= opt_dia->size()) return spec;
    spec.diameter = (float)opt_dia->get_at(extruder_idx);
    spec.valid    = true;

    const auto* opt_vol = project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type");
    if (opt_vol && (size_t)extruder_idx < opt_vol->size()) {
        // Hybrid maps to NONE_FLOWTYPE here, which the per-slot check
        // treats as "any flow accepted".
        spec.flow = DevNozzle::ToNozzleFlowType((NozzleVolumeType)opt_vol->get_at(extruder_idx));
    }

    const auto* opt_nt = full_config.option<ConfigOptionEnumsGenericNullable>("nozzle_type");
    if (opt_nt && (size_t)extruder_idx < opt_nt->size()) {
        spec.nozzle_type = (NozzleType)opt_nt->get_at(extruder_idx);
    }

    return spec;
}

std::optional<DevNozzle> QueryRackNozzleAt(MachineObject*  obj,
                                           const TrayData& td)
{
    if (!obj) return std::nullopt;

    auto tray_opt = obj->get_tray(std::to_string(td.ams_id), std::to_string(td.slot_id));
    if (!tray_opt.has_value()) return std::nullopt;

    auto np = tray_opt->get_nozzle_pos_id(obj);
    if (!np.has_value() || np.value() < 0x10) return std::nullopt;

    return obj->GetNozzleSystem()->GetNozzleOptByAddrId(np.value() - 0x10);
}

std::optional<TrayData> MakeTrayData(MachineObject*    obj,
                                     DevAms*           ams,
                                     DevAmsTray*       tray,
                                     const std::string& ams_id_str)
{
    int ams_id  = 0;
    int tray_id = 0;
    try {
        ams_id  = std::stoi(ams_id_str);
        tray_id = atoi(tray->id.c_str());
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":[error] invalid ams_id or tray_id: " << e.what();
        return std::nullopt;
    }

    TrayData td;
    td.ams_id   = ams_id;
    td.ams_type = tray->ams_type;
    if (tray->ams_type != DevAmsType::EXT_SPOOL) {
        td.slot_id = tray_id;
        if (ams) {
            td.id = ams->GetTrayId(td.slot_id);
        }
    } else {
        td.slot_id = 0;
        td.id      = td.ams_id;
        if (obj->GetFilaSystem()->GetVirtualTrays().size() > 2) {
            td.ams_slot_name = wxString::Format("Ext-%d", 256 - td.id).ToStdString();
        }
    }

    if (tray->ams_type != EXT_SPOOL && !tray->is_exists) {
        td.type = EMPTY;
    } else {
        if (!tray->is_tray_info_ready()) {
            td.type = THIRD;
        } else {
            td.type          = NORMAL;
            td.remain        = tray->remain;
            td.colour        = DevAmsTray::decode_color(tray->color);
            td.name          = tray->get_display_filament_type();
            td.filament_type = tray->get_filament_type();
            td.ctype         = tray->ctype;
            for (const auto& col : tray->cols) {
                td.material_cols.push_back(DevAmsTray::decode_color(col));
            }
        }
    }

    return td;
}

}} // namespace Slic3r::GUI
