#include "FilamentMapDialog.hpp"
#include "FilamentMapPanel.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/LinkLabel.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "CapsuleButton.hpp"
#include "MsgDialog.hpp"
#include "PartPlate.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Log.hpp"

#include <algorithm>
#include <wx/display.h>
#include <wx/scrolwin.h>

namespace Slic3r::GUI {

class SmartFilamentPanel : public wxPanel
{
    static constexpr auto spacing = 20;

public:
    SmartFilamentPanel(wxWindow *parent) : wxPanel(parent)
    {
        SetBackgroundColour(*wxWHITE);
        wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);

        // space
        main_sizer->AddSpacer(FromDIP(spacing));

        // separator
        auto *separator = new wxPanel(this);
        separator->SetBackgroundColour(wxColour("#EEEEEE"));
        main_sizer->Add(separator, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(15));

        // space
        main_sizer->AddSpacer(FromDIP(spacing));

        // smart filament section
        m_smart_filament_checkbox = new CheckBox(this);
        m_smart_filament_checkbox->SetValue(enable_filament_dynamic_map()->getBool());
        m_smart_filament_checkbox->Bind(wxEVT_TOGGLEBUTTON, &SmartFilamentPanel::on_smart_filament_checkbox, this);

        auto *label = new Label(this, _L("Enable smart filament assign: Assign one filament to multiple nozzles to maximize savings"));
        label->SetFont(Label::Body_12);

        auto *wiki_link = new LinkLabel(this, _L("Learn more"), "https://e.bambulab.com/t?c=rYwNe4U869Qa9kW1");
        wiki_link->getLabel()->SetFont(Label::Body_12);
        wiki_link->SeLinkLabelFColour(wxColour("#00AE42"));
        wiki_link->SeLinkLabelBColour(*wxWHITE);

        auto *smart_sizer = new wxBoxSizer(wxHORIZONTAL);
        smart_sizer->Add(m_smart_filament_checkbox, 0, wxALIGN_CENTER_VERTICAL);
        smart_sizer->Add(label, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(3));
        smart_sizer->Add(wiki_link, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(5));

        main_sizer->Add(smart_sizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(15));

        SetSizer(main_sizer);
        Layout();
        Fit();
        wxGetApp().UpdateDarkUIWin(this);
    }

private:
    static ConfigOptionBool *enable_filament_dynamic_map()
    {
        auto &config = wxGetApp().preset_bundle->project_config;
        return dynamic_cast<ConfigOptionBool *>(config.option("enable_filament_dynamic_map"));
    }

    void on_smart_filament_checkbox(wxCommandEvent &event)
    {
        enable_filament_dynamic_map()->value = m_smart_filament_checkbox->GetValue();
        wxGetApp().plater()->update();
        event.Skip();
    }

private:
    CheckBox *m_smart_filament_checkbox{};
};

static std::vector<FilamentMapMode> normalize_auto_modes(const std::vector<FilamentMapMode>& modes)
{
    std::vector<FilamentMapMode> result;
    for (auto mode : modes) {
        if (!is_auto_filament_map_mode(mode))
            continue;
        if (std::find(result.begin(), result.end(), mode) == result.end())
            result.push_back(mode);
    }
    return result;
}

static std::vector<FilamentMapMode> get_default_auto_modes()
{
    return { fmmAutoForFlush, fmmAutoForMatch, fmmAutoForQuality };
}

std::vector<FilamentMapMode> resolve_available_auto_modes(Print* print_obj, const std::vector<FilamentMapMode>& requested_modes, bool machine_synced)
{
    std::vector<FilamentMapMode> supported_modes;
    if (print_obj) {
        supported_modes = normalize_auto_modes(print_obj->get_available_filament_map_modes());
    } else {
        supported_modes.push_back(fmmAutoForFlush);
        supported_modes.push_back(fmmAutoForMatch);
        if (PartPlate::has_different_extruder_types())
            supported_modes.push_back(fmmAutoForQuality);
    }

    // remove match mode when filament swither is ready
    if (wxGetApp().sidebar().is_fila_switch_ready()) {
        supported_modes.erase(std::remove(supported_modes.begin(), supported_modes.end(), fmmAutoForMatch), supported_modes.end());
    }

    std::vector<FilamentMapMode> requested_auto = normalize_auto_modes(requested_modes);
    if (requested_auto.empty())
        requested_auto = get_default_auto_modes();

    std::vector<FilamentMapMode> result;
    for (auto mode : requested_auto) {
        if (std::find(supported_modes.begin(), supported_modes.end(), mode) != supported_modes.end())
            result.push_back(mode);
    }
    if (result.empty())
        result.push_back(fmmAutoForFlush);

    return result;
}

