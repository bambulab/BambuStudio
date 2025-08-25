#include "MultiNozzleSync.hpp"
#include "../GUI_App.hpp"
#include "../DeviceCore/DevManager.h"
#include "libslic3r/PresetBundle.hpp"
#include <wx/sizer.h>


namespace Slic3r::GUI{

ManualNozzleCountDialog::ManualNozzleCountDialog(wxWindow* parent,int default_count, int max_nozzle_count)
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
    for (int i = 1; i <= max_nozzle_count; ++i)
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

void manuallySetNozzleCount(int extruder_id)
{
    auto& preset_bundle = GUI::wxGetApp().preset_bundle;
    if (!preset_bundle)
        return;

    auto full_config = GUI::wxGetApp().preset_bundle->full_config();
    auto extruder_max_nozzle_count = full_config.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count");
    if (!extruder_max_nozzle_count || extruder_id >= extruder_max_nozzle_count->size() || extruder_max_nozzle_count->values[extruder_id] <= 1)
        return;

    ConfigOptionEnumsGeneric* nozzle_volume_type_opt = full_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type");
    NozzleVolumeType volume_type = NozzleVolumeType(nozzle_volume_type_opt->values[extruder_id]);
    double nozzle_diameter = full_config.option<ConfigOptionFloatsNullable>("nozzle_diameter")->values[extruder_id];
    auto nozzle_count_map = get_extruder_nozzle_count(full_config.option<ConfigOptionStrings>("extruder_nozzle_count")->values);
    int default_nozzle_count = 1;
    if (extruder_id < nozzle_count_map.size() && nozzle_count_map[extruder_id].find(volume_type) != nozzle_count_map[extruder_id].end())
        default_nozzle_count = nozzle_count_map[extruder_id][volume_type];
    else
        default_nozzle_count = extruder_max_nozzle_count->values[extruder_id];

    ManualNozzleCountDialog dialog(GUI::wxGetApp().plater(), default_nozzle_count, extruder_max_nozzle_count->values[extruder_id]);

    if (dialog.ShowModal() == wxID_OK) {
        int nozzle_count = dialog.GetNozzleCount();
        MultiNozzleUtils::NozzleGroupInfo info(std::to_string(nozzle_diameter), volume_type, extruder_id, nozzle_count);
        setExtruderNozzleCount(preset_bundle, extruder_id, volume_type, nozzle_count);
    }
}

ExtruderBadge::ExtruderBadge(wxWindow* parent) : wxPanel(parent)
{
    wxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    SetBackgroundColour("#F8F8F8");
    wxBitmap icon = create_scaled_bitmap("nozzle_sync_badge", nullptr, FromDIP(90));

    left = new Label(this, _L("Left"));
    right = new Label(this, _L("Right"));

    badget = new wxStaticBitmap(this, wxID_ANY, icon);

    left_diameter_desp = new Label(this, _L("0.4mm"));
    right_diameter_desp = new Label(this, _L("0.4mm"));
    left_flow_desp = new Label(this, _L(get_nozzle_volume_type_string(NozzleVolumeType::nvtStandard)));
    right_flow_desp = new Label(this, _L(get_nozzle_volume_type_string(NozzleVolumeType::nvtStandard)));

    wxBoxSizer* top_h = new wxBoxSizer(wxHORIZONTAL);
    top_h->Add(left, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(15));
    top_h->AddStretchSpacer(1);
    top_h->Add(right, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(15));

    main_sizer->Add(top_h, 0, wxEXPAND | wxTOP, FromDIP(5));
    main_sizer->Add(badget, 0, wxALIGN_CENTER | wxTOP, FromDIP(5));

    wxBoxSizer* bottom_diameter = new wxBoxSizer(wxHORIZONTAL);
    bottom_diameter->Add(left_diameter_desp, 0, wxALIGN_CENTER_VERTICAL);
    bottom_diameter->AddStretchSpacer(1);
    bottom_diameter->Add(right_diameter_desp, 0, wxALIGN_CENTER_VERTICAL);
    main_sizer->Add(bottom_diameter, 0, wxEXPAND | wxTOP, FromDIP(5));

    wxBoxSizer* bottom_flow = new wxBoxSizer(wxHORIZONTAL);
    bottom_flow->Add(left_flow_desp, 0, wxALIGN_CENTER_VERTICAL|wxLEFT, FromDIP(15));
    bottom_flow->AddStretchSpacer(1);
    bottom_flow->Add(right_flow_desp, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, FromDIP(15));
    main_sizer->Add(bottom_flow, 0, wxEXPAND | wxTOP, FromDIP(5));


    SetSizer(main_sizer);
    Layout();
    Fit();
}

void ExtruderBadge::SetExtruderInfo(int extruder_id, const wxString& diameter, const wxString& volume_type)
{
    if (extruder_id == 0) {
        left_diameter_desp->SetLabel(diameter);
        left_flow_desp->SetLabel(volume_type);
    }
    else if (extruder_id == 1) {
        right_diameter_desp->SetLabel(diameter);
        right_flow_desp->SetLabel(volume_type);
    }
}

HotEndTable::HotEndTable(wxWindow* parent) :  wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    Bind(wxEVT_PAINT, &HotEndTable::OnPaint, this);
    auto main_sizer = new wxBoxSizer(wxVERTICAL);
    auto label = new Label(this, _L("Shelf"));
    label->SetBackgroundColour("#F8F8F8");

