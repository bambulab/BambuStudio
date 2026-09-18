#include "SyncPresetWithPrinterDialog.hpp"

#include "Button.hpp"
#include "CheckBox.hpp"
#include "ComboBox.hpp"
#include "Label.hpp"
#include "StateColor.hpp"

#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/ExtruderHelpers.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <tuple>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <wx/sizer.h>
#include <wx/statbmp.h>

namespace Slic3r { namespace GUI {

namespace {

// `C(...)` re-maps a light-mode color to its dark-mode pair at draw time
inline wxColour C(const wxColour &light) { return StateColor::darkModeColorFor(light); }

constexpr int kDialogWidth = 554;
constexpr int kSidePadding = 30;
constexpr int kCellPadding = 12;
constexpr int kThumbSize = 36;
constexpr int kComboHeight = 24;

// Stand-ins for a target flow type, best first. Not the raw enum order: nvtE3DHighFlow sits at the
// end of NozzleVolumeType but belongs next to nvtHighFlow, and the two TPU types belong together.
const std::vector<NozzleVolumeType> &volume_fallback_order(NozzleVolumeType target)
{
    static const std::map<NozzleVolumeType, std::vector<NozzleVolumeType>> table{
        {nvtStandard, {nvtStandard, nvtHighFlow, nvtE3DHighFlow, nvtTPUHighFlow, nvtTPU285}},
        {nvtHighFlow, {nvtHighFlow, nvtE3DHighFlow, nvtStandard, nvtTPUHighFlow, nvtTPU285}},
        {nvtE3DHighFlow, {nvtE3DHighFlow, nvtHighFlow, nvtStandard, nvtTPUHighFlow, nvtTPU285}},
        {nvtTPUHighFlow, {nvtTPUHighFlow, nvtTPU285, nvtHighFlow, nvtE3DHighFlow, nvtStandard}},
        {nvtTPU285, {nvtTPU285, nvtTPUHighFlow, nvtHighFlow, nvtE3DHighFlow, nvtStandard}},
    };
    static const std::vector<NozzleVolumeType> fallback{nvtStandard, nvtHighFlow, nvtE3DHighFlow, nvtTPUHighFlow, nvtTPU285};

    auto it = table.find(target);
    return it == table.end() ? fallback : it->second;
}

int volume_rank(NozzleVolumeType target, NozzleVolumeType candidate)
{
    const auto &order = volume_fallback_order(target);
    auto it = std::find(order.begin(), order.end(), candidate);
    return it == order.end() ? (int) order.size() : (int) std::distance(order.begin(), it);
}

bool same_spec(const NozzleSpec &a, const NozzleSpec &b, bool with_flow_type)
{
    return with_flow_type ? a == b : a.same_diameter(b);
}

// Append unless already present, keeping encounter order.
void push_unique(std::vector<NozzleSpec> &specs, const NozzleSpec &spec, bool with_flow_type)
{
    auto it = std::find_if(specs.begin(), specs.end(), [&](const NozzleSpec &s) { return same_spec(s, spec, with_flow_type); });
    if (it == specs.end())
        specs.push_back(spec);
}

// What the printer physically carries, both extruders merged and deduplicated.
//
// An unreported flow type is dropped, not defaulted: ToNozzleVolumeType() would map NONE_FLOWTYPE
// to Standard with just a warning, while the rest of the codebase treats it as a hard error
// (SelectMachine.cpp reports PrintStatusNozzleDataInvalid), so it must not become a target here.
std::vector<NozzleSpec> collect_printer_nozzles(MachineObject *obj, bool with_flow_type)
{
    std::vector<NozzleSpec> specs;
    if (obj == nullptr || obj->GetNozzleSystem() == nullptr)
        return specs;

    auto *nozzle_system = obj->GetNozzleSystem();
    for (const std::map<int, DevNozzle> *nozzles : {&nozzle_system->GetRackNozzles(), &nozzle_system->GetExtNozzles()}) {
        for (const auto &[_, nozzle] : *nozzles) {
            if (nozzle.IsEmpty() || nozzle.IsAbnormal() || !nozzle.IsHotend())
                continue;
            const NozzleFlowType flow = nozzle.GetNozzleFlowType();
            if (flow == NozzleFlowType::NONE_FLOWTYPE)
                continue;
            if (nozzle.GetNozzleDiameter() <= 0.f)
                continue;
            push_unique(specs, NozzleSpec{nozzle.GetNozzleDiameter(), DevNozzle::ToNozzleVolumeType(flow)}, with_flow_type);
        }
    }

    std::sort(specs.begin(), specs.end(), [](const NozzleSpec &a, const NozzleSpec &b) {
        return std::tie(a.diameter, a.volume) < std::tie(b.diameter, b.volume);
    });
    return specs;
}

// A spec tagged with its filament slot. The slot cannot be inferred from the vector position
// because slots without a usable diameter are skipped.
struct SlotSpec
{
    size_t slot{0};
    NozzleSpec spec;
};

// The nozzle each filament slot prints with. Diameter from the per-slot selection in multi-diameter
// mode, from the printer preset otherwise. Flow type read only when it takes part, and then from the
// current plate with the project default as fallback.
std::vector<SlotSpec> collect_project_slot_specs(const PresetBundle &preset_bundle, bool multi_diameter_mode, bool with_flow_type)
{
    const Preset &preset = preset_bundle.printers.get_selected_preset();
    const DynamicPrintConfig &project_config = preset_bundle.project_config;

    const auto *diameter_map = project_config.option<ConfigOptionStrings>("filament_diameter_map");
    const auto *nozzle_diams = preset.config.option<ConfigOptionFloatsNullable>("nozzle_diameter");

    // nozzle_diameter is per extruder, but a fixed-extruder machine prints one diameter per plate and
    // its variant names that diameter, so every entry holds the same value. No filament_map lookup.
    const float preset_diameter = nozzle_diams != nullptr && !nozzle_diams->values.empty() ? (float) nozzle_diams->values.front() : 0.f;

    std::vector<int> plate_volume_map;
    if (with_flow_type) {
        if (Plater *plater = wxGetApp().plater()) {
            if (const PartPlate *plate = plater->get_partplate_list().get_curr_plate())
                plate_volume_map = plate->get_real_filament_volume_maps(project_config);
        }
        if (plate_volume_map.empty()) {
            if (const auto *opt = project_config.option<ConfigOptionInts>("filament_volume_map"))
                plate_volume_map = opt->values;
        }
    }

    std::vector<SlotSpec> specs;
    const size_t slot_count = preset_bundle.filament_presets.size();
    specs.reserve(slot_count);

    for (size_t i = 0; i < slot_count; ++i) {
        NozzleSpec spec;

        if (multi_diameter_mode) {
            if (diameter_map != nullptr && i < diameter_map->values.size() && !diameter_map->values[i].empty())
                spec.diameter = (float) std::strtod(diameter_map->values[i].c_str(), nullptr);

            // Validation clears the per-slot selection when only one diameter is configured. Fall
            // back the way the filament list does - to this slot's only option, which still carries
            // the effective diameter, unlike the canonical 0.2 printer preset.
            if (spec.diameter <= 0.f) {
                const std::vector<std::string> options = preset_bundle.get_filament_nozzle_diameter_options(i);
                if (!options.empty())
                    spec.diameter = (float) std::strtod(options.front().c_str(), nullptr);
            }
        }

        if (spec.diameter <= 0.f)
            spec.diameter = preset_diameter;

        if (spec.diameter <= 0.f)
            continue;

        if (with_flow_type && i < plate_volume_map.size())
            spec.volume = (NozzleVolumeType) plate_volume_map[i];

        specs.push_back(SlotSpec{i, spec});
    }

    return specs;
}

// One row per distinct (diameter, flow type) the project uses. Both sides run multi-diameter here,
// so each pair is separately selectable.
std::vector<NozzleSpec> fold_per_nozzle_rows(const std::vector<SlotSpec> &slot_specs,
                                             std::vector<std::vector<size_t>> &row_slots,
                                             std::vector<wxString> &row_labels)
{
    std::vector<NozzleSpec> rows;
    row_slots.clear();
    row_labels.clear();

    for (const SlotSpec &entry : slot_specs) {
        auto it = std::find(rows.begin(), rows.end(), entry.spec);
        if (it == rows.end()) {
            rows.push_back(entry.spec);
            row_slots.push_back({entry.slot});
        } else {
            row_slots[(size_t) std::distance(rows.begin(), it)].push_back(entry.slot);
        }
    }
    for (const NozzleSpec &row : rows)
        row_labels.push_back(format_nozzle_spec_label(row, true));
    return rows;
}

// The single row used whenever the flow type is out of the comparison. Only one diameter can be
// picked for the whole project there, so a dropdown per project diameter would let the user express
// something the target cannot honour. One row covering every slot, its left cell listing the
// diameters the project uses today.
std::vector<NozzleSpec> fold_single_diameter_row(const std::vector<SlotSpec> &slot_specs,
                                                 std::vector<std::vector<size_t>> &row_slots,
                                                 std::vector<wxString> &row_labels)
{
    std::vector<NozzleSpec> rows;
    row_slots.clear();
    row_labels.clear();
    if (slot_specs.empty())
        return rows;

    std::vector<NozzleSpec> diameters;
    std::vector<size_t> slots;
    for (const SlotSpec &entry : slot_specs) {
        push_unique(diameters, entry.spec, false);
        slots.push_back(entry.slot);
    }
    std::sort(diameters.begin(), diameters.end(), [](const NozzleSpec &a, const NozzleSpec &b) { return a.diameter < b.diameter; });

    wxString label;
    for (const NozzleSpec &spec : diameters) {
        if (!label.IsEmpty())
            label += ", ";
        label += format_nozzle_spec_label(spec, false);
    }

    // Only the slot list and label carry information; the diameter is a placeholder the dropdown
    // choice replaces.
    rows.push_back(diameters.front());
    row_slots.push_back(slots);
    row_labels.push_back(label);
    return rows;
}

// Whether the printer can serve everything the project requires. Must be checked on the unfolded
// per-slot specs: the collapsed row above throws diameters away.
bool printer_covers_project(const std::vector<NozzleSpec> &required, const std::vector<NozzleSpec> &printer_nozzles, bool with_flow_type)
{
    bool any_required = false;
    for (const NozzleSpec &spec : required) {
        if (spec.diameter <= 0.f)
            continue; // no usable spec, nothing to serve
        any_required = true;
        auto it = std::find_if(printer_nozzles.begin(), printer_nozzles.end(),
                               [&](const NozzleSpec &c) { return same_spec(c, spec, with_flow_type); });
        if (it == printer_nozzles.end())
            return false;
    }
    return any_required;
}

// Same lookup as Sidebar::update_printer_thumbnail(), with the shared placeholder as fallback.
wxBitmap printer_thumbnail(const std::string &printer_type, wxWindow *win)
{
    std::string name = "printer_preview_" + printer_type;
    if (!printer_type.empty()) {
        const std::string full_path = into_u8(Slic3r::GUI::from_u8(Slic3r::var(name + ".png")));
        boost::system::error_code ec;
        if (!boost::filesystem::exists(full_path, ec))
            name = "printer_placeholder";
    } else {
        name = "printer_placeholder";
    }
    return create_scaled_bitmap(name, win, kThumbSize);
}

// 1px framed table: the outer panel's background paints the border, the row host sits 1px inset.
wxPanel *make_table_frame(wxWindow *parent, wxPanel *&rows_host)
{
    auto *frame = new wxPanel(parent);
    frame->SetBackgroundColour(C(ThemeColor::Grey300));

    rows_host = new wxPanel(frame);
    rows_host->SetBackgroundColour(C(ThemeColor::White));

    auto *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(rows_host, 1, wxEXPAND | wxALL, 1);
    frame->SetSizer(sizer);
    return frame;
}

// Shared by both tables. Two equal columns so the tables line up with each other, unlike the
// mock-up's per-table widths.
wxPanel *make_header_row(wxWindow *parent, const wxString &left, const wxString &right)
{
    auto *row = new wxPanel(parent);
    row->SetBackgroundColour(C(ThemeColor::Grey300));

    auto *sizer = new wxBoxSizer(wxHORIZONTAL);
    for (const wxString &text : {left, right}) {
        auto *label = new Label(row, text);
        label->SetFont(Label::Head_13);
        label->SetForegroundColour(C(ThemeColor::TextPrimary));
        sizer->Add(label, 1, wxALIGN_CENTER_VERTICAL | wxALL, row->FromDIP(kCellPadding) / 2);
    }
    row->SetSizer(sizer);
    return row;
}

} // namespace

wxString format_nozzle_spec_label(const NozzleSpec &spec, bool with_flow_type)
{
    const wxString diameter = wxString::FromUTF8(get_diameter_string(spec.diameter));
    if (!with_flow_type)
        return diameter;
    const wxString volume = DevNozzle::GetNozzleVolumeTypeStr(spec.volume);
    return volume.IsEmpty() ? diameter : diameter + " " + volume;
}

int find_closest_nozzle(const NozzleSpec &want, const std::vector<NozzleSpec> &candidates, bool with_flow_type)
{
    int best = -1;
    // Lexicographic key: diameter distance first, then the coarser nozzle on a tie (hence the
    // negated diameter), then how good a stand-in the flow type is.
    std::tuple<float, float, int> best_key;

    for (size_t i = 0; i < candidates.size(); ++i) {
        const NozzleSpec &candidate = candidates[i];
        const int flow = with_flow_type ? volume_rank(want.volume, candidate.volume) : 0;
        const std::tuple<float, float, int> key{std::abs(candidate.diameter - want.diameter), -candidate.diameter, flow};
        if (best < 0 || key < best_key) {
            best = (int) i;
            best_key = key;
        }
    }
    return best;
}

std::optional<PrinterMigrationInfo> collect_printer_migration_info(MachineObject *obj, bool offer_skip_future_checks)
{
    if (obj == nullptr)
        return std::nullopt;
    PresetBundle *preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle == nullptr)
        return std::nullopt;

