/*****************************************************************//**
 * \file   DevSendPrintHelpers.cpp
 * \brief  Shared helpers for send-print UI text.
 *
 * \author xin.zhang
 * \date   November 2026
 *********************************************************************/
#include "DevSendPrintHelpers.h"

#include <set>

#include "I18N.hpp"
#include "GUI_App.hpp"
#include "DeviceManager.hpp"
#include "DeviceCore/DevConfigUtil.h"
#include "DeviceCore/DevDefs.h"
#include "DeviceCore/DevExtruderSystem.h"
#include "DeviceCore/DevNozzleSystem.h"
#include "DeviceCore/DevSliceContext.hpp"

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"

namespace Slic3r::GUI {

namespace {

bool s_get_slice_nozzle_spec(const std::shared_ptr<DevSliceContext>& slice_context,
                             int                                      extruder_index,
                             double&                                  diameter,
                             NozzleVolumeType&                        volume_type)
{
    if (!slice_context)
        return false;

    std::set<double>           diameters;
    std::set<NozzleVolumeType> volume_types;
    auto collect = [&](const std::vector<MultiNozzleUtils::NozzleInfo>& nozzles) {
        for (const auto& nozzle : nozzles) {
            if (nozzle.extruder_id != extruder_index || nozzle.diameter.empty())
                continue;

            const double value = string_to_double_decimal_point(nozzle.diameter.c_str());
            if (value <= 0.0)
                continue;

            diameters.insert(value);
            volume_types.insert(nozzle.volume_type);
        }
    };

    collect(slice_context->used_logic_nozzles);
    if (diameters.empty()) {
        for (const auto& [_, nozzles] : slice_context->nozzles_by_filament)
            collect(nozzles);
    }

    if (diameters.empty())
        return false;

    diameter = *diameters.begin();
    volume_type = volume_types.size() > 1 ? NozzleVolumeType::nvtHybrid
                                          : (volume_types.empty() ? NozzleVolumeType::nvtStandard : *volume_types.begin());
    return true;
}

wxString s_format_nozzle_spec(double diameter, NozzleVolumeType volume_type)
{
    wxString spec = wxString::FromUTF8(format_diameter_to_str(diameter));
    const std::string volume_short = DevNozzle::ToNozzleVolumeShortString(volume_type);
    if (!volume_short.empty())
        spec += " " + wxString::FromUTF8(volume_short);
    return spec;
}

wxString s_get_base_nozzle_title(int total_extruder_count,
                                 const std::string& printer_type,
                                 int extruder_id)
{
    if (total_extruder_count != 2)
        return _L("Nozzle");

    if (!printer_type.empty()) {
        return _L(DevPrinterConfigUtil::get_toolhead_display_name(printer_type, extruder_id, ToolHeadComponent::Nozzle, ToolHeadNameCase::TitleCase));
    }

    return extruder_id == DEPUTY_EXTRUDER_ID ? _L("Left nozzle") : _L("Right nozzle");
}

} // namespace

wxString GetNozzleTitleLabel(int total_extruder_count,
                             const std::string& printer_type,
                             const std::shared_ptr<DevSliceContext>& slice_context,
                             MachineObject* obj,
                             int extruder_id)
{
    wxString title = s_get_base_nozzle_title(total_extruder_count, printer_type, extruder_id);
    if (slice_context) /*send print*/ {
        if (slice_context->HasMultipleLogicNozzleDiameters()) {
            const int slice_extruder_index = total_extruder_count == 2 ? (extruder_id == DEPUTY_EXTRUDER_ID ? 0 : 1) : 0;

            double diameter = 0.0;
            NozzleVolumeType volume_type = NozzleVolumeType::nvtStandard;
            if (s_get_slice_nozzle_spec(slice_context, slice_extruder_index, diameter, volume_type)) {
                return wxString::Format("%s (%s)", title, s_format_nozzle_spec(diameter, volume_type));
            }
            
            if (obj) {
                const DevNozzle nozzle = obj->GetNozzleSystem()->GetExtNozzle(extruder_id);
                if (!nozzle.IsEmpty() && !nozzle.IsUnknown() && !nozzle.IsAbnormal() && nozzle.IsHotend()) {
                    const NozzleVolumeType volume_type = DevNozzle::ToNozzleVolumeType(nozzle.GetNozzleFlowType());
                    return wxString::Format("%s (%s)", title, s_format_nozzle_spec(nozzle.GetNozzleDiameter(), volume_type));
                }
            }
        }
    } else if (obj && obj->GetExtderSystem()->GetTotalExtderCount() == 2) /* the sync dialog */ {
        const PresetBundle* preset_bundle = wxGetApp().preset_bundle;
        if (preset_bundle && multi_nozzle_diameter_mode_enabled(preset_bundle->project_config)) {
            const DevNozzle nozzle = obj->GetNozzleSystem()->GetExtNozzle(extruder_id);
            if (!nozzle.IsEmpty() && !nozzle.IsUnknown() && !nozzle.IsAbnormal() && nozzle.IsHotend()) {
                const NozzleVolumeType volume_type = DevNozzle::ToNozzleVolumeType(nozzle.GetNozzleFlowType());
                return wxString::Format("%s (%s)", title, s_format_nozzle_spec(nozzle.GetNozzleDiameter(), volume_type));
            }
        }
    }

    return title;
}

wxString GetSliceFilamentNozzleSpec(const std::shared_ptr<DevSliceContext>& slice_context, int filament_id)
{
    if (!slice_context)
        return wxEmptyString;

    const auto iter = slice_context->nozzles_by_filament.find(filament_id);
    if (iter == slice_context->nozzles_by_filament.end())
        return wxEmptyString;

    std::set<double>           diameters;
    std::set<NozzleVolumeType> volume_types;
    for (const auto& nozzle : iter->second) {
        if (nozzle.diameter.empty())
            continue;

        const double value = string_to_double_decimal_point(nozzle.diameter.c_str());
        if (value <= 0.0)
            continue;

        diameters.insert(value);
        volume_types.insert(nozzle.volume_type);
    }

    if (diameters.empty())
        return wxEmptyString;

    const double diameter = *diameters.begin();
    const NozzleVolumeType volume_type = volume_types.size() > 1
                                             ? NozzleVolumeType::nvtHybrid
                                             : (volume_types.empty() ? NozzleVolumeType::nvtStandard : *volume_types.begin());
    return s_format_nozzle_spec(diameter, volume_type);
}

} // namespace Slic3r::GUI
