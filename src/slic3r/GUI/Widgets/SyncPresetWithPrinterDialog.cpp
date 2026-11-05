#include "SyncPresetWithPrinterDialog.hpp"

#include "Button.hpp"
#include "CheckBox.hpp"
#include "ComboBox.hpp"
#include "Label.hpp"
#include "StateColor.hpp"

#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/LocalesUtils.hpp"
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
#include <boost/format.hpp>

#include <wx/sizer.h>
#include <wx/statbmp.h>
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

namespace {

// `C(...)` re-maps a light-mode color to its dark-mode pair at draw time
inline wxColour C(const wxColour &light) { return StateColor::darkModeColorFor(light); }

constexpr int kDialogWidth = 554;
constexpr int kSidePadding = 30;
constexpr int kCellPadding = 12;
constexpr int kRowHeight = 31;
constexpr int kThumbSize = 36;
constexpr int kNameLineHeight = 20;
constexpr int kComboHeight = 24;
// TextInput, and so ComboBox, paints its icon this many physical pixels in from its own left edge.
constexpr int kComboIconInset = 5;

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
// An unreported flow type is dropped, not defaulted, while it takes part in the comparison:
// ToNozzleVolumeType() would map NONE_FLOWTYPE to Standard with just a warning, while the rest of
// the codebase treats it as a hard error (SelectMachine.cpp reports PrintStatusNozzleDataInvalid),
// so it must not become a target. A diameter-only comparison never looks at the flow type, so the
// nozzle still counts there and no volume is derived from the missing report.
std::vector<NozzleSpec> collect_printer_nozzles(MachineObject *obj, bool with_flow_type)
{
    std::vector<NozzleSpec> specs;
    if (obj == nullptr || obj->GetNozzleSystem() == nullptr)
        return specs;

    auto *nozzle_system = obj->GetNozzleSystem();
    for (const std::map<int, DevNozzle> *nozzles : {&nozzle_system->GetRackNozzles(), &nozzle_system->GetExtNozzles()}) {
        for (const auto &[_, nozzle] : *nozzles) {
            // An unknown nozzle has no reliable diameter, so it cannot be offered as a target.
            if (nozzle.IsEmpty() || nozzle.IsUnknown() || nozzle.IsAbnormal() || !nozzle.IsHotend())
                continue;
            const NozzleFlowType flow = nozzle.GetNozzleFlowType();
            if (with_flow_type && flow == NozzleFlowType::NONE_FLOWTYPE)
                continue;
            if (nozzle.GetNozzleDiameter() <= 0.f)
                continue;
            const NozzleVolumeType volume = with_flow_type ? DevNozzle::ToNozzleVolumeType(flow) : nvtStandard;
            push_unique(specs, NozzleSpec{nozzle.GetNozzleDiameter(), volume}, with_flow_type);
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

// The nozzle each filament slot prints with. Single-diameter projects share the printer preset's
// variant; multi-diameter ones use the per-slot map. Flow type is only filled when it takes part.
std::vector<SlotSpec> collect_project_slot_specs(const PresetBundle &preset_bundle, bool multi_diameter_mode, bool with_flow_type)
{
    const size_t slot_count = preset_bundle.filament_presets.size();

    if (!multi_diameter_mode) {
        const float preset_diameter = (float) string_to_double_decimal_point(
            preset_bundle.printers.get_selected_preset().config.opt_string("printer_variant").c_str());
        if (preset_diameter <= 0.f)
            return {};

        std::vector<SlotSpec> specs;
        specs.reserve(slot_count);
        for (size_t i = 0; i < slot_count; ++i)
            specs.push_back(SlotSpec{i, NozzleSpec{preset_diameter}});
        return specs;
    }

    const DynamicPrintConfig &project_config = preset_bundle.project_config;
    const auto *diameter_map = project_config.option<ConfigOptionStrings>("filament_diameter_map");
    const auto *nozzle_diams = preset_bundle.printers.get_selected_preset()
                                   .config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
    const float preset_diameter = nozzle_diams != nullptr && !nozzle_diams->values.empty()
        ? (float) nozzle_diams->values.front() : 0.f;

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
    specs.reserve(slot_count);
    for (size_t i = 0; i < slot_count; ++i) {
        NozzleSpec spec;
        if (diameter_map != nullptr && i < diameter_map->values.size() && !diameter_map->values[i].empty())
            spec.diameter = (float) string_to_double_decimal_point(diameter_map->values[i].c_str());
        if (spec.diameter <= 0.f) {
            const std::vector<std::string> options = preset_bundle.get_filament_nozzle_diameter_options(i);
            if (!options.empty())
                spec.diameter = (float) string_to_double_decimal_point(options.front().c_str());
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

// Dedup slot specs into table rows. Key is the formatted diameter, plus volume only when the table
// shows flow type.
//
// A row the printer already carries is left out: there is nothing to decide about it, so the table
// only lists what the machine cannot serve as is. Several diameters in a diameter-only comparison are
// the exception - they still have to land on one diameter, so the single choice behind the shared
// cell applies to all of them, and each stays even when the printer happens to carry it.
std::vector<NozzleSpec> dedupe_project_rows(const std::vector<SlotSpec> &slot_specs,
                                            const std::vector<NozzleSpec> &printer_nozzles,
                                            bool with_flow_type,
                                            std::vector<std::vector<size_t>> &row_slots,
                                            std::vector<wxString> &row_labels)
{
    row_slots.clear();
    row_labels.clear();

    struct Group
    {
        NozzleSpec spec;
        std::vector<size_t> slots;
    };
    std::map<std::pair<std::string, int>, Group> grouped;
    for (const SlotSpec &entry : slot_specs) {
        const auto key = std::make_pair(format_diameter_to_str(entry.spec.diameter),
                                        with_flow_type ? (int) entry.spec.volume : 0);
        Group &group = grouped[key];
        if (group.slots.empty())
            group.spec = entry.spec;
        group.slots.push_back(entry.slot);
    }

    const bool keep_served = !with_flow_type && grouped.size() > 1;

    std::vector<NozzleSpec> rows;
    for (auto &entry : grouped) {
        Group &group = entry.second;
        const bool already_served = !keep_served &&
                                    std::any_of(printer_nozzles.begin(), printer_nozzles.end(),
                                                [&](const NozzleSpec &candidate) { return same_spec(candidate, group.spec, with_flow_type); });
        if (already_served)
            continue;
        rows.push_back(group.spec);
        row_slots.push_back(std::move(group.slots));
        row_labels.push_back(format_nozzle_spec_label(group.spec, with_flow_type));
    }
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
    sizer->SetMinSize(-1, row->FromDIP(kRowHeight));
    for (const wxString &text : {left, right}) {
        auto *label = new Label(row, text);
        label->SetFont(Label::Head_13);
        label->SetForegroundColour(C(ThemeColor::TextPrimary));
        sizer->Add(label, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, row->FromDIP(kCellPadding));
    }
    row->SetSizer(sizer);
    return row;
}

// Right-hand cell of a nozzle row. A dropdown starts kComboIconInset early so its arrow lines up
// with the header text above it.
wxSizer *make_target_cell(wxWindow *target, bool is_combo)
{
    auto *cell = new wxBoxSizer(wxHORIZONTAL);
    cell->AddSpacer(target->FromDIP(kCellPadding) - (is_combo ? kComboIconInset : 0));
    cell->Add(target, 1, wxALIGN_CENTER_VERTICAL);
    cell->AddSpacer(target->FromDIP(kCellPadding));
    return cell;
}

} // namespace

wxString format_nozzle_spec_label(const NozzleSpec &spec, bool with_flow_type)
{
    const wxString diameter = wxString::FromUTF8(get_diameter_string(spec.diameter));
    if (!with_flow_type)
        return diameter + " mm";
    const wxString volume = DevNozzle::GetNozzleVolumeTypeStr(spec.volume);
    return volume.IsEmpty() ? diameter : diameter + " " + volume;
}

int find_closest_nozzle(const NozzleSpec &want, const std::vector<NozzleSpec> &candidates, bool with_flow_type)
{
    int best = -1;
    // Closest diameter first, preferring the finer nozzle on a tie.
    std::tuple<float, float, int> best_key;

    for (size_t i = 0; i < candidates.size(); ++i) {
        const NozzleSpec &candidate = candidates[i];
        const int flow = with_flow_type ? volume_rank(want.volume, candidate.volume) : 0;
        const std::tuple<float, float, int> key{std::abs(candidate.diameter - want.diameter), candidate.diameter, flow};
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
    info.model_mismatch = info.source_model_id != info.target_model_id;

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

    const size_t slot_count = preset_bundle->filament_presets.size();
    const std::vector<SlotSpec> slot_specs = collect_project_slot_specs(*preset_bundle, multi_diameter_mode, info.compare_flow_type);
    // Checked before the rows are folded: those drop what the printer already carries, so an empty
    // row list is a normal outcome rather than a project without nozzles.
    if (slot_specs.empty()) {
        BOOST_LOG_TRIVIAL(info) << "the project declares no nozzle, nothing to compare";
        return std::nullopt;
    }

    info.slot_nozzles.assign(slot_count, NozzleSpec{});
    for (const SlotSpec &entry : slot_specs)
        if (entry.slot < info.slot_nozzles.size())
            info.slot_nozzles[entry.slot] = entry.spec;

    // Collected first because the rows below leave out whatever the printer can already serve.
    info.printer_nozzles = collect_printer_nozzles(obj, info.compare_flow_type);
    info.project_nozzles = dedupe_project_rows(slot_specs, info.printer_nozzles, info.compare_flow_type, info.row_slots, info.row_labels);

    // Without a rack the machine prints one diameter per plate, so a project asking for several can
    // never be served as it stands - no matter that the extruders happen to carry each one. Counting
    // them separately would call it covered, hide the section, and hand several diameters to a
    // printer preset that can only express one.
    std::set<std::string> project_diameters;
    for (const SlotSpec &entry : slot_specs)
        project_diameters.insert(format_diameter_to_str(entry.spec.diameter));
    const bool needs_more_diameters_than_target_can_hold = !target_has_rack && project_diameters.size() > 1;

    info.nozzles_fully_covered = !needs_more_diameters_than_target_can_hold &&
                                 printer_covers_project(info.slot_nozzles, info.printer_nozzles, info.compare_flow_type);

    if (info.printer_nozzles.empty())
        info.section_kind = NozzleSectionKind::Unavailable;
    else if (info.nozzles_fully_covered)
        info.section_kind = NozzleSectionKind::None;
    else if (info.printer_nozzles.size() == 1)
        info.section_kind = NozzleSectionKind::Notice;
    else
        info.section_kind = NozzleSectionKind::Table;

    info.shared_choice = info.section_kind == NozzleSectionKind::Notice ||
                         (info.section_kind == NozzleSectionKind::Table && !info.compare_flow_type && info.project_nozzles.size() > 1);

    return info;
}

SyncPresetWithPrinterDialog::SyncPresetWithPrinterDialog(wxWindow *parent, PrinterMigrationInfo info)
    : DPIDialog(parent, wxID_ANY, _L("Sync Printer Information"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_info(std::move(info))
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(C(ThemeColor::White));
    build_layout();
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void SyncPresetWithPrinterDialog::build_layout()
{
    const int content_width = FromDIP(kDialogWidth) - 2 * FromDIP(kSidePadding);

    auto *root = new wxBoxSizer(wxVERTICAL);
    root->AddSpacer(FromDIP(25));

    auto *description = new Label(this, _L("The printer model and settings in this 3MF file don't match the connected printer. Do you want to adapt these slicing settings for this printer?"));
    description->SetFont(Label::Body_14);
    description->SetForegroundColour(C(ThemeColor::TextPrimary));
    description->Wrap(content_width);
    root->Add(description, 0, wxLEFT | wxRIGHT, FromDIP(kSidePadding));

    int gap = FromDIP(9);
    auto add_block = [&](wxWindow *block) {
        root->AddSpacer(gap);
        root->Add(block, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(kSidePadding));
        gap = FromDIP(12);
    };

    // Both sides on the same model means the preset stays where it is; a table repeating it twice
    // carries nothing.
    if (m_info.model_mismatch)
        add_block(build_section(_L("Printer model comparison"), [this](wxWindow *parent) { return build_model_table(parent); }));

    // NozzleSectionKind::None and Unavailable draw nothing here: every slot keeps what it had.
    if (m_info.section_kind == NozzleSectionKind::Notice || m_info.section_kind == NozzleSectionKind::Table)
        add_block(build_section(_L("Nozzle comparison"), [this](wxWindow *parent) { return build_nozzle_table(parent); }));

    root->AddSpacer(FromDIP(23));
    auto *migration_note = new Label(this, _L("Note: After adapting the settings, some print parameters will be automatically adjusted for the new printer model."));
    migration_note->SetFont(Label::Body_14);
    migration_note->SetForegroundColour(C(ThemeColor::TextPrimary));
    migration_note->Wrap(content_width);
    root->Add(migration_note, 0, wxLEFT | wxRIGHT, FromDIP(kSidePadding));

    m_detail_loss_hint = build_detail_loss_hint(this);
    root->Add(m_detail_loss_hint, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(kSidePadding));
    update_detail_loss_hint(false);
    root->AddSpacer(FromDIP(m_info.offer_skip_future_checks ? 12 : 20));
    root->Add(build_footer(this), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(kSidePadding));
    root->AddSpacer(FromDIP(23));

    SetSizer(root);
    // Width follows the mock-up; height follows the content, since the row count is driven by the
    // project rather than fixed at the two rows the mock-up happens to show. The width is pinned on the
    // sizer, not the dialog: SetSizeHints() replaces the dialog's min size with the sizer's own.
    root->SetMinSize(wxSize(FromDIP(kDialogWidth), -1));
    root->SetSizeHints(this);
    Layout();
}

wxWindow *SyncPresetWithPrinterDialog::build_section(const wxString &label, const TableFactory &make_table)
{
    auto *panel = new wxPanel(this);
    panel->SetBackgroundColour(GetBackgroundColour());

    auto *title = new Label(panel, label);
    title->SetFont(Label::Body_12);
    title->SetForegroundColour(C(ThemeColor::Grey700));

    auto *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(title, 0, wxTOP | wxBOTTOM, FromDIP(3));
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
    name->SetForegroundColour(C(ThemeColor::TextPrimary));
    text_sizer->Add(name, 0, wxALIGN_LEFT);

    // Only the connected printer gets a second line, to tell two printers of the same model apart.
    if (!dev_name.IsEmpty()) {
        auto *sub = new Label(cell, "(" + dev_name + ")");
        sub->SetFont(Label::Body_13);
        sub->SetForegroundColour(C(ThemeColor::TextPrimary));
        // A wx label is only as tall as its glyphs, so the gap makes up the rest of the line height.
        text_sizer->AddSpacer(std::max(0, FromDIP(kNameLineHeight) - name->GetBestSize().GetHeight()));
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
    rows->Add(make_header_row(rows_host, _L("3MF File Settings"), _L("Connected Printer")), 0, wxEXPAND);

    auto *data_row = new wxPanel(rows_host);
    data_row->SetBackgroundColour(C(ThemeColor::White));
    auto *data_sizer = new wxBoxSizer(wxHORIZONTAL);
    data_sizer->Add(make_model_cell(data_row, m_info.source_model_id, m_info.source_model_name, wxEmptyString), 1,
                    wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(kCellPadding));
    data_sizer->Add(make_model_cell(data_row, m_info.target_model_id, m_info.target_model_name, m_info.target_dev_name), 1,
                    wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(kCellPadding));
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
    rows->Add(make_header_row(rows_host, _L("3MF File Settings"), _L("Connected Printer")), 0, wxEXPAND);

    m_nozzle_combos.clear();

    if (m_info.shared_choice) {
        rows->Add(build_shared_nozzle_rows(rows_host), 0, wxEXPAND);
        rows_host->SetSizer(rows);
        return frame;
    }

    m_nozzle_combos.reserve(m_info.project_nozzles.size());

    for (size_t i = 0; i < m_info.project_nozzles.size(); ++i) {
        auto *row = new wxPanel(rows_host);
        row->SetBackgroundColour(C(ThemeColor::White));

        auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);
        row_sizer->SetMinSize(-1, FromDIP(kRowHeight));
        row_sizer->Add(make_source_label(row, i), 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(kCellPadding));
        auto *column_divider = new wxPanel(row, wxID_ANY, wxDefaultPosition, wxSize(1, -1));
        column_divider->SetBackgroundColour(C(ThemeColor::Grey300));
        row_sizer->Add(column_divider, 0, wxEXPAND);
        row_sizer->Add(make_target_cell(make_nozzle_combo(row, m_info.project_nozzles[i]), true), 1, wxEXPAND);
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

// Every project nozzle on its own row at the left, one cell spanning all of them at the right:
// either the printer's only nozzle as plain text, or a single dropdown whose choice applies to every
// row. The row dividers stop at the left column so the right cell reads as merged.
wxWindow *SyncPresetWithPrinterDialog::build_shared_nozzle_rows(wxWindow *parent)
{
    auto *body = new wxPanel(parent);
    body->SetBackgroundColour(C(ThemeColor::White));

    auto *left = new wxBoxSizer(wxVERTICAL);
    for (size_t i = 0; i < m_info.project_nozzles.size(); ++i) {
        auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);
        row_sizer->SetMinSize(-1, FromDIP(kRowHeight));
        row_sizer->Add(make_source_label(body, i), 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(kCellPadding));
        left->Add(row_sizer, 0, wxEXPAND);
        if (i + 1 < m_info.project_nozzles.size()) {
            auto *row_divider = new wxPanel(body, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
            row_divider->SetBackgroundColour(C(ThemeColor::Grey300));
            left->Add(row_divider, 0, wxEXPAND);
        }
    }

    auto *divider = new wxPanel(body, wxID_ANY, wxDefaultPosition, wxSize(1, -1));
    divider->SetBackgroundColour(C(ThemeColor::Grey300));

    wxWindow *target = nullptr;
    const bool has_dropdown = m_info.section_kind != NozzleSectionKind::Notice;
    if (!has_dropdown) {
        // Nothing to pick, so no dropdown either.
        const wxString only = format_nozzle_spec_label(m_info.printer_nozzles.front(), m_info.compare_flow_type);
        auto *label = new Label(body, wxString::Format(_L("%s (only supported)"), only));
        label->SetFont(Label::Body_13);
        label->SetForegroundColour(C(ThemeColor::TextPrimary));
        target = label;
    } else {
        target = make_nozzle_combo(body, m_info.project_nozzles.empty() ? NozzleSpec{} : m_info.project_nozzles.front());
    }

    auto *sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(left, 1, wxEXPAND);
    sizer->Add(divider, 0, wxEXPAND);
    sizer->Add(make_target_cell(target, has_dropdown), 1, wxEXPAND);
    body->SetSizer(sizer);
    return body;
}

wxWindow *SyncPresetWithPrinterDialog::make_source_label(wxWindow *parent, size_t row) const
{
    auto *source = new Label(parent, row < m_info.row_labels.size() ? m_info.row_labels[row]
                                                                    : format_nozzle_spec_label(m_info.project_nozzles[row], m_info.compare_flow_type));
    source->SetFont(Label::Body_13);
    source->SetForegroundColour(C(ThemeColor::TextPrimary));
    return source;
}

wxWindow *SyncPresetWithPrinterDialog::make_nozzle_combo(wxWindow *parent, const NozzleSpec &want)
{
    auto *combo = new ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, parent->FromDIP(kComboHeight)), 0, nullptr, wxCB_READONLY);
    combo->SetBorderWidth(0);
    combo->SetFont(Label::Body_13);
    combo->SetLabelColor(StateColor(std::pair<wxColour, int>(C(ThemeColor::TextDisabled), StateColor::Disabled),
                                    std::pair<wxColour, int>(C(ThemeColor::TextPrimary), StateColor::Normal)));
    for (const NozzleSpec &candidate : m_info.printer_nozzles)
        combo->Append(format_nozzle_spec_label(candidate, m_info.compare_flow_type));
    const int matched = find_closest_nozzle(want, m_info.printer_nozzles, m_info.compare_flow_type);
    if (matched >= 0)
        combo->SetSelection(matched);
    combo->Enable(!m_info.printer_nozzles.empty());
    combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &evt) {
        evt.Skip();
        update_detail_loss_hint(true);
    });
    m_nozzle_combos.push_back(combo);
    return combo;
}

wxWindow *SyncPresetWithPrinterDialog::build_detail_loss_hint(wxWindow *parent)
{
    auto *panel = new wxPanel(parent);
    panel->SetBackgroundColour(parent->GetBackgroundColour());

    auto *icon = new wxStaticBitmap(panel, wxID_ANY, create_scaled_bitmap("warning", panel, 16));
    auto *label = new Label(panel, _L("Using a larger nozzle after adapting the settings may reduce fine details in your model. Please proceed with caution."));
    label->SetFont(Label::Body_14);
    label->SetForegroundColour(C(ThemeColor::Warning));
    label->Wrap(FromDIP(kDialogWidth) - 2 * FromDIP(kSidePadding) - FromDIP(16 + 6));

    auto *row = new wxBoxSizer(wxHORIZONTAL);
    // Centred on the first text line rather than the whole label, so a wrapped hint keeps it beside line one.
    row->Add(icon, 0, wxTOP, std::max(0, (label->GetCharHeight() - icon->GetBestSize().GetHeight()) / 2));
    row->AddSpacer(FromDIP(6));
    row->Add(label, 1);

    // The gap to the note above belongs to the hint, so it goes away with it.
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(row, 0, wxEXPAND | wxTOP, FromDIP(12));
    panel->SetSizer(sizer);
    return panel;
}

void SyncPresetWithPrinterDialog::update_detail_loss_hint(bool refit)
{
    const PendingNozzleMigration migration = GetConfirmedMigration();
    bool loses_detail = false;
    for (size_t slot = 0; !loses_detail && slot < migration.per_slot.size() && slot < m_info.slot_nozzles.size(); ++slot)
        loses_detail = nozzle_loses_detail(m_info.slot_nozzles[slot].diameter, migration.per_slot[slot].diameter);

    const bool changed = m_detail_loss_hint->IsShown() != loses_detail;
    m_detail_loss_hint->Show(loses_detail);
    if (!refit || !changed) return;

    // Height only: the width stays pinned on the sizer (see build_layout), and SetSizeHints() replaces the
    // min height too, so the dialog shrinks back once the hint hides.
    GetSizer()->SetSizeHints(this);
    Layout();
}

wxWindow *SyncPresetWithPrinterDialog::build_footer(wxWindow *parent)
{
    auto *panel = new wxPanel(parent);
    panel->SetBackgroundColour(parent->GetBackgroundColour());

    auto *sizer = new wxBoxSizer(wxVERTICAL);

    if (m_info.offer_skip_future_checks) {
        auto *check_row = new wxBoxSizer(wxHORIZONTAL);
        m_skip_check = new CheckBox(panel);
        auto *label = new Label(panel, _L("Don't show this again when opening 3MF files"));
        label->SetFont(Label::Body_14);
        label->SetForegroundColour(C(ThemeColor::Grey700));
        // The bitmap button has no label of its own, so the text has to forward the click.
        label->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { m_skip_check->SetValue(!m_skip_check->GetValue()); });

        check_row->Add(m_skip_check, 0, wxALIGN_CENTER_VERTICAL);
        check_row->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(9));
        sizer->Add(check_row, 0, wxEXPAND);
        sizer->AddSpacer(FromDIP(20));
    }

    StateColor cancel_bg(std::pair<wxColour, int>(C(ThemeColor::Grey350), StateColor::Pressed), std::pair<wxColour, int>(C(ThemeColor::Grey300), StateColor::Hovered),
                         std::pair<wxColour, int>(C(ThemeColor::White), StateColor::Normal));
    StateColor cancel_border(std::pair<wxColour, int>(C(ThemeColor::Grey700), StateColor::Normal));
    StateColor cancel_text(std::pair<wxColour, int>(C(ThemeColor::Grey700), StateColor::Normal));

    auto *cancel_btn = new Button(panel, _L("Cancel"), "", 0, 0, wxID_ANY);
    cancel_btn->SetFont(Label::Body_12);
    cancel_btn->SetPaddingSize(wxSize(FromDIP(12), FromDIP(6)));
    cancel_btn->SetMinSize(wxSize(-1, FromDIP(24)));
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
    StateColor confirm_text(std::pair<wxColour, int>(ThemeColor::WhiteFixed, StateColor::Normal));

    m_confirm_btn = new Button(panel, _L("Adapt Settings"), "", 0, 0, wxID_ANY);
    m_confirm_btn->SetFont(Label::Body_12);
    m_confirm_btn->SetPaddingSize(wxSize(FromDIP(12), FromDIP(6)));
    m_confirm_btn->SetMinSize(wxSize(-1, FromDIP(24)));
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

    auto *button_row = new wxBoxSizer(wxHORIZONTAL);
    button_row->AddStretchSpacer();
    button_row->Add(m_confirm_btn, 0, wxALIGN_CENTER_VERTICAL);
    button_row->Add(cancel_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(16));
    sizer->Add(button_row, 0, wxEXPAND);

    panel->SetSizer(sizer);
    return panel;
}

std::vector<NozzleSpec> SyncPresetWithPrinterDialog::GetChosenNozzles() const
{
    auto chosen_of = [this](const ComboBox *combo) {
        const int selected = combo == nullptr ? -1 : combo->GetSelection();
        return selected >= 0 && selected < (int) m_info.printer_nozzles.size() ? m_info.printer_nozzles[selected] : NozzleSpec{};
    };

    if (m_info.shared_choice) {
        if (m_nozzle_combos.empty())
            return {};
        return std::vector<NozzleSpec>(m_info.project_nozzles.size(), chosen_of(m_nozzle_combos.front()));
    }

    std::vector<NozzleSpec> chosen;
    chosen.reserve(m_nozzle_combos.size());
    for (ComboBox *combo : m_nozzle_combos)
        chosen.push_back(chosen_of(combo));
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

    // Slots behind a row the table left out are already served by the printer, so replay their own
    // spec instead of dropping them: the diameter gate downstream counts every slot, not just the
    // ones the user was asked about.
    migration.per_slot = m_info.slot_nozzles;

    const std::vector<NozzleSpec> chosen = GetChosenNozzles();
    for (size_t row = 0; row < chosen.size() && row < m_info.row_slots.size(); ++row)
        for (size_t slot : m_info.row_slots[row])
            if (slot < migration.per_slot.size())
                migration.per_slot[slot] = chosen[row];
    return migration;
}

bool SyncPresetWithPrinterDialog::SkipFutureChecks() const { return m_skip_check != nullptr && m_skip_check->GetValue(); }

}} // namespace Slic3r::GUI