    m_arow_nozzle_box = CreateNozzleBox({ 0,2,4 });
    m_brow_nozzle_box = CreateNozzleBox({ 1,3,5 });
    main_sizer->Add(label, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxBOTTOM, FromDIP(5));
    main_sizer->Add(m_arow_nozzle_box, 0, wxLEFT | wxRIGHT, FromDIP(5));
    main_sizer->Add(m_brow_nozzle_box, 0, wxLEFT | wxRIGHT, FromDIP(5));
    SetBackgroundColour("#F8F8F8");

    SetSizer(main_sizer);
    Layout();
    Fit();
}

void HotEndTable::UpdateRackInfo(std::weak_ptr<DevNozzleRack> rack)
{
    m_nozzle_rack = rack;
    const auto& nozzle_rack = rack.lock();
    if (nozzle_rack) {
        UpdateNozzleItems(m_nozzle_items, nozzle_rack);
    }
}

StaticBox* HotEndTable::CreateNozzleBox(const std::vector<int>& nozzle_indices)
{
    StaticBox* nozzle_box = new StaticBox(this);
    nozzle_box->SetBackgroundColour("#F8F8F8");
    nozzle_box->SetBorderColorNormal("#F8F8F8");
    nozzle_box->SetCornerRadius(0);

    wxSizer* h_sizer = new wxBoxSizer(wxHORIZONTAL);
    for (auto idx : nozzle_indices) {
        wgtDeviceNozzleRackNozzleItem* nozzle_item = new wgtDeviceNozzleRackNozzleItem(nozzle_box, idx);
        nozzle_item->SetBackgroundColorNormal("#EEEEEE");
        for (auto& child : nozzle_item->GetChildren())
            child->SetBackgroundColour("#EEEEEE");
        m_nozzle_items[idx] = nozzle_item;
        h_sizer->Add(nozzle_item, 0, wxALL, FromDIP(8));
    }

    nozzle_box->SetSizer(h_sizer);

    return nozzle_box;
}

void HotEndTable::UpdateNozzleItems(const std::unordered_map<int, wgtDeviceNozzleRackNozzleItem*>& nozzle_items, std::shared_ptr<DevNozzleRack> nozzle_rack)
{
    for (auto& item : nozzle_items)
        item.second->Update(nozzle_rack);
}

