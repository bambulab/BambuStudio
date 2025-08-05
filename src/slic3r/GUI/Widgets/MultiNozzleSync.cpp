#include "MultiNozzleSync.hpp"
#include "../GUI_App.hpp"

namespace Slic3r{

static const int MaxNozzleCount = 6;

ManualNozzleCountDialog::ManualNozzleCountDialog(wxWindow* parent,int default_count)
    :GUI::DPIDialog(parent, wxID_ANY, "Set nozzle count", wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    this->SetBackgroundColour(*wxWHITE);
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    wxPanel* content = new wxPanel(this);
    content->SetBackgroundColour(*wxWHITE);

    wxBitmap icon = create_scaled_bitmap("hotend_thumbnail",nullptr,FromDIP(60));
    auto* nozzle_icon = new wxStaticBitmap(content, wxID_ANY, icon);

    wxBoxSizer* content_sizer = new wxBoxSizer(wxHORIZONTAL);
    content->SetSizer(content_sizer);

    wxBoxSizer* choice_sizer = new wxBoxSizer(wxVERTICAL);

    auto* label = new wxStaticText(content, wxID_ANY, "Please set nozzle count");
    choice_sizer->Add(label, 0, wxALL | wxALIGN_LEFT, FromDIP(20));

    wxArrayString nozzle_choices;
    for (int i = 1; i <= MaxNozzleCount; ++i)
        nozzle_choices.Add(wxString::Format("%d", i));

    m_choice = new wxChoice(content, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(100), -1), nozzle_choices);
    m_choice->SetSelection(default_count - 1);
    m_choice->SetBackgroundColour(*wxWHITE);
    choice_sizer->Add(m_choice, 0, wxLEFT | wxBOTTOM, FromDIP(20));

    content_sizer->Add(nozzle_icon, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(15));
    content_sizer->Add(choice_sizer, 0, wxALIGN_CENTRE_VERTICAL);

    auto bt_enable = StateColor(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

    m_confirm_btn = new Button(this, _L("Confirm"));
    m_confirm_btn->SetBackgroundColor(bt_enable);
    m_confirm_btn->SetBorderColor(bt_enable);
    m_confirm_btn->SetTextColor(StateColor::darkModeColorFor("#FFFFFE"));
    m_confirm_btn->SetMinSize(wxSize(FromDIP(68), FromDIP(23)));
    m_confirm_btn->SetMinSize(wxSize(FromDIP(68), FromDIP(23)));
    m_confirm_btn->SetCornerRadius(12);
    m_confirm_btn->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {EndModal(wxID_OK);});


    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    mainSizer->Add(content, 1, wxEXPAND);
    mainSizer->Add(m_confirm_btn, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, FromDIP(20));

    SetSizerAndFit(mainSizer);
    CentreOnParent();
}

int ManualNozzleCountDialog::GetNozzleCount() const
{
    return m_choice->GetSelection() + 1;
}

static std::vector<MultiNozzleUtils::NozzleGroupInfo> loadFullNozzleCountFromAppConf()
{
    std::string extruder_nozzle_str = GUI::wxGetApp().app_config->get("presets", "extruder_nozzle_count");
    std::vector<std::string> extruder_nozzle_count;
    boost::algorithm::split(extruder_nozzle_count, extruder_nozzle_str, boost::is_any_of(","));
    std::vector<MultiNozzleUtils::NozzleGroupInfo> nozzle_group;
    for (auto& str : extruder_nozzle_count) {
        auto nozzle_info = MultiNozzleUtils::NozzleGroupInfo::deserialize(str);
        if (nozzle_info)
            nozzle_group.emplace_back(*nozzle_info);
    }

    return nozzle_group;
}

void setNozzleCountToAppConf(int extruder_id,const std::vector<MultiNozzleUtils::NozzleGroupInfo>& nozzle_info)
{
    for (auto& info : nozzle_info) {
        if (info.extruder_id != extruder_id)
            throw "set nozzle count with different extruder id";
    }

    std::vector<MultiNozzleUtils::NozzleGroupInfo> full_nozzle_info = nozzle_info;

    auto exist_nozzles = loadFullNozzleCountFromAppConf();
    for (auto& nozzle : exist_nozzles) {
        if (nozzle.extruder_id != extruder_id)
            full_nozzle_info.emplace_back(nozzle);
    }

    std::vector<std::string> nozzle_count_str;

    for (auto& info : full_nozzle_info) {
        if (info.extruder_id != extruder_id)
            throw "set nozzle count with different extruder id";
        nozzle_count_str.emplace_back(info.serialize());
    }

    std::string extruder_nozzle_str = boost::algorithm::join(nozzle_count_str, ",");
    GUI::wxGetApp().app_config->set("presets", "extruder_nozzle_count", extruder_nozzle_str);
}

int getNozzleCountFromAppConf(const DynamicConfig& config, int extruder_id, double nozzle_diameter, NozzleVolumeType type)
{
    auto has_multiple_nozzle = config.option<ConfigOptionBoolsNullable>("has_multiple_nozzle");
    if (!has_multiple_nozzle)
        return 1;

    if (extruder_id >= has_multiple_nozzle->values.size() || !has_multiple_nozzle->values[extruder_id])
        return 1;

    auto nozzle_info = loadFullNozzleCountFromAppConf();
    std::optional<MultiNozzleUtils::NozzleGroupInfo> target_nozzle;
    for (size_t idx = 0; idx < nozzle_info.size(); ++idx) {
        if (std::fabs(nozzle_diameter - nozzle_info[idx].diameter) < EPSILON &&
            extruder_id == nozzle_info[idx].extruder_id &&
            type == nozzle_info[idx].volume_type) {
            target_nozzle = nozzle_info[idx];
        }
    }
    if (target_nozzle)
        return target_nozzle->nozzle_count;

    return MaxNozzleCount;
}

void manuallySetNozzleCount(int extruder_id)
{
    auto& preset_bundle = GUI::wxGetApp().preset_bundle;
    if (!preset_bundle)
        return;

    auto full_config = GUI::wxGetApp().preset_bundle->full_config();
    ConfigOptionBoolsNullable* has_multiple_nozzle = full_config.option<ConfigOptionBoolsNullable>("has_multiple_nozzle");
    if (!has_multiple_nozzle || extruder_id >= has_multiple_nozzle->size() || !has_multiple_nozzle->values[extruder_id])
        return;

    ConfigOptionEnumsGeneric* nozzle_volume_type_opt = full_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type");
    NozzleVolumeType volume_type = NozzleVolumeType(nozzle_volume_type_opt->values[extruder_id]);
    double nozzle_diameter = full_config.option<ConfigOptionFloatsNullable>("nozzle_diameter")->values[extruder_id];
    int default_nozzle_count = getNozzleCountFromAppConf(full_config, extruder_id, nozzle_diameter, volume_type);
    ManualNozzleCountDialog dialog(GUI::wxGetApp().plater(), default_nozzle_count);

    if (dialog.ShowModal() == wxID_OK) {
        int nozzle_count = dialog.GetNozzleCount();
        MultiNozzleUtils::NozzleGroupInfo info(nozzle_diameter, volume_type, extruder_id, nozzle_count);
        setNozzleCountToAppConf(extruder_id, { info });// write to app config

        // write to preset bundle config
        if (extruder_id >= preset_bundle->extruder_nozzle_counts.size())
            preset_bundle->extruder_nozzle_counts.resize(extruder_id + 1);
        preset_bundle->extruder_nozzle_counts[extruder_id][volume_type] = nozzle_count;
    }
}


}