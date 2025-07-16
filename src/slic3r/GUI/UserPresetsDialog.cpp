
#include "UserPresetsDialog.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"

#include <slic3r/GUI/Widgets/CheckBox.hpp>
#include <slic3r/GUI/Widgets/TabCtrl.hpp>

namespace Slic3r {
namespace GUI {

UserPresetsDialog::UserPresetsDialog(wxWindow *parent)
    : DPIDialog(parent, wxID_ANY, _L("Management user presets"))
{
    SetBackgroundColour(*wxWHITE);
    SetMinSize({FromDIP(788), -1});

    m_tab_ctrl = new TabCtrl(this, wxID_ANY);
    m_tab_ctrl->SetFont(Label::Body_14);
    m_tab_ctrl->AppendItem("");
    m_tab_ctrl->AppendItem("");
    m_tab_ctrl->AppendItem("");
    m_tab_ctrl->SelectItem(0);
    m_tab_ctrl->Bind(wxEVT_TAB_SEL_CHANGED, [this] (auto & evt) { on_collection_changed(evt.GetInt()); });

    m_switch_button = new SwitchButton(this);
    m_switch_button->SetFont(Label::Body_13);
    m_switch_button->SetMaxSize({FromDIP(182), -1});
    m_switch_button->SetLabels(" " + _L("Custom filaments") + " ", _L("Others"));
    m_switch_button->Bind(wxEVT_TOGGLEBUTTON, [this](auto &evt) { evt.Skip(); on_collection_changed(m_collection); });

    m_search = new TextInput(this, "", "", "im_text_search");
    m_search->SetSize({FromDIP(568), FromDIP(24)});
    m_search->SetCornerRadius(FromDIP(12));
    m_search->Bind(wxEVT_TEXT, [this](auto &evt) { on_search(evt.GetString()); });

    m_empty_panel = new wxPanel(this);
    m_empty_panel->SetMinSize({-1, FromDIP(360)});
    m_empty_panel->SetMaxSize({-1, FromDIP(360)});
    m_empty_panel->SetForegroundColour(wxColor("#A0A0A0"));
    {
        wxSizer *sizer = new wxBoxSizer(wxVERTICAL);
        wxStaticBitmap *bitmap = new wxStaticBitmap(m_empty_panel, wxID_ANY, create_scaled_bitmap(wxGetApp().dark_mode() ? "preset_empty_dark" : "preset_empty", this, 150));
        sizer->Add(bitmap, 0, wxALIGN_CENTER | wxTOP, FromDIP(70));
        Label *label = new Label(m_empty_panel, _L("No content"));
        label->SetBackgroundColour(this->GetBackgroundColour());
        label->SetForegroundColour(m_empty_panel->GetForegroundColour());
        sizer->Add(label, 0, wxALIGN_CENTER);
        m_empty_panel->SetSizer(sizer);
        m_empty_panel->Hide();
    }

    m_scrolled = new wxScrolledWindow(this);
    m_scrolled->SetBackgroundColour("#F8F8F8");
    m_scrolled->SetScrollbars(0, 100, 1, 2);
    m_scrolled->SetScrollRate(0, 5);
    m_scrolled->SetMinSize({-1, FromDIP(360)});
    m_scrolled->SetMaxSize({-1, FromDIP(360)});
    {
        wxSizer *sizer = new wxBoxSizer(wxHORIZONTAL);
        wxSizer *sizerLeft = new wxBoxSizer(wxVERTICAL);
        auto     line      = new StaticLine(m_scrolled, true);
        wxSizer *sizerRight = new wxBoxSizer(wxVERTICAL);
        sizer->Add(sizerLeft, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
        sizer->Add(line, 0, wxEXPAND);
        sizer->Add(sizerRight, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
        m_scrolled->SetSizer(sizer);
    }

    m_check_all = new CheckBox(this);
    auto label = new Label(this, _L("Select All"));
    m_label_check_count = new Label(this);
    m_label_check_count->SetForegroundColour("#6B6B6B");
    m_button_delete     = new Button(this, _L("Delete"));
    m_button_delete->SetBorderColorNormal(wxColor("#D01B1B"));
    m_button_delete->SetTextColorNormal(wxColor("#D01B1B"));
    m_check_all->Bind(wxEVT_TOGGLEBUTTON, [this](auto &evt) { on_all_checked(evt.IsChecked(), true); });
    label->Bind(wxEVT_LEFT_UP, [this](auto &evt) {
        bool checked = !m_check_all->GetValue();
        m_check_all->SetValue(checked);
        on_all_checked(checked, true);
    });
    m_button_delete->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](auto &evt) { delete_checked(); });
    wxSizer *sizer_bottom = new wxBoxSizer(wxHORIZONTAL);
    sizer_bottom->Add(m_check_all, 0, wxALIGN_CENTER | wxLEFT, FromDIP(20));
    sizer_bottom->Add(label, 0, wxALIGN_CENTER | wxLEFT, FromDIP(8));
    sizer_bottom->Add(m_label_check_count, 1, wxALIGN_CENTER | wxLEFT, FromDIP(8));
    sizer_bottom->Add(m_button_delete, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(20));

    wxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(sizer);
    sizer->Add(m_tab_ctrl, 0, wxALIGN_CENTER | wxALL, FromDIP(20));
    sizer->Add(m_switch_button, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(10));
    sizer->Add(m_search, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(10));
    sizer->Add(m_scrolled, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    sizer->Add(m_empty_panel, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    sizer->Add(sizer_bottom, 0, wxEXPAND | wxALL, FromDIP(20));

    wxGetApp().UpdateDlgDarkUI(this);
    m_switch_button->Rescale();

    init_preset_list();
    update_preset_counts();
    on_collection_changed(0);

    Layout();
    Fit();
    CenterOnParent();
}

void UserPresetsDialog::init_preset_list()
{
    auto bundle = wxGetApp().preset_bundle;
    for (PresetCollection *collection : {(PresetCollection *) &bundle->printers, &bundle->filaments, &bundle->prints}) {
        std::vector<std::string> presets;
        for (auto &preset : *collection) {
            if (!preset.is_user()) continue;
            if (collection->type() == Preset::TYPE_FILAMENT) {
                if (preset.base_id.empty()) {
                    auto id = preset.filament_id;
                    m_filament_names[id] = preset.alias;
                    m_filament_presets[id].emplace_back(preset.name);
                    continue;
                }
            }
            presets.push_back(preset.name);
        }
        m_presets.emplace_back(std::move(presets));
    }
}

void UserPresetsDialog::create_preset_list(wxWindow *parent)
{
    if (is_filament_list()) {
        for (auto &filament : m_filament_presets)
            m_filament_sizers.emplace(filament.first, create_filament_group(parent, filament));
    } else {
        auto &presets = m_presets[m_collection];
        for (auto &preset : presets)
            m_preset_sizers.emplace(preset, create_preset_line(m_scrolled, preset));
    }
}

wxSizer *UserPresetsDialog::create_preset_line(wxWindow *parent, std::string const &preset)
{
    wxSizer *vsizer = new wxBoxSizer(wxVERTICAL);
    wxSizer *hsizer = new wxBoxSizer(wxHORIZONTAL);
    auto check = new CheckBox(parent);
    auto label = new Label(parent, from_u8(preset), wxST_ELLIPSIZE_END);
    auto line  = new StaticLine(parent);
    label->SetMaxSize({FromDIP(268), -1});
    label->SetToolTip(label->GetLabel());
    check->Bind(wxEVT_TOGGLEBUTTON, [this, preset](auto &evt) {
        evt.Skip();
        on_preset_checked(preset, evt.IsChecked(), true);
    });
    label->Bind(wxEVT_LEFT_UP, [this, preset, check](auto &evt) {
        bool checked = !check->GetValue();
        check->SetValue(checked);
        on_preset_checked(preset, checked, true);
    });
    hsizer->Add(check, 0, wxALIGN_CENTER | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(6));
    hsizer->Add(label, 1, wxALIGN_CENTER);
    vsizer->Add(hsizer, 0, wxEXPAND);
    vsizer->Add(line, 0, wxEXPAND);
    return vsizer;
}

wxSizer *UserPresetsDialog::create_filament_group(wxWindow *parent, std::pair<std::string const, std::vector<std::string>> const &filament)
{
    wxSizer * vsizer = new wxBoxSizer(wxVERTICAL);
    wxSizer * hsizer = new wxBoxSizer(wxHORIZONTAL);
    auto check  = new CheckBox(parent);
    auto label = new Label(parent, from_u8(m_filament_names[filament.first]), wxST_ELLIPSIZE_END);
    auto line  = new StaticLine(parent);
    label->SetMaxSize({-1, FromDIP(268)});
    label->SetToolTip(label->GetLabel());
    check->Bind(wxEVT_TOGGLEBUTTON, [this, filament = filament.first](auto &evt) {
        evt.Skip();
        on_filament_checked(filament, evt.IsChecked(), true);
    });
    label->Bind(wxEVT_LEFT_UP, [this, filament = filament.first, check](auto &evt) {
        bool checked = !check->GetValue();
        check->SetValue(checked);
        on_filament_checked(filament, checked, true);
    });
    hsizer->Add(check, 0, wxALIGN_CENTER | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(6));
    hsizer->Add(label, 1, wxALIGN_CENTER);
    vsizer->Add(hsizer, 0, wxEXPAND);
    vsizer->Add(line, 0, wxEXPAND);
    for (auto preset : filament.second) {
        auto sizer3 = create_preset_line(parent, preset);
        m_preset_sizers.emplace(preset, sizer3);
        vsizer->Add(sizer3, 0, wxEXPAND | wxLEFT, FromDIP(20));
    }
    return vsizer;
}

void UserPresetsDialog::layout_preset_list(bool delete_old)
{
    wxSizer *sizer      = m_scrolled->GetSizer();
    wxSizer *sizerLeft  = sizer->GetItem(size_t(0))->GetSizer();
    wxSizer *sizerRight = sizer->GetItem(size_t(2))->GetSizer();
    if (delete_old) {
        sizerLeft->Clear(true);
        sizerRight->Clear(true);
    } else {
        while (!sizerLeft->IsEmpty()) sizerLeft->Detach(0);
        while (!sizerRight->IsEmpty()) sizerRight->Detach(0);
    }
    sizer = sizerLeft;
    if (is_filament_list()) {
        size_t total = (std::accumulate(m_filament_presets.begin(), m_filament_presets.end(), 0,
                [](size_t t, auto &filament) { return t + filament.second.size() + 1; }) - m_hiden_sizers.size());
        for (auto &filament : m_filament_presets) {
            auto sizer2 = m_filament_sizers[filament.first];
            if (m_hiden_sizers.count(sizer2) > 0)
                continue;
            size_t count = 1 + std::count_if(filament.second.begin(), filament.second.end(), [this](auto & preset) {
                return m_hiden_sizers.count(m_preset_sizers[preset]) == 0;
            });
            if (sizer == sizerLeft) {
                size_t diff = std::abs(int(total - count * 2));
                if (diff > std::abs(int(total)))
                    sizer = sizerRight;
                else
                    total -= count * 2;
            }
            sizer->Add(sizer2, 0, wxEXPAND);
        }
    } else {
        auto & presets = m_presets[m_collection];
        size_t total   = (presets.size() - m_hiden_sizers.size() + 1) / 2;
        for (auto &preset : presets) {
            auto sizer2 = m_preset_sizers[preset];
            if (m_hiden_sizers.count(sizer2) > 0)
                continue;
            sizer->Add(sizer2, 0, wxEXPAND);
            if (--total == 0)
                sizer = sizerRight;
        }
    }
    bool is_empty = sizerLeft->IsEmpty() && sizerRight->IsEmpty();
    m_scrolled->Show(!is_empty);
    m_empty_panel->Show(is_empty);
    m_scrolled->Layout();
    m_empty_panel->Layout();
}

void UserPresetsDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    SetMinSize({FromDIP(788), -1});
    m_tab_ctrl->Rescale();
    m_switch_button->SetMaxSize({FromDIP(182), -1});
    m_switch_button->Rescale();
    m_search->SetSize({FromDIP(568), FromDIP(24)});
    m_search->SetCornerRadius(FromDIP(12));
    m_scrolled->SetMinSize({-1, FromDIP(320)});
    m_scrolled->SetMaxSize({-1, FromDIP(320)});
    for (auto sizer : m_preset_sizers) {
        auto *label = dynamic_cast<Label *>(sizer.second->GetItem(size_t(0))->GetSizer()->GetItem(size_t(1))->GetWindow());
        label->SetMaxSize({FromDIP(268), -1});
    }
    Layout();
    Fit();
}

bool UserPresetsDialog::is_filament_list() const { return m_collection == 1 && m_switch_button->GetValue() == false; }

void UserPresetsDialog::on_collection_changed(int collection)
{
    std::swap(m_collection, collection);
    m_checked_filaments.clear();
    m_checked_presets.clear();
    m_preset_sizers.clear();
    m_filament_sizers.clear();
    m_hiden_sizers.clear();
    m_tab_ctrl->SetItemBold(collection, false);
    m_tab_ctrl->SetItemBold(m_collection, true);
    m_search->GetTextCtrl()->ChangeValue("");
    GetSizer()->Show(m_switch_button, m_collection == 1);
    Freeze();
    create_preset_list(m_scrolled);
    layout_preset_list(true);
    wxGetApp().UpdateDarkUIWin(m_scrolled);
    Thaw();
    update_checked();
    Layout();
    Refresh();
}

void UserPresetsDialog::on_search(wxString const &keyword)
{
    if (keyword.IsEmpty()) {
        for (auto sizer : m_hiden_sizers)
            sizer->Show(true);
        m_hiden_sizers.clear();
        m_scrolled->Freeze();
        layout_preset_list();
        m_scrolled->Thaw();
        return;
    }
    auto & hiden_sizers = m_hiden_sizers;
    auto show_sizers = [&hiden_sizers](wxSizer* sizer, bool show) {
        auto iter = hiden_sizers.find(sizer);
        if (show) {
            if (iter != hiden_sizers.end()) {
                sizer->Show(true);
                hiden_sizers.erase(iter);
            }
        } else {
            if (iter == hiden_sizers.end()) {
                sizer->Show(false);
                hiden_sizers.insert(sizer);
            }
        }
    };
    m_scrolled->Freeze();
    std::string key = into_u8(keyword);
    auto match = [&key](std::string & preset) {
        return std::search(preset.begin(), preset.end(), key.begin(), key.end(),
            [](char a, char b) { return std::tolower(a) == std::tolower(b); }) != preset.end();
    };
    if (is_filament_list()) {
        for (auto &filament : m_filament_presets) {
            size_t count = 0;
            for (auto &preset : filament.second) {
                bool show = match(preset);
                show_sizers(m_preset_sizers[preset], show);
                if (!show) ++count;
            }
            show_sizers(m_filament_sizers[filament.first], count != filament.second.size());
        }
    } else {
        auto &presets = m_presets[m_collection];
        for (auto &preset : presets)
            show_sizers(m_preset_sizers[preset], match(preset));
    }
    layout_preset_list();
    m_scrolled->Thaw();
}

void UserPresetsDialog::on_preset_checked(std::string const &preset, bool checked, bool from_user)
{
    auto iter = std::lower_bound(m_checked_presets.begin(), m_checked_presets.end(), preset);
    bool old  = iter != m_checked_presets.end() && *iter == preset;
    if (old == checked)
        return;
    if (checked) {
        m_checked_presets.insert(iter, preset);
    } else {
        m_checked_presets.erase(iter);
    }
    if (!from_user) {
        auto *cb = dynamic_cast<CheckBox *>(m_preset_sizers[preset]->GetItem(size_t(0))->GetSizer()->GetItem(size_t(0))->GetWindow());
        cb->SetValue(checked);
    } else {
        if (m_collection == 1) {
            for (auto &filament : m_filament_presets) {
                auto iter = std::lower_bound(filament.second.begin(), filament.second.end(), preset);
                if (iter != filament.second.end() && *iter == preset) {
                    auto & count = m_checked_filaments[filament.first];
                    count += checked ? 1 : -1;
                    auto *cb = dynamic_cast<CheckBox *>(m_filament_sizers[filament.first]->GetItem(size_t(0))->GetSizer()->GetItem(size_t(0))->GetWindow());
                    if (count == 0) {
                        cb->SetValue(false);
                        cb->SetHalfChecked(false);
                        m_checked_filaments.erase(filament.first);
                    } else if (count == filament.second.size()) {
                        cb->SetValue(true);
                        cb->SetHalfChecked(false);
                    } else {
                        cb->SetValue(false);
                        cb->SetHalfChecked(true);
                    }
                    break;
                }
            }
        }
        update_checked();
    }
}

void UserPresetsDialog::on_filament_checked(std::string const &filament, bool checked, bool from_user)
{
    auto iter = m_filament_presets.find(filament);
    for (auto &preset : iter->second)
        on_preset_checked(preset, checked, false);
    if (checked)
        m_checked_filaments[filament] = iter->second.size();
    else
        m_checked_filaments.erase(filament);
    if (!from_user) {
        auto *cb = dynamic_cast<CheckBox *>(m_filament_sizers[filament]->GetItem(size_t(0))->GetSizer()->GetItem(size_t(0))->GetWindow());
        cb->SetValue(checked);
        cb->SetHalfChecked(false);
    } else {
        update_checked();
    }
}

void UserPresetsDialog::on_all_checked(bool checked, bool from_user)
{
    if (is_filament_list()) {
        for (auto &filament : m_filament_presets)
            on_filament_checked(filament.first, checked, false);
    } else {
        auto & presets = m_presets[m_collection];
        for (auto &preset : presets)
            on_preset_checked(preset, checked, false);
    }
    if (!from_user)
        m_check_all->SetValue(checked);
    update_checked();
}

void UserPresetsDialog::update_preset_counts()
{
    wxString labels[] = {_L("Printer presets (%d/%d)"), _L("Filament presets (%d/%d)"), _L("Process presets (%d/%d)")};
    int capacities[] = {20, 100, 200};
    for (int i = 0; i < 3; ++i) {
        size_t n = i == 1 ? std::accumulate(m_filament_presets.begin(), m_filament_presets.end(), size_t(0),
            [](size_t t, auto &filament) { return t + filament.second.size(); }) : 0;
        if (m_preset_sizers.empty()) {
            m_tab_ctrl->SetItemTextColour(i, wxColour("#262E30"));
            m_tab_ctrl->SetItemPaddingSize(i, {FromDIP(20), FromDIP(4)});
        }
        m_tab_ctrl->SetItemText(i, wxString::Format(labels[i], int(m_presets[i].size() + n), capacities[i]));
    }
}

void UserPresetsDialog::update_checked()
{
    size_t total = 0;
    if (is_filament_list()) {
        total = std::accumulate(m_filament_presets.begin(), m_filament_presets.end(), total,
            [](size_t t, auto &filament) { return t + filament.second.size(); });
    } else {
        total = m_presets[m_collection].size();
    }
    size_t count = m_checked_presets.size();
    if (count == 0) {
        m_check_all->SetValue(false);
        m_check_all->SetHalfChecked(false);
    } else if (count == total) {
        m_check_all->SetValue(true);
        m_check_all->SetHalfChecked(false);
    } else {
        m_check_all->SetValue(false);
        m_check_all->SetHalfChecked(true);
    }
    m_label_check_count->SetLabel(count > 0 ? wxString::Format(_L("%u Selected"), count) : "");
    m_button_delete->Enable(count > 0);
    m_button_delete->SetToolTip(count > 0 ? "" : _L("Please select the preset to be deleted"));
}

void UserPresetsDialog::delete_checked()
{
    if (!delete_presets(m_collection + 3, m_checked_presets)) // check only
        return;

    // Collect checked sizer of presets (need m_checked_presets, so do it befor delete_presets)
    std::set<wxSizer*> checked_sizers;
    for (auto &preset : m_checked_presets) {
        auto iter = m_preset_sizers.find(preset);
        checked_sizers.insert(iter->second);
        m_preset_sizers.erase(iter);
    }

    delete_presets(m_collection, m_checked_presets); // real delete

    // Collect checked sizer of filaments
    if (is_filament_list()) {
        for (auto &filament : m_checked_filaments) {
            auto iter = m_filament_presets.find(filament.first);
            auto iter2 = m_filament_sizers.find(filament.first);
            if (iter == m_filament_presets.end()) {
                // Collect checked sizer (filament removed)
                checked_sizers.insert(iter2->second);
                while (iter2->second->GetItemCount() > 1)
                    iter2->second->Detach(1);
                m_filament_sizers.erase(iter2);
            } else {
                for (auto sizer : checked_sizers)
                    iter2->second->Detach(sizer);
                // Update check box
                auto *cb = dynamic_cast<CheckBox *>(iter2->second->GetItem(size_t(0))->GetSizer()->GetItem(size_t(0))->GetWindow());
                cb->SetValue(false);
                cb->SetHalfChecked(false);
            }
        }
        m_checked_filaments.clear();
    }

    update_preset_counts();
    layout_preset_list();
    update_checked();

    for (auto sizer : checked_sizers) {
        sizer->DeleteWindows();
        m_hiden_sizers.erase(sizer);
        delete sizer;
    }
}

}}

#include "Tab.hpp"
#include "MsgDialog.hpp"

namespace Slic3r {
namespace GUI {

static void find_compatible_user_presets(PresetCollection const &collection, std::string printer, std::vector<std::string> &presets)
{
    for (auto& preset : collection) {
        if (!preset.is_user()) continue;
        auto *compatible_printers = dynamic_cast<const ConfigOptionStrings *>(preset.config.option("compatible_printers"));
        if (compatible_printers &&
                std::find(compatible_printers->values.begin(), compatible_printers->values.end(), printer) != compatible_printers->values.end())
            presets.insert(std::lower_bound(presets.begin(), presets.end(), preset.name), preset.name);
    }
}

static void remove_both(std::vector<std::string> &l, std::vector<std::string> &r)
{
    auto i = l.begin();
    auto j = r.begin();
    while (i != l.end() && j != r.end()) {
        if (*i == *j) {
            i = l.erase(i);
            j = r.erase(j);
        } else if (*i < *j) {
            ++i;
        } else {
            ++j;
        }
    }
}

bool UserPresetsDialog::delete_confirm(int collection, int preset_num)
{
    wxString types[] = {_L("Printer"), _L("Filament"), _L("Process")};
    DeleteConfirmDialog dlg(this, wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Delete"),
                            wxString::Format(_L("%d %s Preset will be deleted."), preset_num, types[collection % 3]));
    int res = dlg.ShowModal();
    return res == wxID_OK;
}

bool UserPresetsDialog::delete_confirm(int collection, int filament_preset_num, int print_preset_num)
{
    DeleteConfirmDialog
        dlg(this, wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Delete"),
            wxString::Format(_L("%d Filament Preset and %d Process Preset is attached to this printer. Those presets would be deleted if the printer is deleted."),
                             filament_preset_num, print_preset_num));
    int res = dlg.ShowModal();
    return res == wxID_OK;
}

bool UserPresetsDialog::delete_presets(int collection, std::vector<std::string> &presets)
{
    Preset::Type types[] = {Preset::TYPE_PRINTER, Preset::TYPE_FILAMENT, Preset::TYPE_PRINT};
    Tab *tab = wxGetApp().get_tab(types[collection % 3]);
    auto collection2 = tab->get_presets();
    // Find attached filaments & print presets for custom printers and delete together
    if (collection == 3) {
        auto filament_presets = std::make_shared<std::vector<std::string>>();
        auto print_presets = std::make_shared<std::vector<std::string>>();
        for (auto &preset : presets) {
            auto preset2 = collection2->find_preset(preset);
            if (!preset2->is_system && collection2->get_preset_base(*preset2) == preset2) { // Root printer preset
                find_compatible_user_presets(wxGetApp().preset_bundle->filaments, preset, *filament_presets);
                find_compatible_user_presets(wxGetApp().preset_bundle->prints, preset, *print_presets);
            }
        }
        if (!filament_presets->empty() || !print_presets->empty()) {
            if (!delete_confirm(collection, int(filament_presets->size()), int(print_presets->size())))
                return false;
            // Remove filaments & print presets attached to current custom printer
            auto current = tab->get_presets()->get_edited_preset().name;
            auto iter = std::lower_bound(presets.begin(), presets.end(), current);
            if (iter != presets.end() && *iter == current) {
                auto preset2 = collection2->find_preset(current);
                if (!preset2->is_system && collection2->get_preset_base(*preset2) == preset2) {
                    std::vector<std::string> filament_presets2;
                    std::vector<std::string> print_presets2;
                    find_compatible_user_presets(wxGetApp().preset_bundle->filaments, current, filament_presets2);
                    find_compatible_user_presets(wxGetApp().preset_bundle->prints, current, print_presets2);
                    remove_both(*filament_presets, filament_presets2);
                    remove_both(*print_presets, print_presets2);
                }
            }
            CallAfter([this, filament_presets, print_presets] {
                delete_presets(1, *filament_presets);
                delete_presets(2, *print_presets);
                update_preset_counts();
            });
            return true;
        }
    }
    if (collection >= 3) {
        return delete_confirm(collection, int(presets.size()));
    }

    // Delete current specially
    auto current = tab->get_presets()->get_edited_preset().name;
    auto iter    = std::lower_bound(presets.begin(), presets.end(), current);
    if (iter != presets.end() && *iter == current)
        iter = presets.erase(iter);
    else
        current.clear();

    // Delete all not current
    for (auto &preset : presets) {
        auto preset2 = collection2->find_preset(preset);
        if (!preset2->setting_id.empty()) {
            BOOST_LOG_TRIVIAL(info) << "delete preset = " << preset << ", setting_id = " << preset2->setting_id;
            collection2->set_sync_info_and_save(preset, preset2->setting_id, "delete", 0);
            wxGetApp().delete_preset_from_cloud(preset2->setting_id);
        }
        collection2->delete_preset(preset);
    }
    // Delete current
    if (!current.empty())
        tab->select_preset("", true);
    else
        wxGetApp().plater()->sidebar().update_presets(collection2->type());

    // Remove from preset/filament list
    if (!current.empty())
        presets.insert(iter, current);
    remove_both(m_presets[collection], presets);
    if (collection == 1) {
        auto iter = m_filament_presets.begin();
        for (; iter != m_filament_presets.end(); ) {
            remove_both(iter->second, presets);
            if (iter->second.empty())
                iter = m_filament_presets.erase(iter);
            else
                ++iter;
        }
    }
    assert(presets.empty());
    return true;
}

}}