void HotEndTable::OnPaint(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    wxSize size = GetClientSize();

    dc.SetPen(wxPen(wxColour("#EEEEEE"), 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRoundedRectangle(0, 0, size.GetWidth(), size.GetHeight(), 5);
}


MultiNozzleStatusTable::MultiNozzleStatusTable(wxWindow* parent): wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
{
    m_badge = new ExtruderBadge(this);

    SetBackgroundColour(wxColour("#F8F8F8"));

    auto main_sizer    = new wxBoxSizer(wxVERTICAL);

    auto title_panel = new wxPanel(this);
    title_panel->SetBackgroundColour(0xEEEEEE);
    auto title_sizer = new wxBoxSizer(wxHORIZONTAL);
    title_panel->SetSizer(title_sizer);

    Label* static_text = new Label(this, _L("Nozzle Info"));
    static_text->SetFont(Label::Head_13);
    static_text->SetBackgroundColour(0xEEEEEE);

    title_sizer->Add(static_text, 0, wxALIGN_CENTER | wxALL, FromDIP(5));

    main_sizer->Add(title_panel, 0, wxEXPAND);
    main_sizer->AddSpacer(10);

    auto nozzle_panel = new wxPanel(this);
    wxSizer* nozzle_area_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_table = new HotEndTable(this);

    nozzle_area_sizer->Add(m_badge, 0, wxLEFT | wxRIGHT, FromDIP(20));
    nozzle_area_sizer->Add(m_table, 0, wxRIGHT, FromDIP(10));

    main_sizer->Add(nozzle_area_sizer);
    main_sizer->AddSpacer(FromDIP(5));

    SetSizer(main_sizer);
    Layout();
    Fit();
}

void MultiNozzleStatusTable::UpdateRackInfo(std::weak_ptr<DevNozzleRack> rack)
{
    if (m_table)
        m_table->UpdateRackInfo(rack);
    if (m_badge) {
        auto nozzle_rack = rack.lock();
        if (!nozzle_rack)
            return;
        auto nozzles_in_extruder = nozzle_rack->GetNozzleSystem()->GetNozzles();
        for (auto& elem : nozzles_in_extruder) {
            auto& nozzle = elem.second;
            int extruder_id = nozzle.AtLeftExtruder() ? 0 : 1;
            wxString flow_str;
            if (nozzle.m_nozzle_flow == NozzleFlowType::H_FLOW)
                flow_str = _L(get_nozzle_volume_type_string(nvtHighFlow));
            else
                flow_str = _L(get_nozzle_volume_type_string(nvtStandard));

            m_badge->SetExtruderInfo(extruder_id, nozzle.GetNozzleDiameterStr(), flow_str);
        }
    }
}


Slic3r::GUI::MultiNozzleSyncDialog::MultiNozzleSyncDialog(wxWindow* parent,std::weak_ptr<DevNozzleRack> rack) : DPIDialog(parent, wxID_ANY, "Sync Nozzle status",wxDefaultPosition, wxDefaultSize,wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    m_nozzle_rack = rack;
    wxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    SetBackgroundColour(*wxWHITE);

    m_tips = new Label(this, "");
    wxBoxSizer* label_sizer = new wxBoxSizer(wxHORIZONTAL);
    label_sizer->Add(m_tips, 0, wxLEFT | wxRIGHT, FromDIP(25));
    main_sizer->Add(label_sizer, 0, wxTOP | wxBOTTOM, FromDIP(15));

    m_list_panel = new wxPanel(this);
    m_list_panel->SetBackgroundColour(*wxWHITE);
    main_sizer->Add(m_list_panel, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(5));

    m_nozzle_table = new MultiNozzleStatusTable(this);
    wxBoxSizer* table_sizer = new wxBoxSizer(wxHORIZONTAL);
    table_sizer->Add(m_nozzle_table, 0, wxLEFT | wxRIGHT, FromDIP(25));
    main_sizer->Add(table_sizer, 0, wxTOP | wxBOTTOM, FromDIP(15));

    wxBoxSizer* button_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_cancel_btn = new Button(this, "Cancel");
    m_confirm_btn = new Button(this, "Confirm");

    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    StateColor btn_br_green(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

    m_confirm_btn->SetBackgroundColor(btn_bg_green);
    m_confirm_btn->SetTextColor(wxColour("#FFFFFE"));
    m_confirm_btn->SetMinSize(wxSize(FromDIP(60), FromDIP(24)));
    m_cancel_btn->SetMinSize(wxSize(FromDIP(60), FromDIP(24)));

    button_sizer->AddStretchSpacer();
    button_sizer->Add(m_cancel_btn, 0, wxALL, FromDIP(10));
    button_sizer->Add(m_confirm_btn, 0, wxALL, FromDIP(10));

    main_sizer->Add(button_sizer, 0, wxEXPAND | wxALL, FromDIP(10));

    SetSizer(main_sizer);

    main_sizer->SetSizeHints(this);
    main_sizer->Fit(this);

    int table_width = m_nozzle_table->GetBestSize().GetWidth();
    m_tips->Wrap(table_width);
    m_tips->SetMaxSize(wxSize(table_width, -1));

    m_refresh_timer = new wxTimer(this);
    Bind(wxEVT_TIMER, &MultiNozzleSyncDialog::OnRefreshTimer, this);
}


void MultiNozzleSyncDialog::OnRackStatusReadingFinished(wxEvent& evt) {
    if (!IsShown()) return;

    m_refreshing = false;
    if (m_refresh_timer)
        m_refresh_timer->Stop();
    if (!UpdateUi(m_nozzle_rack))
        EndModal(wxID_OK);
}

void MultiNozzleSyncDialog::OnRefreshTimer(wxTimerEvent& evt){
    if(!m_refreshing)
        return;
    auto nozzle_rack = m_nozzle_rack.lock();
    if(!nozzle_rack)
        return;

    int reading_count = nozzle_rack->GetReadingCount();
    int reading_idx = nozzle_rack->GetReadingIdx();

    wxString tip = wxString::Format("Refresh %d/%d...", reading_idx, reading_count);
    m_confirm_btn->SetLabel(tip);
    m_confirm_btn->Fit();
    if (m_confirm_btn->GetParent())
        m_confirm_btn->GetParent()->Layout();

    m_confirm_btn->Disable();
    m_cancel_btn->Disable();
}

void MultiNozzleSyncDialog::UpdateRackInfo(std::weak_ptr<DevNozzleRack> rack)
{
    m_nozzle_rack = rack;
    UpdateUi(rack);
}

void MultiNozzleSyncDialog::OnSelectRadio(int select_idx)
{
    for (size_t idx = 0; idx < m_nozzle_option_labels.size(); ++idx) {
        auto& radio_box = m_nozzle_option_labels[idx].first;
        if (idx == select_idx)
            radio_box->SetValue(true);
        else
            radio_box->SetValue(false);
    }
    m_nozzle_option_idx = select_idx;
}

bool MultiNozzleSyncDialog::hasMultiDiameters(const std::vector<MultiNozzleUtils::NozzleGroupInfo>& group_infos)
{
    if (group_infos.empty())
        return false;
    return !std::all_of(group_infos.begin(), group_infos.end(), [val = group_infos.front()](auto& elem) {return val.diameter == elem.diameter; });
}


std::vector<NozzleOption> MultiNozzleSyncDialog::GetNozzleOptions(const std::vector<MultiNozzleUtils::NozzleGroupInfo>& nozzle_groups)
{
    std::vector<NozzleOption> options;

    std::set<std::string> diameters;
    std::multimap<std::string, MultiNozzleUtils::NozzleGroupInfo> groups_mapped_for_diameter;
    for (auto& nozzle_group : nozzle_groups) {
        groups_mapped_for_diameter.insert({ nozzle_group.diameter,nozzle_group });
    }

#if ENABLE_MIX_FLOW_PRINT
    for (auto it = groups_mapped_for_diameter.begin(); it != groups_mapped_for_diameter.end(); ) {
        NozzleOption option;
        const auto& diameter = it->first;
        auto range = groups_mapped_for_diameter.equal_range(diameter);

        option.diameter = diameter;
        for (auto val_it = range.first; val_it != range.second; ++val_it) {
            const auto& elem = val_it->second;
            option.extruder_nozzle_count[elem.extruder_id][elem.volume_type] += elem.nozzle_count;
        }
        options.emplace_back(std::move(option));
        it = range.second;
    }
#else
    for (auto it = groups_mapped_for_diameter.begin(); it != groups_mapped_for_diameter.end(); ) {
        const auto& diameter = it->first;
        auto range = groups_mapped_for_diameter.equal_range(diameter);

        std::vector<std::pair<NozzleVolumeType, int>> left_nozzles;
        std::vector<std::pair<NozzleVolumeType, int>> right_nozzles;

        for (auto val_it = range.first; val_it != range.second; ++val_it) {
            const auto& elem = val_it->second;
            if (elem.extruder_id == 0)
                left_nozzles.emplace_back(elem.volume_type, elem.nozzle_count);
            else
                right_nozzles.emplace_back(elem.volume_type, elem.nozzle_count);
        }

        for (const auto& left_nozzle : left_nozzles.empty() ? std::vector<std::pair<NozzleVolumeType, int>>{{}} : left_nozzles) {
            for (const auto& right_nozzle : right_nozzles.empty() ? std::vector<std::pair<NozzleVolumeType, int>>{{}} : right_nozzles) {
                NozzleOption option;
                option.diameter = diameter;

                if (!left_nozzles.empty()) {
                    option.extruder_nozzle_count[0] = { left_nozzle.first, left_nozzle.second };
                }

                if (!right_nozzles.empty()) {
                    option.extruder_nozzle_count[1] = { right_nozzle.first, right_nozzle.second };
                }

                options.emplace_back(std::move(option));
            }
        }

        it = range.second;
    }
#endif
    return options;
}

bool MultiNozzleSyncDialog::UpdateOptionList(std::weak_ptr<DevNozzleRack> rack, bool ignore_unknown, bool ignore_unreliable)
{
    const auto& nozzle_rack = rack.lock();
    if (!nozzle_rack)
        return true;
    bool has_unknown = nozzle_rack->HasUnknownNozzles() && !ignore_unknown;
    bool has_unreliable = nozzle_rack->HasUnreliableNozzles() && !ignore_unreliable;

    if (has_unknown || has_unreliable) {
        m_list_panel->Hide();
        return true;
    }

    m_list_panel->Show();

    m_nozzle_option_values.clear();
    m_nozzle_option_labels.clear();

    auto nozzle_groups = nozzle_rack->GetNozzleGroups();
    auto options = GetNozzleOptions(nozzle_groups);

    m_nozzle_option_values = options;

    auto list_sizer = new wxBoxSizer(wxVERTICAL);

    int recommend_idx = std::max_element(options.begin(), options.end(), [](const NozzleOption& opt1, const NozzleOption& opt2) {
        int count1 = 0, count2 = 0;
        for (auto elem : opt1.extruder_nozzle_count)
            count1 += elem.second.second;
        for (auto elem : opt2.extruder_nozzle_count)
            count2 += elem.second.second;
        return count1 < count2;
        }) - options.begin();


        for (size_t idx = 0; idx < options.size(); ++idx) {
            auto& option = options[idx];
            std::vector<NozzleVolumeType> left_types, right_types;
            wxString left_nozzle;
            wxString right_nozzle;

            if (!option.extruder_nozzle_count.count(0))
                left_nozzle = _L("Left Nozzle(0)");
            else
                left_nozzle = wxString::Format(_L("Left %s Nozzle(%d)"), get_nozzle_volume_type_string(option.extruder_nozzle_count[0].first), option.extruder_nozzle_count[0].second);

            if (!option.extruder_nozzle_count.count(1))
                right_nozzle = _L("Right Nozzle(0)");
            else
                right_nozzle = wxString::Format(_L("Right %s Nozzle(%d)"), get_nozzle_volume_type_string(option.extruder_nozzle_count[1].first), option.extruder_nozzle_count[1].second);

            wxString label = wxString::Format("%s: %s + %s", option.diameter, left_nozzle, right_nozzle);
            auto line_sizer = new wxBoxSizer(wxHORIZONTAL);
            RadioBox* rb = new RadioBox(m_list_panel);
            Label* lb = new Label(m_list_panel, label);
            m_nozzle_option_labels.push_back({ rb,lb });
            rb->Bind(wxEVT_LEFT_DOWN, [this, idx](wxMouseEvent& e) {
                OnSelectRadio(idx);
                });

            if (idx == recommend_idx)
                OnSelectRadio(idx);

            line_sizer->Add(rb, 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(5));
            line_sizer->Add(lb, 1, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(5));
            list_sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
        }
        m_list_panel->SetSizer(list_sizer);

        if (!has_unknown && !has_unreliable && options.size() == 1) {
            return false;
        }
        return true;
}

void MultiNozzleSyncDialog::UpdateTip(std::weak_ptr<DevNozzleRack> rack, bool ignore_unknown, bool ignore_unreliable)
{
    const auto& nozzle_rack = rack.lock();
    if (!nozzle_rack)
        return;

    bool has_unknown = nozzle_rack->HasUnknownNozzles() && !ignore_unknown;
    bool has_unreliable = nozzle_rack->HasUnreliableNozzles() && !ignore_unreliable;

    if (has_unknown && has_unreliable) {
        m_tips->SetLabel(_L("Unknown nozzle detected. Refresh to update info (unrefreshed nozzles will be excluded during slicing). Verify nozzle diameter & flow rate against displayed values."));
    }
    else if (has_unknown) {
        m_tips->SetLabel(_L("Unknown nozzle detected. Refresh to update (unrefreshed nozzles will be skipped in slicing)."));
    }
    else if (has_unreliable) {
        m_tips->SetLabel(_L("Please confirm whether the required nozzle diameter and flow rate match the currently displayed values."));
    }
    else {
        auto nozzle_groups = nozzle_rack->GetNozzleGroups();

        if (hasMultiDiameters(nozzle_groups)) {
            m_tips->SetLabel(_L("Your printer has multiple nozzle sizes installed. Mixing nozzle diameters in one print is not supported."
                "Please select a nozzle diameter for this print(If the selected size is only on one extruder, single-extruder printing will be enforced.)"));
        }
        else {
            m_tips->SetLabel("");
        }
    }
}

int MultiNozzleSyncDialog::ShowModal()
{
    bool res = UpdateUi(m_nozzle_rack);
    if (!res)
        return wxID_OK;
    
    return DPIDialog::ShowModal();

}

MultiNozzleSyncDialog::~MultiNozzleSyncDialog()
{
    if (m_refresh_timer)
        m_refresh_timer->Stop();
    if (auto rack = m_nozzle_rack.lock()) {
        rack->Unbind(DEV_RACK_EVENT_READING_FINISHED, &MultiNozzleSyncDialog::OnRackStatusReadingFinished, this);
    }
}

void MultiNozzleSyncDialog::UpdateButton(std::weak_ptr<DevNozzleRack> rack, bool ignore_unknown, bool ignore_unreliable)
{
    const auto& nozzle_rack = rack.lock();
    if (!nozzle_rack)
        return;

    if (m_refreshing)
        return;

    bool has_unknown = nozzle_rack->HasUnknownNozzles() && !ignore_unknown;
    bool has_unreliable = nozzle_rack->HasUnreliableNozzles() && !ignore_unreliable;

    auto refresh_cmd = [rack, this]() {
        auto nozzle_rack = m_nozzle_rack.lock();
        if (!nozzle_rack)
            return;
        nozzle_rack->CtrlRackReadAll();
        m_refreshing = true;
        if (m_refresh_timer)
            m_refresh_timer->Start(500);
        nozzle_rack->Bind(DEV_RACK_EVENT_READING_FINISHED, &MultiNozzleSyncDialog::OnRackStatusReadingFinished, this);
        };

    auto trust_cmd = [rack, this]() {
        auto nozzle_rack = rack.lock();
        if (!nozzle_rack)
            return;
        nozzle_rack->CtrlRackConfirmAll();
        UpdateUi(rack, true, false);
    };

    auto ignore_opt = [rack,this]() {
        if (!UpdateUi(rack, true, true))
            EndModal(wxID_OK);
        };

    m_cancel_btn->Enable();
    m_confirm_btn->Enable();

    if (has_unknown && has_unreliable) {
        m_cancel_btn->Show();
        m_confirm_btn->Show();

        m_cancel_btn->SetLabel(_L("Ignore"));
        m_confirm_btn->SetLabel(_L("Refresh"));

        m_cancel_btn->Bind(wxEVT_LEFT_DOWN, [this, rack, ignore_opt](auto& e) {ignore_opt(); });
        m_confirm_btn->Bind(wxEVT_LEFT_DOWN, [this, rack, refresh_cmd](auto& e) {refresh_cmd(); });
    }
    else if (has_unknown) {
        m_cancel_btn->Show();
        m_confirm_btn->Show();

        m_cancel_btn->SetLabel(_L("Ignore"));
        m_confirm_btn->SetLabel(_L("Refresh"));

        m_cancel_btn->Bind(wxEVT_LEFT_DOWN, [this, rack, ignore_opt](auto& e) {ignore_opt(); });
        m_confirm_btn->Bind(wxEVT_LEFT_DOWN, [this, rack, refresh_cmd](auto& e) {refresh_cmd(); });
    }
    else if (has_unreliable) {
        m_cancel_btn->Show();
        m_confirm_btn->Show();

        m_cancel_btn->SetLabel(_L("Refresh"));
        m_confirm_btn->SetLabel(_L("Confirm"));

        m_cancel_btn->Bind(wxEVT_LEFT_DOWN, [this, rack, refresh_cmd](auto& e) {refresh_cmd(); });
        m_confirm_btn->Bind(wxEVT_LEFT_DOWN, [this, rack, trust_cmd](auto& e) {trust_cmd(); });

    }
    else {
        m_cancel_btn->Hide();
        m_confirm_btn->Show();
        m_confirm_btn->SetLabel(_L("OK"));
        m_confirm_btn->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {EndModal(wxID_OK); });
    }

}

bool MultiNozzleSyncDialog::UpdateUi(std::weak_ptr<DevNozzleRack> rack, bool ignore_unknown, bool ignore_unreliable)
{
    m_nozzle_table->UpdateRackInfo(rack);
    bool res = UpdateOptionList(rack, ignore_unknown, ignore_unreliable);
    if (!res)
        return false;

    UpdateTip(rack, ignore_unknown, ignore_unreliable);
    UpdateButton(rack, ignore_unknown, ignore_unreliable);

    Layout();

    int table_width = m_nozzle_table->GetBestSize().GetWidth();
    m_tips->Wrap(table_width);
    m_tips->SetMaxSize(wxSize(table_width, -1));
    Fit();
    return true;
}


std::optional<NozzleOption> tryPopUpMultiNozzleDialog(MachineObject* obj)
{
    if (!obj)
        return std::nullopt;
    auto rack = obj->GetNozzleSystem()->GetNozzleRack();
    if (!rack)
        return std::nullopt;
    MultiNozzleSyncDialog dialog(wxGetApp().plater_,rack);

    bool has_unreliable = rack->HasUnreliableNozzles();
    bool has_unknown = rack->HasUnknownNozzles();

    auto config = wxGetApp().preset_bundle->printers.get_edited_preset().config;

    if (dialog.ShowModal() == wxID_OK) {
        auto selected_option = dialog.GetSelectedOption();
        if (!selected_option)
            return std::nullopt;
        auto& preset_bundle = GUI::wxGetApp().preset_bundle;
        auto& project_config = preset_bundle->project_config;

        ConfigOptionEnumsGeneric* nozzle_volume_type_opt = project_config.option<ConfigOptionEnumsGeneric>("nozzle_volume_type");

        // write to preset bundle config
        for (int extruder_id = 0; extruder_id < 2; ++extruder_id) {
            NozzleVolumeType volume_type;
            int nozzle_count;
            if (!selected_option->extruder_nozzle_count.count(extruder_id)) {
                volume_type = NozzleVolumeType(nozzle_volume_type_opt->values[extruder_id]);
                nozzle_count = 0;
            }
            else {
                volume_type = selected_option->extruder_nozzle_count[extruder_id].first;
                nozzle_count = selected_option->extruder_nozzle_count[extruder_id].second;
            }
            setExtruderNozzleCount(preset_bundle, extruder_id, volume_type, nozzle_count);
        }
        return selected_option;
    }

    return std::nullopt;
}

void setExtruderNozzleCount(PresetBundle* preset_bundle, int extruder_id, NozzleVolumeType volume_type, int nozzle_count, bool update_ui)
{
    if (!preset_bundle)
        return;
    if (extruder_id >= preset_bundle->extruder_nozzle_counts.size())
        preset_bundle->extruder_nozzle_counts.resize(extruder_id + 1);
    preset_bundle->extruder_nozzle_counts[extruder_id].clear(); // currently we do not support multiple volume types
    preset_bundle->extruder_nozzle_counts[extruder_id][volume_type] = nozzle_count;
    if (update_ui) {
        int sum_count = 0;
        for (auto& elem : preset_bundle->extruder_nozzle_counts[extruder_id])
            sum_count += elem.second;
        wxGetApp().plater()->sidebar().set_extruder_nozzle_count(extruder_id, sum_count);
    }
}


}
