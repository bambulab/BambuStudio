#include "FilamentCatalogDialog.hpp"
#include "FilamentCatalog.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "FilamentBitmapUtils.hpp"
#include "libslic3r/PresetBundle.hpp"
#include <wx/srchctrl.h>
#include <wx/listctrl.h>
#include <wx/imaglist.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/numformatter.h>
#include <wx/sizer.h>
#include <wx/settings.h>
#include <wx/msgdlg.h>
#include <algorithm>

namespace Slic3r { namespace GUI {

static bool profile_matches_slot(PresetBundle& bundle, size_t slot, const Preset& preset)
{
    if (!preset.is_compatible || preset.is_default) return false;
    if (!multi_nozzle_diameter_mode_enabled(bundle.project_config)) return true;
    const Preset& printer = bundle.printers.get_edited_preset();
    std::string diameter = printer.config.opt_string("printer_variant");
    const auto* map = bundle.project_config.option<ConfigOptionStrings>("filament_diameter_map");
    if (map && slot < map->values.size() && !map->values[slot].empty()) diameter = map->values[slot];
    const auto* printers = preset.config.option<ConfigOptionStrings>("compatible_printers");
    if (printers) for (const auto& name : printers->values) {
        const Preset* candidate = bundle.printers.find_preset(name, false);
        if (candidate && candidate->config.opt_string("printer_model") == printer.config.opt_string("printer_model") &&
            candidate->config.opt_string("printer_variant") == diameter) return true;
    }
    return false;
}

std::string find_catalog_profile(PresetBundle& bundle, size_t slot, const wxString& id)
{
    for (const Preset& preset : bundle.filaments.get_presets())
        if (preset.is_system && preset.filament_id == id.ToStdString() && profile_matches_slot(bundle, slot, preset))
            return preset.name;
    return {};
}

std::string find_inventory_profile(PresetBundle& bundle, size_t slot, const FilamentSpool& spool)
{
    const wxString brand = filament_vendor_key(spool.brand);
    if (brand.empty()) return {};
    const wxString material = inventory_material_name(spool).Lower();
    std::string by_name;
    for (const Preset& preset : bundle.filaments.get_presets()) {
        if (!profile_matches_slot(bundle, slot, preset)) continue;
        const auto* vendors = preset.config.option<ConfigOptionStrings>("filament_vendor");
        if (!vendors || vendors->values.empty() || filament_vendor_key(vendors->values.front()) != brand) continue;
        if (!spool.filament_id.empty() && (preset.filament_id == spool.filament_id || preset.setting_id == spool.filament_id))
            return preset.name;
        // Some manually entered spools have no usable ID. An exact vendor + family
        // name is a safe fallback; do not use fuzzy names to select print settings.
        wxString alias = wxString::FromUTF8(preset.alias.empty() ? preset.name : preset.alias).BeforeFirst('@').Trim().Trim(false).Lower();
        wxString prefix = wxString::FromUTF8(vendors->values.front()).Trim().Trim(false).Lower();
        if (alias.StartsWith(prefix + " ")) alias = alias.Mid(prefix.size() + 1);
        else if (brand == "bambu lab" && alias.StartsWith("bambu ")) alias = alias.Mid(6);
        if (!material.empty() && (alias == material || alias == brand + " " + material) && by_name.empty()) by_name = preset.name;
    }
    return by_name;
}

static wxString remaining_label(const InventoryRemaining& remaining)
{
    if (remaining.spools == 0) return wxString(); // Catalog entry not present in inventory.
    const wxString grams = wxNumberFormatter::ToString(remaining.grams, 0, wxNumberFormatter::Style_WithThousandsSep);
    if (remaining.spools == 1)
        return remaining.unknown ? _L("Unknown") : wxString::Format(_L("≈%s g"), grams);
    if (remaining.unknown == remaining.spools)
        return wxString::Format(_L("%zu spools · Unknown"), remaining.spools);
    if (remaining.unknown > 0)
        return wxString::Format(_L("%zu spools · ≈%s g + unknown"), remaining.spools, grams);
    return wxString::Format(_L("%zu spools · ≈%s g total"), remaining.spools, grams);
}

FilamentCatalogDialog::FilamentCatalogDialog(wxWindow* parent, PresetBundle& bundle, size_t slot)
    : wxDialog(parent ? wxGetTopLevelParent(parent) : nullptr, wxID_ANY, _L("Find Filament"), wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    auto* layout = new wxBoxSizer(wxVERTICAL);
    auto* intro = new wxStaticText(this, wxID_ANY,
        _L("Choose a color and its filament profile for this project slot. Load the spool into your printer when you are ready to print."));
    intro->Wrap(FromDIP(700));
    layout->Add(intro, 0, wxEXPAND | wxALL, FromDIP(12));
    m_search = new wxSearchCtrl(this, wxID_ANY);
    m_search->SetDescriptiveText(_L("Search name, brand, material, product code or hex"));
    m_search->ShowCancelButton(true);
    layout->Add(m_search, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));
    m_mine = new wxCheckBox(this, wxID_ANY, _L("My filaments only"));
    m_mine->SetToolTip(_L("Show active spools of any brand recorded in Filament Manager, whether or not they are loaded in a printer."));
    layout->Add(m_mine, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
    m_status = new wxStaticText(this, wxID_ANY, "");
    layout->Add(m_status, 0, wxALL, FromDIP(12));
    m_results = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(1170, 360)),
                               wxLC_REPORT | wxLC_SINGLE_SEL);
    m_results->AppendColumn(_L("Color"), wxLIST_FORMAT_LEFT, FromDIP(220));
    m_results->AppendColumn(_L("Filament"), wxLIST_FORMAT_LEFT, FromDIP(210));
    m_results->AppendColumn(_L("Product code"), wxLIST_FORMAT_LEFT, FromDIP(125));
    m_results->AppendColumn(_L("Profile"), wxLIST_FORMAT_LEFT, FromDIP(170));
    m_results->AppendColumn(_L("Remaining"), wxLIST_FORMAT_LEFT, FromDIP(220));
    m_results->AppendColumn(_L("Notes"), wxLIST_FORMAT_LEFT, FromDIP(200));
    auto* images = new wxImageList(FromDIP(28), FromDIP(28), true);
    m_results->AssignImageList(images, wxIMAGE_LIST_SMALL);
    auto* query = wxGetApp().get_filament_color_code_query();
    if (query) for (const auto& family : query->GetFilamentFamilies()) {
        const std::string profile = find_catalog_profile(bundle, slot, family.first);
        for (const auto& item : *family.second->GetFilamentColor2CodeMap()) {
            auto color = item.second->GetFilaColor();
            if (color.GetColors().empty()) continue;
            const int image = images->Add(create_filament_bitmap(color.GetColors(), FromDIP(wxSize(28, 28)),
                color.m_color_type == FilamentColor::ColorType::GRADIENT_CLR));
            Entry entry;
            entry.info = item.second;
            entry.preset_name = profile;
            entry.color = color;
            entry.name = item.second->GetFilaColorName();
            entry.material = "Bambu " + item.second->GetFilaType();
            entry.product_code = item.second->GetFilaColorCode();
            entry.search_text = filament_catalog_search_text(*item.second);
            entry.image = image;
            m_entries.push_back(std::move(entry));
        }
    }
    auto* store = wxGetApp().fila_manager_store();
    m_mine->Enable(store != nullptr);
    if (!store) m_mine->SetToolTip(_L("Filament Manager inventory is not available."));
    if (store) for (const auto& id : store->all_spool_ids()) {
        const auto* spool = store->get_spool(id);
        if (!spool || !inventory_spool_available(*spool)) continue;
        Entry entry;
        entry.inventory = true;
        entry.remaining.add(*spool);
        entry.color = inventory_filament_color(*spool);
        entry.preset_name = find_inventory_profile(bundle, slot, *spool);
        entry.material = wxString::FromUTF8(spool->brand) + " " + inventory_material_name(*spool);
        entry.name = wxString::FromUTF8(spool->color_name);
        entry.note = entry.notes.add(wxString::FromUTF8(spool->note));
        wxString hex;
        for (const auto& value : filament_catalog_colors(entry.color)) {
            if (!hex.empty()) hex += ", ";
            hex += wxString::FromUTF8(value);
        }
        if (query && filament_vendor_key(spool->brand) == "bambu lab") {
            wxString family = wxString::FromUTF8(spool->filament_id);
            if (const Preset* preset = bundle.filaments.find_preset(entry.preset_name, false))
                family = wxString::FromUTF8(preset->filament_id);
            entry.info = query->GetFilaInfo(family, entry.color);
            if (entry.info) {
                entry.name = entry.info->GetFilaColorName();
                entry.product_code = entry.info->GetFilaColorCode();
            }
        }
        // Mark the existing catalog row as owned rather than duplicating it once
        // per physical spool. Other brands and custom colors remain inventory rows.
        if (entry.info) {
            auto catalog = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& item) { return item.info == entry.info; });
            if (catalog != m_entries.end()) {
                catalog->inventory = true;
                catalog->remaining.add(*spool);
                catalog->search_text += " " + entry.note;
                catalog->note = catalog->notes.add(entry.note);
                continue;
            }
        }
        if (entry.name.empty()) entry.name = hex.empty() ? _L("Unknown color") : hex;
        else if (!hex.empty()) entry.name += " (" + hex + ")";
        entry.search_text = entry.name + " " + entry.material + " " + entry.product_code + " " + entry.note;
        if (entry.info) for (const auto& name : entry.info->GetColorNames()) entry.search_text += " " + name.second;
        if (!entry.color.GetColors().empty())
            entry.image = images->Add(create_filament_bitmap(entry.color.GetColors(), FromDIP(wxSize(28, 28)),
                entry.color.m_color_type == FilamentColor::ColorType::GRADIENT_CLR));
        m_entries.push_back(std::move(entry));
    }
    std::sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
        const int order = a.name.CmpNoCase(b.name);
        if (order != 0) return order < 0;
        return (a.material + a.product_code + a.note) < (b.material + b.product_code + b.note);
    });
    layout->Add(m_results, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));
    m_choice = new wxStaticText(this, wxID_ANY, "");
    m_choice->SetMinSize(FromDIP(wxSize(-1, 48)));
    layout->Add(m_choice, 0, wxEXPAND | wxALL, FromDIP(12));
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer();
    buttons->Add(new wxButton(this, wxID_CANCEL, _L("Cancel")), 0, wxRIGHT, FromDIP(8));
    m_apply = new wxButton(this, wxID_OK, _L("Apply"));
    buttons->Add(m_apply);
    layout->Add(buttons, 0, wxEXPAND | wxALL, FromDIP(12));
    SetSizerAndFit(layout);
    SetMinSize(FromDIP(wxSize(650, 480)));
    m_mine->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { filter(); });
    m_search->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { filter(); });
    m_search->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_DOWN && !m_visible.empty()) {
            m_results->SetFocus();
            m_results->SetItemState(0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                      wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
        } else event.Skip();
    });
    m_search->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, [this](wxCommandEvent&) { m_search->SetValue(""); });
    m_results->Bind(wxEVT_LIST_COL_CLICK, [this](wxListEvent& event) {
        const int column = event.GetColumn();
        if (column < 0 || column >= m_results->GetColumnCount()) return;
        m_sort_ascending = column == m_sort_column ? !m_sort_ascending : true;
        m_sort_column = column;
        filter();
    });
    m_results->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { update_selection(); });
    m_results->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](wxListEvent&) { update_selection(); });
    m_results->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) {
        const long row = event.GetIndex();
        if (row < 0 || static_cast<size_t>(row) >= m_visible.size()) return;
        m_results->SetItemState(row, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        apply_selection();
    });
    m_apply->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { apply_selection(); });
    filter();
    wxGetApp().UpdateDarkUIWin(this);
    CentreOnParent();
    m_search->SetFocus();
}