    PrinterMigrationInfo info;
    info.offer_skip_future_checks = offer_skip_future_checks;

    Preset &preset = preset_bundle->printers.get_selected_preset();
    info.source_model_id = preset.get_printer_type(preset_bundle);
    if (const VendorProfile::PrinterModel *pm = PresetUtils::system_printer_model(preset))
        info.source_model_name = wxString::FromUTF8(pm->name);
    else
        info.source_model_name = wxString::FromUTF8(info.source_model_id);

    info.target_model_id = obj->get_show_printer_type();
    info.target_model_name = obj->get_printer_type_display_str();
    info.target_dev_name = wxString::FromUTF8(obj->get_dev_name());

    // Asked of the machine being migrated *to*, not the currently selected preset.
    const Preset *target_preset = get_printer_preset(obj);

    // See PrinterMigrationInfo::compare_flow_type.
    // The project flag outlives a printer switch, so an H2D (or any non-rack) 3mf can still
    // carry multi_nozzle_diameter_mode=1 and a leftover filament_diameter_map. Per-slot
    // diameters are only real on a rack; otherwise the project's diameter is printer_variant.
    const bool multi_diameter_mode = multi_nozzle_diameter_mode_enabled(preset_bundle->project_config) &&
                                     is_single_ext_multi_nozzle(preset.config);
    const bool target_has_rack = target_preset != nullptr && is_single_ext_multi_nozzle(target_preset->config);
    info.compare_flow_type = multi_diameter_mode && target_has_rack;