FilamentMapMode resolve_fallback_filament_map_mode(FilamentMapMode mode, const std::vector<FilamentMapMode>& available_modes, bool machine_synced)
{
    if (!is_auto_filament_map_mode(mode))
        return mode;
    const bool mode_available = std::find(available_modes.begin(), available_modes.end(), mode) != available_modes.end();
    if (mode_available && !(mode == fmmAutoForMatch && !machine_synced))
        return mode;
    return available_modes.empty() ? fmmAutoForFlush : available_modes.front();
}

bool build_multi_nozzle_manual_maps(const ConfigBase       &config,
                                    const std::vector<int> &dialog_maps,
                                    std::vector<int>       &out_filament_maps,
                                    std::vector<int>       &out_nozzle_maps)
{
    if (!is_single_ext_multi_nozzle(config))
        return false;

    auto ext_nozzle_cnt = config.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count");
    int total_nozzles = ext_nozzle_cnt->values.empty() ? 0 : ext_nozzle_cnt->values[0];
    int ams_idx = get_effective_ams_nozzle_idx_for_active_diameter(config);
    bool has_ams = (ams_idx >= 0);

    out_nozzle_maps.assign(dialog_maps.size(), 0);
    if (has_ams) {
        // 固定组取第一个非 AMS 喷嘴当代表：组内哪个都行，后端会挑兼容的具体喷嘴
        int fixed_group_nozzle_idx = 0;
        for (int nozzle = 0; nozzle < total_nozzles; ++nozzle) {
            if (nozzle != ams_idx) {
                fixed_group_nozzle_idx = nozzle;
                break;
            }
        }

        // 面板的 1 是 AMS 栏，2 是固定喷嘴栏
        for (size_t i = 0; i < dialog_maps.size(); ++i)
            out_nozzle_maps[i] = (dialog_maps[i] == 1) ? ams_idx : fixed_group_nozzle_idx;
    }

    // 这类机型只有一个真实挤出机，filament_map 恒为全 1，分组只存在 filament_nozzle_map 里
    out_filament_maps.assign(dialog_maps.size(), 1);
    return true;
}

static FilamentMapMode get_applied_map_mode(DynamicConfig& proj_config, const Plater* plater_ref, const PartPlate* partplate_ref, const bool sync_plate)
{
    if (sync_plate)
        return partplate_ref->get_real_filament_map_mode(proj_config);
    return plater_ref->get_global_filament_map_mode();
}

static std::vector<int> get_applied_map(DynamicConfig& proj_config, const Plater* plater_ref, const PartPlate* partplate_ref, const bool sync_plate)
{
    if (sync_plate)
        return partplate_ref->get_real_filament_maps(proj_config);
    return plater_ref->get_global_filament_map();
}

static std::vector<int> get_applied_volume_map(DynamicConfig& proj_config, const Plater* plater_ref, const PartPlate* partplate_ref, const bool sync_plate)
{
    if (sync_plate)
        return partplate_ref->get_real_filament_volume_maps(proj_config);
    return plater_ref->get_global_filament_volume_map();
}

extern std::string& get_left_extruder_unprintable_text();
extern std::string& get_right_extruder_unprintable_text();

// TPU 摆放错误时用户从错误通知点"重新分组"，改对后点切片本该直接切；但自定义模式默认每次切片
// 都弹摆放对话框。这里记下那次确认，只放行紧接着的那一次切片。
struct TpuRegroupConfirmation
{
    ObjectID plate_id;
    size_t slice_invalid_epoch {0}; // 确认那一刻盘的失效计数快照，不相等就说明中间发生过改动
};
static TpuRegroupConfirmation s_tpu_regroup_confirmation;

