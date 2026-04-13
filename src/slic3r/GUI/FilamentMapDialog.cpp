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

#include <algorithm>

namespace Slic3r { namespace GUI {

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

static bool get_pop_up_remind_flag()
{
    auto &app_config = wxGetApp().app_config;
    return app_config->get_bool("pop_up_filament_map_dialog");
}

static void set_pop_up_remind_flag(bool remind)
{
    auto &app_config = wxGetApp().app_config;
    app_config->set_bool("pop_up_filament_map_dialog", remind);
}

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


bool try_pop_up_before_slice(bool is_slice_all, Plater* plater_ref, PartPlate* partplate_ref, bool force_pop_up)
{
    auto full_config = wxGetApp().preset_bundle->full_config();
    const auto nozzle_diameters = full_config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
    if (nozzle_diameters->size() <= 1)
        return true;

    bool sync_plate = true;

    std::vector<std::string> filament_colors = full_config.option<ConfigOptionStrings>("filament_colour")->values;
    std::vector<std::string> filament_types = full_config.option<ConfigOptionStrings>("filament_type")->values;
    FilamentMapMode applied_mode = get_applied_map_mode(full_config, plater_ref,partplate_ref, sync_plate);
    std::vector<int> applied_maps = get_applied_map(full_config, plater_ref, partplate_ref, sync_plate);
    std::vector<int> applied_volume_maps = get_applied_volume_map(full_config, plater_ref, partplate_ref, sync_plate);
    applied_maps.resize(filament_colors.size(), 1);
    applied_volume_maps.resize(filament_colors.size(), 0);

    if (!force_pop_up && applied_mode != fmmManual)
        return true;

    std::vector<int> filament_lists;
    if (is_slice_all) {
        filament_lists.resize(filament_colors.size());
        std::iota(filament_lists.begin(), filament_lists.end(), 1);
    }
    else {
        filament_lists = partplate_ref->get_extruders();
    }

    std::vector<FilamentMapMode> requested_modes = get_default_auto_modes();
    Print* print_obj = partplate_ref->fff_print();
    std::vector<FilamentMapMode> available_modes = resolve_available_auto_modes(
        print_obj,
        requested_modes,
        plater_ref->get_machine_sync_status()
    );

    FilamentMapDialog map_dlg(plater_ref,
        filament_colors,
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
        plater_ref->update(false, true);
        // check whether able to slice, if not, return false
        if (!get_left_extruder_unprintable_text().empty() || !get_right_extruder_unprintable_text().empty()){
            return false;
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

FilamentMapDialog::FilamentMapDialog(wxWindow                       *parent,
                                     const std::vector<std::string> &filament_color,
                                     const std::vector<std::string> &filament_type,
                                     const std::vector<int>         &filament_map,
                                     const std::vector<int>         &filament_volume_map,
                                     const std::vector<int>         &filaments,
                                     const FilamentMapMode           mode,
                                     bool                            machine_synced,
                                     bool                            show_default,
                                     bool                            with_checkbox,
                                     const std::vector<FilamentMapMode>& available_modes)
    : wxDialog(parent, wxID_ANY, _L("Filament grouping"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_filament_color(filament_color)
    , m_filament_type(filament_type)
    , m_filament_map(filament_map)
    , m_filament_volume_map(filament_volume_map)
{
    SetBackgroundColour(*wxWHITE);

    SetMinSize(wxSize(FromDIP(580), -1));

    m_fila_switch_ready = wxGetApp().sidebar().is_fila_switch_ready();

    if (is_auto_filament_map_mode(mode))
        m_page_type = PageType::ptAuto;
    else if (mode == fmmManual)
        m_page_type = PageType::ptManual;
    else
        m_page_type = PageType::ptDefault;

    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->AddSpacer(FromDIP(22));

    wxBoxSizer *mode_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxString auto_btn_label = m_fila_switch_ready ? _L("Fila Saving") : _L("Auto");
    m_auto_btn   = new CapsuleButton(this, PageType::ptAuto, auto_btn_label, false);
    m_manual_btn = new CapsuleButton(this, PageType::ptManual, _L("Custom"), false);
    if (show_default)
        m_default_btn = new CapsuleButton(this, PageType::ptDefault, _L("Same as Global"), true);
    else
        m_default_btn = nullptr;

    const int button_padding = FromDIP(2);
    mode_sizer->AddStretchSpacer();
    mode_sizer->Add(m_auto_btn, 1, wxALIGN_CENTER | wxLEFT | wxRIGHT, button_padding);
    mode_sizer->Add(m_manual_btn, 1, wxALIGN_CENTER | wxLEFT | wxRIGHT, button_padding);
    if (show_default) mode_sizer->Add(m_default_btn, 1, wxALIGN_CENTER | wxLEFT | wxRIGHT, button_padding);
    mode_sizer->AddStretchSpacer();

    main_sizer->Add(mode_sizer, 0, wxEXPAND);
    main_sizer->AddSpacer(FromDIP(24));

    auto            panel_sizer       = new wxBoxSizer(wxHORIZONTAL);

    std::vector<FilamentMapMode> modes_to_use = normalize_auto_modes(available_modes);
    if (modes_to_use.empty())
        modes_to_use = get_default_auto_modes();

    FilamentMapMode default_auto_mode = is_auto_filament_map_mode(mode) ? mode : fmmAutoForFlush;
    if (!machine_synced && default_auto_mode == fmmAutoForMatch)
        default_auto_mode = fmmAutoForFlush;
    if (std::find(modes_to_use.begin(), modes_to_use.end(), default_auto_mode) == modes_to_use.end())
        default_auto_mode = modes_to_use.front();

    m_manual_map_panel                = new FilamentMapManualPanel(this, m_filament_color, m_filament_type, filaments, filament_map, filament_volume_map);
    m_auto_map_panel                          = new FilamentMapAutoPanel(this, default_auto_mode, machine_synced, modes_to_use);
    m_saving_panel                            = m_fila_switch_ready ? new FilamentMapSavingPanel(this) : nullptr;
    if (show_default)
        m_default_map_panel = new FilamentMapDefaultPanel(this);
    else
        m_default_map_panel = nullptr;

    panel_sizer->Add(m_manual_map_panel, 0, wxALIGN_CENTER | wxEXPAND);
    panel_sizer->Add(m_auto_map_panel, 1, wxALIGN_CENTER | wxEXPAND);
    if (m_saving_panel)
        panel_sizer->Add(m_saving_panel, 1, wxALIGN_CENTER | wxEXPAND);
    if (show_default) panel_sizer->Add(m_default_map_panel, 0, wxALIGN_CENTER | wxEXPAND);
    main_sizer->Add(panel_sizer, 1, wxEXPAND);

    wxPanel* bottom_panel = new wxPanel(this);
    bottom_panel->SetBackgroundColour(*wxWHITE);
    wxBoxSizer *bottom_sizer = new wxBoxSizer(wxHORIZONTAL);
    bottom_panel->SetSizer(bottom_sizer);
    bottom_sizer->Fit(bottom_panel);

    if (m_fila_switch_ready) {
        smart_filament = new SmartFilamentPanel(this);
        smart_filament->Show(mode == fmmAutoForFlush);
        main_sizer->Add(smart_filament);
    }

    if (with_checkbox) {
        auto* checkbox_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_checkbox = new CheckBox(bottom_panel);
        m_checkbox->Bind(wxEVT_TOGGLEBUTTON, &FilamentMapDialog::on_checkbox, this);
        checkbox_sizer->Add(m_checkbox, 0, wxALIGN_CENTER, 0);

        auto* checkbox_label = new Label(bottom_panel, _L("Don't remind me again"));
        checkbox_label->SetFont(Label::Body_12);
        checkbox_sizer->Add(checkbox_label, 0, wxLEFT| wxALIGN_CENTER , FromDIP(3));

        bottom_sizer->Add(checkbox_sizer, 0 ,  wxALIGN_CENTER | wxALL, FromDIP(15));
    }

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

    m_auto_btn->Bind(wxEVT_BUTTON, &FilamentMapDialog::on_switch_mode, this);
    m_manual_btn->Bind(wxEVT_BUTTON, &FilamentMapDialog::on_switch_mode, this);
    if (show_default) m_default_btn->Bind(wxEVT_BUTTON, &FilamentMapDialog::on_switch_mode, this);

    m_manual_map_panel->Bind(wxEVT_INVALID_MANUAL_MAP, [this](wxCommandEvent& event) {
        if (m_page_type != PageType::ptManual) {
            if (!m_ok_btn->IsEnabled()) {
                m_ok_btn->Enable();
            }
            return;
        }
        if (event.GetInt()) {
            if (!m_ok_btn->IsEnabled()) {
                m_ok_btn->Enable();
            }
        }
        else {
            if (m_ok_btn->IsEnabled()) {
                m_ok_btn->Disable();
            }
        }
        });

    SetSizer(main_sizer);
    Layout();
    {
        const int target_w = std::max(FromDIP(580), m_manual_map_panel->GetMinWidth());
        int       best_h   = std::max({m_auto_map_panel->GetBestSize().GetHeight(), m_manual_map_panel->GetBestSize().GetHeight()});
        if (m_saving_panel) best_h = std::max(m_saving_panel->GetBestSize().GetHeight(), best_h);

        int current_panel_h = m_manual_map_panel->GetSize().GetHeight();
        if (m_page_type == PageType::ptAuto) {
            if (m_fila_switch_ready && m_saving_panel)
                current_panel_h = m_saving_panel->GetBestSize().GetHeight();
            else if (m_auto_map_panel)
                current_panel_h = m_auto_map_panel->GetBestSize().GetHeight();
        }

        int top_bottom_extra = GetBestSize().GetHeight() - current_panel_h;
        int target_h         = top_bottom_extra + best_h;

        SetMinSize(wxSize(target_w, target_h));
        SetMaxSize(wxSize(target_w, target_h));
        SetSize(wxSize(target_w, target_h));
    }

    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

FilamentMapMode FilamentMapDialog::get_mode()
{
    if (m_page_type == PageType::ptAuto) {
        if (m_fila_switch_ready)
            return fmmAutoForFlush;
        return m_auto_map_panel->GetMode();
    }
    if (m_page_type == PageType::ptManual) return fmmManual;
    return fmmDefault;
}

int FilamentMapDialog::ShowModal()
{
    update_panel_status(m_page_type);
    return wxDialog::ShowModal();
}

void FilamentMapDialog::on_checkbox(wxCommandEvent &event)
{
    bool is_checked = m_checkbox->GetValue();
    m_checkbox->SetValue(is_checked);
    set_pop_up_remind_flag(!is_checked);

    if (is_checked) {
        MessageDialog dialog(nullptr, _L("No further pop-up will appear. You can reopen it in 'Preferences'"), _L("Tips"), wxICON_INFORMATION | wxOK);
        dialog.ShowModal();
        this->Close();
    }

    event.Skip();
}

void FilamentMapDialog::on_ok(wxCommandEvent &event)
{
    if (m_page_type == PageType::ptManual) {
        m_filament_map = m_manual_map_panel->GetFilamentMaps();
        m_filament_volume_map = m_manual_map_panel->GetFilamentVolumeMaps();
    }

    EndModal(wxID_OK);
}

void FilamentMapDialog::on_cancle(wxCommandEvent &event) { EndModal(wxID_CANCEL); }

void FilamentMapDialog::update_panel_status(PageType page)
{
    std::vector<CapsuleButton*>button_list = { m_default_btn,m_manual_btn,m_auto_btn };
    for (auto p : button_list) {
        if (p && p->IsSelected()) {
            p->Select(false);
        }
    }
    std::vector<wxPanel*>panel_list = { m_default_map_panel,m_manual_map_panel,m_auto_map_panel,m_saving_panel };
    for (auto p : panel_list) {
        if (p && p->IsShown()) {
            p->Hide();
        }
    }

    if (page == PageType::ptDefault) {
        if (m_default_btn && m_default_map_panel) {
            m_default_btn->Select(true);
            m_default_map_panel->Show();
        }
    }
    if (page == PageType::ptManual) {
        m_manual_btn->Select(true);
        m_manual_map_panel->Show();
    }
    if (page == PageType::ptAuto) {
        m_auto_btn->Select(true);
        if (m_fila_switch_ready && m_saving_panel) {
            m_saving_panel->Show();
        } else if (m_auto_map_panel) {
            m_auto_map_panel->Show();
        }
        if (!m_ok_btn->IsEnabled()) {
            m_ok_btn->Enable();
        }
    }

    if (smart_filament) smart_filament->Show(get_mode() == fmmAutoForFlush);

    Layout();
}

void FilamentMapDialog::on_switch_mode(wxCommandEvent &event)
{
    int win_id  = event.GetId();
    m_page_type = PageType(win_id);

    update_panel_status(m_page_type);
    event.Skip();
}

void FilamentMapDialog::set_modal_btn_labels(const wxString &ok_label, const wxString &cancel_label)
{
    m_ok_btn->SetLabel(ok_label);
    m_cancel_btn->SetLabel(cancel_label);
}

}} // namespace Slic3r::GUI