    const std::vector<SlotSpec> slot_specs = collect_project_slot_specs(*preset_bundle, multi_diameter_mode, info.compare_flow_type);
    info.slot_nozzles.assign(preset_bundle->filament_presets.size(), NozzleSpec{});
    for (const SlotSpec &entry : slot_specs)
        if (entry.slot < info.slot_nozzles.size())
            info.slot_nozzles[entry.slot] = entry.spec;

    info.printer_nozzles = collect_printer_nozzles(obj, info.compare_flow_type);

    if (info.compare_flow_type)
        info.project_nozzles = fold_per_nozzle_rows(slot_specs, info.row_slots, info.row_labels);
    else
        info.project_nozzles = fold_single_diameter_row(slot_specs, info.row_slots, info.row_labels);

    if (info.project_nozzles.empty()) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": the project declares no nozzle, nothing to compare";
        return std::nullopt;
    }

    info.nozzles_fully_covered = printer_covers_project(info.slot_nozzles, info.printer_nozzles, info.compare_flow_type);

    if (info.printer_nozzles.empty())
        info.section_kind = NozzleSectionKind::Unavailable;
    else if (info.nozzles_fully_covered)
        info.section_kind = NozzleSectionKind::None;
    else if (info.printer_nozzles.size() == 1)
        info.section_kind = NozzleSectionKind::Notice;
    else
        info.section_kind = NozzleSectionKind::Table;