bool try_pop_up_before_slice(bool is_slice_all, Plater* plater_ref, PartPlate* partplate_ref, bool force_pop_up)
{
    auto full_config = wxGetApp().preset_bundle->full_config();

    // 取出上一次记下的确认并立刻清空，放在所有提前返回之前，避免残留到下一次
    const TpuRegroupConfirmation pending_confirmation = s_tpu_regroup_confirmation;
    s_tpu_regroup_confirmation = {};

    const auto nozzle_diameters = full_config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
    const auto extruder_max_nozzle_count = full_config.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count");
    bool support_multi_nozzle = extruder_max_nozzle_count
        && std::any_of(
            extruder_max_nozzle_count->values.begin(),
            extruder_max_nozzle_count->values.end(),
            [](int val) { return val > 1; });
    bool support_popup = (nozzle_diameters && nozzle_diameters->size() > 1) || support_multi_nozzle;
    if (!support_popup)
        return true;

    bool sync_plate = true;

    if (should_skip_filament_grouping(full_config)) {
        // In single-diameter mode identical nozzles reduce to a single-nozzle printer. Clear any
        // previously stored grouping choice before slicing; multi-diameter mode keeps its per-filament
        // nozzle selections even though the grouping UI is hidden.
        if (!multi_nozzle_diameter_mode_enabled(full_config)) {
            auto reset_mode = [&](PartPlate* plate) {
                if (plate && plate->get_real_filament_map_mode(wxGetApp().preset_bundle->project_config) != fmmAutoForFlush)
                    plate->set_filament_map_mode(fmmAutoForFlush);
            };
            if (is_slice_all) {
                for (PartPlate* plate : plater_ref->get_partplate_list().get_plate_list())
                    reset_mode(plate);
            } else {
                reset_mode(partplate_ref);
            }
        }
        return true;
    }

    std::vector<std::string> filament_colors = full_config.option<ConfigOptionStrings>("filament_colour")->values;
    std::vector<std::string> filament_types = full_config.option<ConfigOptionStrings>("filament_type")->values;
    FilamentMapMode applied_mode = get_applied_map_mode(full_config, plater_ref,partplate_ref, sync_plate);
    std::vector<int> applied_maps = get_applied_map(full_config, plater_ref, partplate_ref, sync_plate);
    std::vector<int> applied_volume_maps = get_applied_volume_map(full_config, plater_ref, partplate_ref, sync_plate);
    applied_maps.resize(filament_colors.size(), 1);
    applied_volume_maps.resize(filament_colors.size(), 0);

    // Single-extruder multi-nozzle machines keep the AMS/fixed split in filament_nozzle_map
    // (filament_map stays all-1). Rebuild the manual-panel display from it whenever available,
    // regardless of the current mode.
    {
        auto opt_hotend = full_config.option<ConfigOptionBool>("handle_hotend_as_extruder");
        int ams_idx = get_effective_ams_nozzle_idx_for_active_diameter(full_config);
        if (opt_hotend && opt_hotend->value && ams_idx >= 0) {
            auto plate_nozzle_maps = partplate_ref->get_filament_nozzle_maps();
            for (size_t i = 0; i < applied_maps.size() && i < plate_nozzle_maps.size(); ++i)
                applied_maps[i] = (plate_nozzle_maps[i] == ams_idx) ? 1 : 2;
        }
    }
    if (applied_mode == fmmNozzleManual)
        applied_mode = fmmManual;

    std::vector<FilamentMapMode> requested_modes = get_default_auto_modes();
    Print* print_obj = partplate_ref->fff_print();
    std::vector<FilamentMapMode> available_modes;
    if (is_single_ext_multi_nozzle(full_config)) {
        available_modes = get_single_ext_multi_nozzle_auto_modes(full_config);
    } else {
        available_modes = resolve_available_auto_modes(
            print_obj,
            requested_modes,
            plater_ref->get_machine_sync_status()
        );
    }

    // The hover popup only displays the fallback mode; it is written here, right before slicing, so a
    // slice never runs with a mode the UI considers unavailable and a valid result is never left behind
    // a config that changed under it.
    const FilamentMapMode fallback_mode = resolve_fallback_filament_map_mode(applied_mode, available_modes, plater_ref->get_machine_sync_status());
    if (fallback_mode != applied_mode) {
        BOOST_LOG_TRIVIAL(info) << KV(log_key::plate_id, partplate_ref->get_index()) << "filament map mode " << int(applied_mode)
                                << " is unavailable, fall back to " << int(fallback_mode) << ", slice_all " << is_slice_all;
        applied_mode = fallback_mode;
    }

    // 跳过这一次弹窗的条件：待放行的确认就是这张盘的，且确认之后这张盘没再发生过切片失效
    const bool tpu_regroup_confirmed = !is_slice_all
        && pending_confirmation.plate_id == partplate_ref->id()
        && pending_confirmation.slice_invalid_epoch == partplate_ref->get_slice_invalid_epoch();
    if (!force_pop_up && (applied_mode != fmmManual || tpu_regroup_confirmed)) {
        // Slice all uses one auto mode on every plate. Manual modes are left alone: applied_mode has
        // fmmNozzleManual folded into fmmManual, and writing that back would clear the plate maps.
        if (is_auto_filament_map_mode(applied_mode)) {
            const auto& proj_config = wxGetApp().preset_bundle->project_config;
            std::vector<PartPlate*> plates = is_slice_all ? plater_ref->get_partplate_list().get_plate_list()
                                                          : std::vector<PartPlate*>{partplate_ref};
            for (PartPlate* plate : plates)
                if (plate && plate->get_real_filament_map_mode(proj_config) != applied_mode)
                    plate->set_filament_map_mode(applied_mode);
        }
        return true;
    }

    std::vector<int> filament_lists;
    if (is_slice_all) {
        filament_lists.resize(filament_colors.size());
        std::iota(filament_lists.begin(), filament_lists.end(), 1);
    }
    else {
        filament_lists = partplate_ref->get_extruders();
    }

    FilamentMapDialog map_dlg(plater_ref,
        filament_types,
        applied_maps,
        applied_volume_maps,
        filament_lists,
        applied_mode,
        plater_ref->get_machine_sync_status(),
        false,
        false,
        available_modes
    );
    auto ret = map_dlg.ShowModal();

    if (ret == wxID_OK) {
        FilamentMapMode new_mode = map_dlg.get_mode();
        std::vector<int> new_maps = map_dlg.get_filament_maps();
        std::vector<int> new_volume_maps = map_dlg.get_filament_volume_maps();

        // Backend representation of new_maps, only filled on single-extruder multi-nozzle machines.
        std::vector<int> backend_filament_maps;
        std::vector<int> new_nozzle_maps;
        if (new_mode == fmmManual && build_multi_nozzle_manual_maps(full_config, new_maps, backend_filament_maps, new_nozzle_maps)) {
            auto apply_to_plate = [&](PartPlate* plate) {
                plate->set_filament_map_mode(new_mode);
                plate->set_filament_maps(backend_filament_maps);
                plate->set_filament_volume_maps(new_volume_maps);
                plate->set_filament_nozzle_maps(new_nozzle_maps);
            };

            if (sync_plate) {
                if (is_slice_all) {
                    for (auto* plate : plater_ref->get_partplate_list().get_plate_list())
                        apply_to_plate(plate);
                } else {
                    apply_to_plate(partplate_ref);
                }
            }
        } else {
            if (sync_plate) {
                if (is_slice_all) {
                    auto plate_list = plater_ref->get_partplate_list().get_plate_list();
                    for (int i = 0; i < plate_list.size(); ++i) {
                        plate_list[i]->set_filament_map_mode(new_mode);
                        if (new_mode == fmmManual) {
                            plate_list[i]->set_filament_maps(new_maps);
                            plate_list[i]->set_filament_volume_maps(new_volume_maps);
                        }
                    }
                }
                else {
                    partplate_ref->set_filament_map_mode(new_mode);
                    if (new_mode == fmmManual) {
                        partplate_ref->set_filament_maps(new_maps);
                        partplate_ref->set_filament_volume_maps(new_volume_maps);
                    }
                }
            }
            else {
                plater_ref->set_global_filament_map_mode(new_mode);
                if (new_mode == fmmManual) {
                    plater_ref->set_global_filament_map(new_maps);
                    plater_ref->set_global_filament_volume_map(new_volume_maps);
                }
            }
        }

        // 只收敛 TPU：单盘、自定义模式、单挤出机多喷嘴机型，且走的是强制弹窗入口
        //（错误通知里的"重新分组"；注意 Linux 下切片按钮也走这个入口）
        bool tpu_regroup_flow = force_pop_up && !is_slice_all && new_mode == fmmManual && is_single_ext_multi_nozzle(full_config);
        if (tpu_regroup_flow)
            tpu_regroup_flow = !wxGetApp().preset_bundle->get_used_tpu_filaments(partplate_ref->get_extruders(true)).empty();

        // update() 会先按 can_slice() 决定是否清掉 process_completed_with_error，而此刻
        // m_ready_for_slice 还是旧摆放留下的 false，不先刷新就会留下错误态、吞掉随后那次切片。
        if (tpu_regroup_flow) {
            bool model_fits = false, validate_error = false;
            plater_ref->validate_current_plate(model_fits, validate_error);
        }
        plater_ref->update(false, true);
        if (!get_left_extruder_unprintable_text().empty() || !get_right_extruder_unprintable_text().empty()){
            return false;
        }

        // 没放对就不记确认，下次照常弹窗；这两项和 validate_current_plate 判 model_fits 时一致，
        // 保证"放行"和"错误通知消失"同一口径。失效计数必须在 update() 之后取，否则会漏掉本次改动引起的那次失效。
        if (tpu_regroup_flow) {
            wxString printable_error;
            if (partplate_ref->check_filament_printable(full_config, printable_error) && !partplate_ref->check_missing_tpu285_nozzle(full_config))
                s_tpu_regroup_confirmation = {partplate_ref->id(), partplate_ref->get_slice_invalid_epoch()};
        }
        return true;
    }
    return false;
}