void FilamentCatalogDialog::filter()
{
    // Keep the selected entry, not its old row number, when sorting/filtering.
    const long selected_row = m_results->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    const size_t selected_entry = selected_row >= 0 && static_cast<size_t>(selected_row) < m_visible.size() ?
        m_visible[selected_row] : m_entries.size();
    m_results->Freeze();
    m_results->DeleteAllItems();
    m_visible.clear();
    for (size_t i = 0; i < m_entries.size(); ++i) {
        const auto& entry = m_entries[i];
        if (m_mine->GetValue() && !entry.inventory) continue;
        if (!filament_search_matches(entry.search_text, m_search->GetValue())) continue;
        m_visible.push_back(i);
    }
    std::stable_sort(m_visible.begin(), m_visible.end(), [this](size_t left, size_t right) {
        const auto& a = m_entries[left];
        const auto& b = m_entries[right];
        if (m_sort_column == 4)
            return compare_inventory_remaining(a.remaining, b.remaining, m_sort_ascending) < 0;
        int order = 0;
        if (m_sort_column == 3) {
            order = a.preset_name.empty() == b.preset_name.empty() ? 0 : a.preset_name.empty() ? 1 : -1;
        } else {
            const wxString& av = m_sort_column == 1 ? a.material : m_sort_column == 2 ? a.product_code : m_sort_column == 5 ? a.note : a.name;
            const wxString& bv = m_sort_column == 1 ? b.material : m_sort_column == 2 ? b.product_code : m_sort_column == 5 ? b.note : b.name;
            if (av.empty() != bv.empty()) return !av.empty();
            order = av.CmpNoCase(bv);
        }
        return m_sort_ascending ? order < 0 : order > 0;
    });
    const wxString headings[] = {_L("Color"), _L("Filament"), _L("Product code"), _L("Profile"), _L("Remaining"), _L("Notes")};
    for (int column = 0; column < m_results->GetColumnCount(); ++column) {
        wxListItem header;
        header.SetText(headings[column] + (column == m_sort_column ?
            (m_sort_ascending ? wxString::FromUTF8(" ↑") : wxString::FromUTF8(" ↓")) : wxString::FromUTF8(" ↕")));
        m_results->SetColumn(column, header);
    }
    long new_selection = -1;
    for (size_t index : m_visible) {
        const auto& entry = m_entries[index];
        const long row = m_results->InsertItem(m_results->GetItemCount(), entry.name, entry.image);
        m_results->SetItem(row, 1, entry.material);
        m_results->SetItem(row, 2, entry.product_code);
        m_results->SetItem(row, 3, entry.preset_name.empty() ? _L("No matching profile") : _L("Profile found"));
        m_results->SetItem(row, 4, remaining_label(entry.remaining));
        wxString note = entry.note;
        note.Replace("\r", " ");
        note.Replace("\n", " ");
        m_results->SetItem(row, 5, note);
        if (entry.preset_name.empty() || entry.color.GetColors().empty())
            m_results->SetItemTextColour(row, wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        if (index == selected_entry) new_selection = row;
    }
    if (new_selection >= 0) {
        m_results->SetItemState(new_selection, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                             wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
        m_results->EnsureVisible(new_selection);
    }
    m_results->Thaw();
    m_status->SetLabel(m_mine->GetValue() ?
        wxString::Format(_L("My filaments: %zu matches"), m_visible.size()) :
        wxString::Format(_L("Bambu catalog and my filaments: %zu matches"), m_visible.size()));
    update_selection();
}

FilamentCatalogChoice FilamentCatalogDialog::selection() const
{
    long row = m_results->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (row < 0 || static_cast<size_t>(row) >= m_visible.size()) return {};
    const auto& entry = m_entries[m_visible[row]];
    return {entry.color, entry.preset_name};
}

void FilamentCatalogDialog::apply_selection()
{
    const long row = m_results->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (row < 0 || static_cast<size_t>(row) >= m_visible.size()) return;
    const auto choice = selection();
    if (choice.color.GetColors().empty()) {
        wxMessageBox(_L("This spool has no valid saved color. Edit it in Filament Manager before applying it."),
                     _L("Cannot apply filament"), wxOK | wxICON_INFORMATION, this);
    } else if (choice.preset_name.empty()) {
        wxMessageBox(_L("No installed profile matches this filament and the selected printer and nozzle."),
                     _L("Cannot apply filament"), wxOK | wxICON_INFORMATION, this);
    } else {
        EndModal(wxID_OK);
    }
}

void FilamentCatalogDialog::update_selection()
{
    long row = m_results->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    m_apply->Enable(false);
    if (row < 0 || static_cast<size_t>(row) >= m_visible.size()) {
        m_choice->SetLabel("");
        Layout();
        return;
    }
    const auto& entry = m_entries[m_visible[row]];
    m_apply->Enable(!entry.preset_name.empty() && !entry.color.GetColors().empty());
    m_choice->SetLabel(entry.color.GetColors().empty() ?
        _L("This spool has no valid saved color. Edit it in Filament Manager before applying it.") : entry.preset_name.empty() ?
        _L("No installed profile matches this filament and the selected printer and nozzle.") :
        wxString());
    if (!entry.preset_name.empty()) {
        const wxString separator = m_choice->GetLabel().empty() ? "" : "\n";
        m_choice->SetLabel(m_choice->GetLabel() + separator +
                          wxString::Format(_L("Profile: %s"), wxString::FromUTF8(entry.preset_name)));
    }
    if (!entry.note.empty()) m_choice->SetLabel(m_choice->GetLabel() + "\n" + entry.note);
    m_choice->Wrap(std::max(FromDIP(100), GetClientSize().GetWidth() - FromDIP(24)));
    Layout();
}

}} // namespace Slic3r::GUI