    return info;
}

SyncPresetWithPrinterDialog::SyncPresetWithPrinterDialog(wxWindow *parent, PrinterMigrationInfo info)
    : DPIDialog(parent, wxID_ANY, _L("Sync printer information"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_info(std::move(info))
{
    SetBackgroundColour(C(ThemeColor::White));
    build_layout();
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void SyncPresetWithPrinterDialog::build_layout()
{
    const int content_width = FromDIP(kDialogWidth) - 2 * FromDIP(kSidePadding);

    auto *root = new wxBoxSizer(wxVERTICAL);
    root->AddSpacer(FromDIP(16));

    // Neutral wording on purpose: no 3MF is involved on the manual sync path, unlike the mock-up.
    auto *description = new Label(this, _L("The current printer preset does not match the connected printer. Please confirm the migration plan below."));
    description->SetFont(Label::Body_14);
    description->SetForegroundColour(C(ThemeColor::TextPrimary));
    description->Wrap(content_width);
    root->Add(description, 0, wxLEFT | wxRIGHT, FromDIP(kSidePadding));

    root->AddSpacer(FromDIP(12));
    root->Add(build_section(_L("Machine model"), [this](wxWindow *parent) { return build_model_table(parent); }), 0, wxEXPAND | wxLEFT | wxRIGHT,
              FromDIP(kSidePadding));

    // NozzleSectionKind::None draws nothing here: every slot keeps what it had.
    if (m_info.section_kind == NozzleSectionKind::Notice || m_info.section_kind == NozzleSectionKind::Unavailable) {
        root->AddSpacer(FromDIP(12));
        root->Add(build_nozzle_notice(this), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(kSidePadding));
    } else if (m_info.section_kind == NozzleSectionKind::Table) {
        root->AddSpacer(FromDIP(12));
        root->Add(build_section(_L("Nozzle type"), [this](wxWindow *parent) { return build_nozzle_table(parent); }), 0,
                  wxEXPAND | wxLEFT | wxRIGHT, FromDIP(kSidePadding));
    }

    root->AddSpacer(FromDIP(20));
    root->Add(build_footer(this), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(kSidePadding));
    root->AddSpacer(FromDIP(16));

    SetSizer(root);
    // Width follows the mock-up; height follows the content, since the row count is driven by the
    // project rather than fixed at the two rows the mock-up happens to show.
    SetMinSize(wxSize(FromDIP(kDialogWidth), -1));
    root->SetSizeHints(this);
    Layout();
}

wxWindow *SyncPresetWithPrinterDialog::build_section(const wxString &label, const TableFactory &make_table)
{
    auto *panel = new wxPanel(this);
    panel->SetBackgroundColour(GetBackgroundColour());

    auto *title = new Label(panel, label);
    title->SetFont(Label::Body_12);
    title->SetForegroundColour(C(ThemeColor::Grey450));

    auto *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(title, 0, wxBOTTOM, FromDIP(2));
    sizer->Add(make_table(panel), 0, wxEXPAND);
    panel->SetSizer(sizer);
    return panel;
}

wxWindow *SyncPresetWithPrinterDialog::make_model_cell(wxWindow *parent, const std::string &model_id, const wxString &model_name, const wxString &dev_name)
{
    auto *cell = new wxPanel(parent);
    cell->SetBackgroundColour(parent->GetBackgroundColour());

    auto *thumb = new wxStaticBitmap(cell, wxID_ANY, printer_thumbnail(model_id, cell));

    auto *text_sizer = new wxBoxSizer(wxVERTICAL);
    auto *name = new Label(cell, model_name);
    name->SetFont(Label::Head_13);
    name->SetForegroundColour(C(ThemeColor::Grey700));
    text_sizer->Add(name, 0, wxALIGN_LEFT);

    // Only the connected printer gets a second line, to tell two printers of the same model apart.
    if (!dev_name.IsEmpty()) {
        auto *sub = new Label(cell, "(" + dev_name + ")");
        sub->SetFont(Label::Body_13);
        sub->SetForegroundColour(C(ThemeColor::Grey700));
        text_sizer->Add(sub, 0, wxALIGN_LEFT);
    }

    auto *sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(thumb, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    sizer->Add(text_sizer, 1, wxALIGN_CENTER_VERTICAL);
    cell->SetSizer(sizer);
    return cell;
}

wxWindow *SyncPresetWithPrinterDialog::build_model_table(wxWindow *parent)
{
    wxPanel *rows_host = nullptr;
    wxPanel *frame = make_table_frame(parent, rows_host);

    auto *rows = new wxBoxSizer(wxVERTICAL);
    rows->Add(make_header_row(rows_host, _L("Current preset"), _L("Connected printer")), 0, wxEXPAND);

    auto *data_row = new wxPanel(rows_host);
    data_row->SetBackgroundColour(C(ThemeColor::White));
    auto *data_sizer = new wxBoxSizer(wxHORIZONTAL);
    data_sizer->Add(make_model_cell(data_row, m_info.source_model_id, m_info.source_model_name, wxEmptyString), 1,
                    wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(kCellPadding) / 2);
    data_sizer->Add(make_model_cell(data_row, m_info.target_model_id, m_info.target_model_name, m_info.target_dev_name), 1,
                    wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(kCellPadding) / 2);
    data_row->SetSizer(data_sizer);

    rows->Add(data_row, 0, wxEXPAND);
    rows_host->SetSizer(rows);
    return frame;
}

wxWindow *SyncPresetWithPrinterDialog::build_nozzle_table(wxWindow *parent)
{
    wxPanel *rows_host = nullptr;
    wxPanel *frame = make_table_frame(parent, rows_host);

    auto *rows = new wxBoxSizer(wxVERTICAL);
    rows->Add(make_header_row(rows_host, _L("Current preset"), _L("Connected printer")), 0, wxEXPAND);

    m_nozzle_combos.clear();
    m_nozzle_combos.reserve(m_info.project_nozzles.size());

    for (size_t i = 0; i < m_info.project_nozzles.size(); ++i) {
        const NozzleSpec &want = m_info.project_nozzles[i];

        auto *row = new wxPanel(rows_host);
        row->SetBackgroundColour(C(ThemeColor::White));

        auto *source = new Label(row, i < m_info.row_labels.size() ? m_info.row_labels[i]
                                                                   : format_nozzle_spec_label(want, m_info.compare_flow_type));
        source->SetFont(Label::Body_13);
        source->SetForegroundColour(C(ThemeColor::Grey700));

        auto *combo = new ComboBox(row, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, row->FromDIP(kComboHeight)), 0, nullptr, wxCB_READONLY);
        for (const NozzleSpec &candidate : m_info.printer_nozzles)
            combo->Append(format_nozzle_spec_label(candidate, m_info.compare_flow_type));
        const int matched = find_closest_nozzle(want, m_info.printer_nozzles, m_info.compare_flow_type);
        if (matched >= 0)
            combo->SetSelection(matched);
        combo->Enable(!m_info.printer_nozzles.empty());
        m_nozzle_combos.push_back(combo);

        auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);
        row_sizer->Add(source, 1, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(kCellPadding) / 2);
        row_sizer->Add(combo, 1, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(kCellPadding) / 2);
        row->SetSizer(row_sizer);

        rows->Add(row, 0, wxEXPAND);
        // Between rows only; the last row has no trailing border in the mock-up.
        if (i + 1 < m_info.project_nozzles.size()) {
            auto *divider = new wxPanel(rows_host, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
            divider->SetBackgroundColour(C(ThemeColor::Grey300));
            rows->Add(divider, 0, wxEXPAND);
        }
    }

    rows_host->SetSizer(rows);
    return frame;
}

wxWindow *SyncPresetWithPrinterDialog::build_nozzle_notice(wxWindow *parent)
{
    wxString text;
    if (m_info.section_kind == NozzleSectionKind::Unavailable) {
        text = _L("The connected printer has not reported its nozzle information, so the nozzles are left unchanged. "
                  "Only the printer preset will be switched.");
    } else {
        const wxString only = format_nozzle_spec_label(m_info.printer_nozzles.front(), m_info.compare_flow_type);
        text = wxString::Format(_L("The connected printer only has a %s nozzle, so every filament is mapped onto it."), only);
    }

    auto *label = new Label(parent, text);
    label->SetFont(Label::Body_13);
    label->SetForegroundColour(C(ThemeColor::Grey700));
    label->Wrap(FromDIP(kDialogWidth) - 2 * FromDIP(kSidePadding));
    return label;
}

wxWindow *SyncPresetWithPrinterDialog::build_footer(wxWindow *parent)
{
    auto *panel = new wxPanel(parent);
    panel->SetBackgroundColour(parent->GetBackgroundColour());

    auto *sizer = new wxBoxSizer(wxHORIZONTAL);

    if (m_info.offer_skip_future_checks) {
        m_skip_check = new CheckBox(panel);
        auto *label = new Label(panel, _L("Don't check again when loading 3MF"));
        label->SetFont(Label::Body_14);
        label->SetForegroundColour(C(ThemeColor::TextDisabled));
        // The bitmap button has no label of its own, so the text has to forward the click.
        label->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { m_skip_check->SetValue(!m_skip_check->GetValue()); });

        sizer->Add(m_skip_check, 0, wxALIGN_CENTER_VERTICAL);
        sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(9));
    }
    sizer->AddStretchSpacer();

    StateColor cancel_bg(std::pair<wxColour, int>(C(ThemeColor::Grey350), StateColor::Pressed), std::pair<wxColour, int>(C(ThemeColor::Grey300), StateColor::Hovered),
                         std::pair<wxColour, int>(C(ThemeColor::White), StateColor::Normal));
    StateColor cancel_border(std::pair<wxColour, int>(C(ThemeColor::Grey700), StateColor::Normal));
    StateColor cancel_text(std::pair<wxColour, int>(C(ThemeColor::Grey700), StateColor::Normal));

    auto *cancel_btn = new Button(panel, _L("Cancel"), "", 0, 0, wxID_ANY);
    cancel_btn->SetMinSize(wxSize(FromDIP(70), FromDIP(24)));
    cancel_btn->SetCornerRadius(FromDIP(12));
    cancel_btn->SetBackgroundColor(cancel_bg);
    cancel_btn->SetBorderColor(cancel_border);
    cancel_btn->SetTextColor(cancel_text);
    cancel_btn->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { EndModal(wxID_CANCEL); });

    StateColor confirm_bg(std::pair<wxColour, int>(C(ThemeColor::Grey500), StateColor::Disabled), std::pair<wxColour, int>(C(ThemeColor::BrandGreenPressed), StateColor::Pressed),
                          std::pair<wxColour, int>(C(ThemeColor::BrandGreenHovered), StateColor::Hovered), std::pair<wxColour, int>(C(ThemeColor::BrandGreen), StateColor::Normal));
    // Solid button: the border tracks the fill so no edge shows through.
    StateColor confirm_border(std::pair<wxColour, int>(C(ThemeColor::Grey500), StateColor::Disabled), std::pair<wxColour, int>(C(ThemeColor::BrandGreenPressed), StateColor::Pressed),
                              std::pair<wxColour, int>(C(ThemeColor::BrandGreenHovered), StateColor::Hovered), std::pair<wxColour, int>(C(ThemeColor::BrandGreen), StateColor::Normal));
    StateColor confirm_text(std::pair<wxColour, int>(C(ThemeColor::White), StateColor::Normal));

    m_confirm_btn = new Button(panel, _L("Confirm"), "", 0, 0, wxID_ANY);
    m_confirm_btn->SetMinSize(wxSize(FromDIP(70), FromDIP(24)));
    m_confirm_btn->SetCornerRadius(FromDIP(12));
    m_confirm_btn->SetBackgroundColor(confirm_bg);
    m_confirm_btn->SetBorderColor(confirm_border);
    m_confirm_btn->SetTextColor(confirm_text);
    m_confirm_btn->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { EndModal(wxID_OK); });
    // Unreachable in practice - an empty candidate list becomes Unavailable, not Table - but kept
    // so a table can never ship with nothing to confirm.
    const bool has_plan = m_info.section_kind != NozzleSectionKind::Table || !m_info.printer_nozzles.empty();
    m_confirm_btn->Enable(has_plan);
    if (has_plan)
        m_confirm_btn->SetFocus();

    sizer->Add(cancel_btn, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(m_confirm_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(16));

    panel->SetSizer(sizer);
    return panel;
}

