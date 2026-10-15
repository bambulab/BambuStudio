/*****************************************************************//**
 * \file   DevMappingHelpers.cpp
 * \brief  Implementations of shared AMS / hotend mapping helpers.
 *
 * \author xin.zhang
 * \date   May 2026
 *********************************************************************/
#include "DevMappingHelpers.h"

#include <set>

#include <boost/lexical_cast.hpp>

#include "Plater.hpp"

#include "libslic3r/MultiNozzleUtils.hpp"
#include "libslic3r/LocalesUtils.hpp"

#include "DeviceCore/DevDefs.h"
#include "DeviceCore/DevFilaSystem.h"
#include "DeviceCore/DevManager.h"
#include "DeviceCore/DevNozzleRack.h"
#include "DeviceCore/DevNozzleSystem.h"
#include "DeviceCore/DevSliceContext.hpp"
#include "DeviceCore/DevUtilBackend.h"
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

namespace {

// Collapse the logical nozzles a filament was sliced with into the single
// (diameter, flow) pair the slot check compares against.
ExpectedNozzleSpec sCollapseNozzlesToSpec(const std::vector<MultiNozzleUtils::NozzleInfo>& nozzles)
{
    ExpectedNozzleSpec spec;
    if (nozzles.empty())
        return spec;

    std::set<float> diameters;
    std::set<NozzleFlowType> flows;
    for (const auto& nozzle : nozzles) {
        try {
            diameters.insert(static_cast<float>(boost::lexical_cast<double>(nozzle.diameter)));
        } catch (const std::exception&) {
            diameters.insert(static_cast<float>(string_to_double_decimal_point(nozzle.diameter.c_str())));
        }
        flows.insert(DevNozzle::ToNozzleFlowType(nozzle.volume_type));
    }
    if (diameters.empty())
        return spec;

    spec.diameter = *diameters.begin();
    spec.valid    = true;

    // Multiple flow types across layers -> Hybrid: skip strict flow check
    // (same semantics as DevMapping + existing preset Hybrid -> NONE_FLOWTYPE).
    if (flows.size() > 1)
        spec.flow = NozzleFlowType::NONE_FLOWTYPE;
    else if (!flows.empty())
        spec.flow = *flows.begin();

    return spec;
}

} // namespace

ExpectedNozzleSpec ResolveExpectedNozzleSpecFromSliceContext(int filament_id, const DevSliceContext& slice_context)
{
    const auto iter = slice_context.nozzles_by_filament.find(filament_id);
    if (iter == slice_context.nozzles_by_filament.end())
        return {};

    return sCollapseNozzlesToSpec(iter->second);
}

#if 0
// Superseded by ResolveExpectedNozzleSpecFromSliceContext, see the header for the reason.

ExpectedNozzleSpec ResolveExpectedNozzleSpecFromGroupResult(int filament_id, const MultiNozzleUtils::NozzleGroupResultBase* group_result)
{
    if (!group_result)
        return {};

    return sCollapseNozzlesToSpec(group_result->get_nozzles_for_filament(filament_id));
}

ExpectedNozzleSpec ResolveExpectedNozzleSpec(int filament_id, Plater* plater)
{
    if (!plater)
        return {};

    auto group_result = DevUtilBackend::GetNozzleGroupResult(plater);
    if (!group_result)
        return {};

    return ResolveExpectedNozzleSpecFromGroupResult(filament_id, group_result.get());
}
#endif

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
        BOOST_LOG_TRIVIAL(error) << "[error] invalid ams_id or " << KV(log_key::tray_id, e.what());
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
            td.remain_g      = tray->remain_g;
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