StateColor btn_bg_green(
    std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled),
    std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
    std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
    std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal)
);

static const StateColor btn_bd_green(std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

static const StateColor btn_text_green(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal));

static const StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
                                     std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                                     std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));

static const StateColor btn_bd_white(std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Normal));

static const StateColor btn_text_white(std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Normal));

FilamentMapDialog::FilamentMapDialog(wxWindow                           *parent,
                                     const std::vector<std::string>     &filament_type,
                                     const std::vector<int>             &filament_map,
                                     const std::vector<int>             &filament_volume_map,
                                     const std::vector<int>             &filaments,
                                     const FilamentMapMode               mode,
                                     bool                                machine_synced,
                                     bool                                show_default,
                                     bool                                with_checkbox,
                                     const std::vector<FilamentMapMode> &available_modes)
    : wxDialog(parent, wxID_ANY, _L("Filament grouping"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_filament_map(filament_map)
    , m_filament_volume_map(filament_volume_map)
    , m_filament_type(filament_type)
{
    SetBackgroundColour(*wxWHITE);

    std::vector<FilamentMapMode> modes_to_use = normalize_auto_modes(available_modes);
    if (modes_to_use.empty()) modes_to_use = get_default_auto_modes();

    m_fila_switch_ready = wxGetApp().sidebar().is_fila_switch_ready();
    auto       full_config          = wxGetApp().preset_bundle->full_config();
    const bool is_multi_nozzle_mode = is_single_ext_multi_nozzle(full_config);
    const bool single_default_mode  = false;
    const bool only_convenience_mode = modes_to_use.size() == 1 && modes_to_use.front() == fmmAutoForMatch;
    const bool show_manual_mode = can_customize_filament_grouping(full_config);
    const bool only_saving_mode = !only_convenience_mode && m_fila_switch_ready &&
        (std::find(modes_to_use.cbegin(), modes_to_use.cend(), FilamentMapMode::fmmAutoForQuality) == modes_to_use.cend());
    m_auto_mode_available = !(only_convenience_mode && !machine_synced);

    if ((is_auto_filament_map_mode(mode) && m_auto_mode_available) || !show_manual_mode)
        m_page_type = PageType::ptAuto;
    else
        m_page_type = PageType::ptManual;

    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
    make_header(main_sizer, only_saving_mode, single_default_mode, show_manual_mode);
    make_body(main_sizer, only_saving_mode, single_default_mode, is_multi_nozzle_mode, show_manual_mode,
              modes_to_use, filaments, mode, machine_synced);
    make_footer(main_sizer, mode);

    SetSizer(main_sizer);

    const bool initial_manual = m_page_type == PageType::ptManual;
    if (m_manual_panel)
        m_manual_panel->Show(initial_manual);
    if (m_manual_scroll)
        m_manual_scroll->Show(initial_manual);
    m_auto_panel->Show(!initial_manual);

    // we dont want width changes when switching modes
    auto width = std::max({GetBestSize().GetWidth(), FromDIP(580)});
    SetMinClientSize({width, -1});

    Fit();
    constrain_dialog_height();
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void FilamentMapDialog::make_header(wxBoxSizer *main_sizer, bool only_saving_mode, bool single_default_mode,
                                    bool show_manual_mode)
{
    wxBoxSizer *mode_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxString auto_label = single_default_mode ? _L("Default") : (only_saving_mode ? _L("Fila Saving") : _L("Auto"));
    m_auto_btn = new CapsuleButton(this, PageType::ptAuto, auto_label, false);

    m_auto_btn->Bind(wxEVT_BUTTON, &FilamentMapDialog::on_switch_mode, this);

    const int button_padding = FromDIP(2);
    mode_sizer->AddStretchSpacer();
    mode_sizer->Add(m_auto_btn, 1, wxALIGN_CENTER | wxLEFT | wxRIGHT, button_padding);
    if (show_manual_mode) {
        m_manual_btn = new CapsuleButton(this, PageType::ptManual, _L("Custom"), false);
        m_manual_btn->Bind(wxEVT_BUTTON, &FilamentMapDialog::on_switch_mode, this);
        mode_sizer->Add(m_manual_btn, 1, wxALIGN_CENTER | wxLEFT | wxRIGHT, button_padding);
    }
    mode_sizer->AddStretchSpacer();

    main_sizer->AddSpacer(FromDIP(22));
    main_sizer->Add(mode_sizer, 0, wxEXPAND);
    main_sizer->AddSpacer(FromDIP(24));
}

void FilamentMapDialog::make_body(
    wxBoxSizer *sizer, bool only_saving_mode, bool single_default_mode, bool is_multi_nozzle_mode,
    bool show_manual_mode, const std::vector<FilamentMapMode> &modes_to_use,
    const std::vector<int> &filaments, const FilamentMapMode mode, bool machine_synced)
{
    FilamentMapMode default_auto_mode = is_auto_filament_map_mode(mode) ? mode : fmmAutoForFlush;
    if (!machine_synced && default_auto_mode == fmmAutoForMatch)
        default_auto_mode = fmmAutoForFlush;
    if (std::find(modes_to_use.begin(), modes_to_use.end(), default_auto_mode) == modes_to_use.end())
        default_auto_mode = modes_to_use.front();

    if (show_manual_mode && is_multi_nozzle_mode) {
        m_manual_scroll = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_NONE);
        m_manual_scroll->SetBackgroundColour(*wxWHITE);
        m_manual_scroll->SetScrollRate(0, FromDIP(10));
        auto *scroll_sizer = new wxBoxSizer(wxVERTICAL);
        m_manual_panel = new FilamentMapMultiNozzlePanel(
            m_manual_scroll, m_filament_type, filaments, m_filament_map, m_filament_volume_map);
        scroll_sizer->Add(m_manual_panel, 1, wxEXPAND);
        m_manual_scroll->SetSizer(scroll_sizer);
        m_manual_scroll->FitInside();
        m_manual_scroll->SetMinSize({
            FromDIP(580),
            m_manual_panel->GetBestSize().GetHeight()
        });
        SetTitle(_L("Set filament placement"));
    } else if (show_manual_mode) {
        m_manual_panel = new FilamentMapManualPanel(this, m_filament_type, filaments, m_filament_map, m_filament_volume_map);
    }
    if (m_manual_panel) {
        m_manual_panel->Bind(wxEVT_INVALID_MANUAL_MAP, [this](wxCommandEvent &event) {
            // The panel also validates while the dialog is being built, before the footer exists.
            if (m_ok_btn == nullptr)
                return;
            if (m_page_type != PageType::ptManual) {
                if (!m_ok_btn->IsEnabled()) { m_ok_btn->Enable(); }
                return;
            }
            if (event.GetInt()) {
                if (!m_ok_btn->IsEnabled()) { m_ok_btn->Enable(); }
            } else {
                if (m_ok_btn->IsEnabled()) { m_ok_btn->Disable(); }
            }
        });
    }

    const bool only_convenience_mode = modes_to_use.size() == 1 && modes_to_use.front() == fmmAutoForMatch;
    if (single_default_mode)
        m_auto_panel = new FilamentMapSavingPanel(this, _L("Automatically and intelligently assigns filaments to minimize filament time consumption"));
    else if (only_convenience_mode)
        m_auto_panel = new FilamentMapSavingPanel(this, _L("Intelligently assigns filaments based on the filaments currently loaded in the printer's nozzles"), fmmAutoForMatch);
    else if (only_saving_mode)
        m_auto_panel = new FilamentMapSavingPanel(this);
    else
        m_auto_panel = new FilamentMapAutoPanel(this, default_auto_mode, machine_synced, modes_to_use);

    auto flags = wxSizerFlags().CenterHorizontal();
    sizer->Add(m_auto_panel, flags);
    if (m_manual_scroll)
        sizer->Add(m_manual_scroll, 0, wxEXPAND);
    else if (m_manual_panel)
        sizer->Add(m_manual_panel, flags);
}

void FilamentMapDialog::make_footer(wxBoxSizer *main_sizer, const FilamentMapMode mode)
{
    if (m_fila_switch_ready) {
        smart_filament = new SmartFilamentPanel(this);
        smart_filament->Show(mode == fmmAutoForFlush);
        main_sizer->Add(smart_filament, wxSizerFlags().Expand());
    }

    wxPanel *bottom_panel = new wxPanel(this);
    bottom_panel->SetBackgroundColour(*wxWHITE);
    wxBoxSizer *bottom_sizer = new wxBoxSizer(wxHORIZONTAL);
    bottom_panel->SetSizer(bottom_sizer);
    bottom_sizer->Fit(bottom_panel);

    bottom_sizer->AddStretchSpacer();

    {
        wxBoxSizer *button_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_ok_btn                 = new Button(bottom_panel, _L("OK"));
        m_cancel_btn             = new Button(bottom_panel, _L("Cancel"));
        m_ok_btn->SetCornerRadius(FromDIP(12));
        m_cancel_btn->SetCornerRadius(FromDIP(12));
        m_ok_btn->SetFont(Label::Body_12);
        m_cancel_btn->SetFont(Label::Body_12);

        m_ok_btn->SetBackgroundColor(btn_bg_green);
        m_ok_btn->SetTextColor(btn_text_green);
        m_cancel_btn->SetBackgroundColor(btn_bg_white);
        m_cancel_btn->SetBorderColor(btn_bd_white);
        m_cancel_btn->SetTextColor(btn_text_white);

        button_sizer->Add(m_ok_btn, 1, wxRIGHT, FromDIP(4));
        button_sizer->Add(m_cancel_btn, 1, wxLEFT, FromDIP(4));

        bottom_sizer->Add(button_sizer, 0, wxALIGN_CENTER | wxALL, FromDIP(15));
    }
    main_sizer->Add(bottom_panel, 0, wxEXPAND);

    m_ok_btn->Bind(wxEVT_BUTTON, &FilamentMapDialog::on_ok, this);
    m_cancel_btn->Bind(wxEVT_BUTTON, &FilamentMapDialog::on_cancle, this);
}

FilamentMapMode FilamentMapDialog::get_mode()
{
    if (m_page_type == PageType::ptAuto) return m_auto_panel->GetMode();
    return fmmManual;
}

int FilamentMapDialog::ShowModal()
{
    update_panel_status(m_page_type);
    return wxDialog::ShowModal();
}

void FilamentMapDialog::on_ok(wxCommandEvent &event)
{
    if (m_page_type == PageType::ptManual && m_manual_panel) {
        if (auto multi_nozzle = dynamic_cast<FilamentMapMultiNozzlePanel *>(m_manual_panel)) {
            m_filament_map        = multi_nozzle->GetFilamentMaps();
            m_filament_volume_map = multi_nozzle->GetFilamentVolumeMaps();
        } else if (auto manual = dynamic_cast<FilamentMapManualPanel *>(m_manual_panel)) {
            m_filament_map        = manual->GetFilamentMaps();
            m_filament_volume_map = manual->GetFilamentVolumeMaps();
        }
    }

    EndModal(wxID_OK);
}

void FilamentMapDialog::on_cancle(wxCommandEvent &event) { EndModal(wxID_CANCEL); }

void FilamentMapDialog::update_panel_status(PageType page)
{
    const bool is_manual = (page == PageType::ptManual);

    if (m_manual_btn)
        m_manual_btn->Select(is_manual);
    m_auto_btn->Select(!is_manual);

    // Before showing the manual panel: showing it validates the grouping at once, and that result must decide OK.
    if (!is_manual)
        m_ok_btn->Enable(m_auto_mode_available);
    else if (!m_ok_btn->IsEnabled())
        m_ok_btn->Enable();

    if (m_manual_panel)
        m_manual_panel->Show(is_manual);
    if (m_manual_scroll)
        m_manual_scroll->Show(is_manual);
    m_auto_panel->Show(!is_manual);

    if (smart_filament) smart_filament->Show(get_mode() == fmmAutoForFlush);

    Layout();
    Fit();
    constrain_dialog_height();
}

void FilamentMapDialog::constrain_dialog_height()
{
    if (!m_manual_scroll || !m_manual_scroll->IsShown())
        return;

    m_manual_scroll->FitInside();
    const int display_index = wxDisplay::GetFromWindow(this);
    const wxRect display_area = display_index == wxNOT_FOUND
        ? wxGetClientDisplayRect()
        : wxDisplay(static_cast<unsigned int>(display_index)).GetClientArea();
    const int max_dialog_height = std::min(FromDIP(780), std::max(FromDIP(300), display_area.GetHeight() - FromDIP(40)));
    const int overflow = GetSize().GetHeight() - max_dialog_height;
    if (overflow <= 0)
        return;

    const int content_height = m_manual_panel->GetBestSize().GetHeight();
    const int viewport_height = std::max(FromDIP(120), content_height - overflow);
    const int viewport_width = FromDIP(580);
    m_manual_scroll->SetMinSize({viewport_width, viewport_height});
    m_manual_scroll->SetMaxSize({-1, viewport_height});
    m_manual_scroll->FitInside();
    Layout();
    Fit();

    const int remaining_overflow = GetSize().GetHeight() - max_dialog_height;
    if (remaining_overflow > 0 && viewport_height > FromDIP(120)) {
        const int adjusted_height = std::max(FromDIP(120), viewport_height - remaining_overflow);
        m_manual_scroll->SetMinSize({viewport_width, adjusted_height});
        m_manual_scroll->SetMaxSize({-1, adjusted_height});
        Layout();
        Fit();
    }
}

void FilamentMapDialog::on_switch_mode(wxCommandEvent &event)
{
    int win_id  = event.GetId();
    m_page_type = PageType(win_id);

    update_panel_status(m_page_type);
    event.Skip();
}
} // namespace Slic3r::GUI