std::vector<NozzleSpec> SyncPresetWithPrinterDialog::GetChosenNozzles() const
{
    std::vector<NozzleSpec> chosen;
    chosen.reserve(m_nozzle_combos.size());
    for (ComboBox *combo : m_nozzle_combos) {
        const int selected = combo == nullptr ? -1 : combo->GetSelection();
        if (selected >= 0 && selected < (int) m_info.printer_nozzles.size())
            chosen.push_back(m_info.printer_nozzles[selected]);
        else
            chosen.push_back(NozzleSpec{});
    }
    return chosen;
}

PendingNozzleMigration SyncPresetWithPrinterDialog::GetConfirmedMigration() const
{
    PendingNozzleMigration migration;
    migration.apply_flow_type = m_info.compare_flow_type;

    if (m_info.section_kind == NozzleSectionKind::None) {
        // Nothing was asked because the printer carries everything. Replay each slot's own spec so
        // slots that differ from each other stay that way.
        migration.per_slot = m_info.slot_nozzles;
        return migration;
    }

    // A slot the user was never asked about keeps whatever the printer preset switch left it with.
    migration.per_slot.assign(m_info.slot_nozzles.size(), NozzleSpec{});

    if (m_info.section_kind == NozzleSectionKind::Unavailable) {
        // Nothing to map onto; Confirm exists only to switch the preset.
        migration.apply_flow_type = false;
        return migration;
    }

    if (m_info.section_kind == NozzleSectionKind::Notice) {
        // The printer's only nozzle takes every slot that had one.
        for (size_t slot = 0; slot < m_info.slot_nozzles.size(); ++slot)
            if (m_info.slot_nozzles[slot].diameter > 0.f)
                migration.per_slot[slot] = m_info.printer_nozzles.front();
        return migration;
    }

    const std::vector<NozzleSpec> chosen = GetChosenNozzles();
    for (size_t row = 0; row < chosen.size() && row < m_info.row_slots.size(); ++row)
        for (size_t slot : m_info.row_slots[row])
            if (slot < migration.per_slot.size())
                migration.per_slot[slot] = chosen[row];
    return migration;
}

bool SyncPresetWithPrinterDialog::SkipFutureChecks() const { return m_skip_check != nullptr && m_skip_check->GetValue(); }

}} // namespace Slic3r::GUI
