#include "PresetComboBoxes.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/LocalesUtils.hpp"

#include <cstddef>
#include <cstdlib>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>
#include <string>
#include <algorithm>
#include <tuple>
#include <boost/algorithm/string.hpp>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/statbox.h>
#include <wx/colordlg.h>
#include <wx/wupdlock.h>
#include <wx/menu.h>
#include <wx/odcombo.h>
#include <wx/listbook.h>

#ifdef _WIN32
#include <wx/msw/dcclient.h>
#include <wx/msw/private.h>
#endif

#include "libslic3r/libslic3r.h"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Tab.hpp"
#include "ConfigWizard.hpp"
#include "../Utils/ASCIIFolding.hpp"
#include "../Utils/FixModelByWin10.hpp"
#include "../Utils/UndoRedo.hpp"
#include "../Utils/ColorSpaceConvert.hpp"
#include "BitmapCache.hpp"
#include "SavePresetDialog.hpp"
#include "MsgDialog.hpp"
#include "ParamsDialog.hpp"
#include "FilamentPickerDialog.hpp"
#include "wxExtensions.hpp"

#include "DeviceCore/DevManager.h"
#include "libslic3r/Log.hpp"

// A workaround for a set of issues related to text fitting into gtk widgets:
#if defined(__WXGTK20__) || defined(__WXGTK3__)
    #include <glib-2.0/glib-object.h>
    #include <pango-1.0/pango/pango-layout.h>
    #include <gtk/gtk.h>
#endif

using Slic3r::GUI::format_wxstr;

namespace Slic3r {
namespace GUI {

#define BORDER_W 10

int tray_icon_width(const std::map<int, DynamicPrintConfig> &filament_ams_list)
{
    size_t max{3};
    for (auto &[_, tray] : filament_ams_list) {
        auto name = tray.opt_string("tray_name", 0u);
        max       = std::max(max, name.size());
    }

    return max * 8;
}

wxBitmap *tray_color_icon(const DynamicPrintConfig &tray, const std::string &name, int icon_width)
{
    auto color       = tray.opt_string("filament_colour", 0u);
    auto multi_color = tray.opt<ConfigOptionStrings>("filament_multi_colour")->values;
    auto ctype_str   = tray.opt_string("filament_colour_type", 0u);
    return (multi_color.size() > 1) ? get_extruder_color_icon(multi_color, ctype_str == "0", name, icon_width, 16) : get_extruder_color_icon(color, name, icon_width, 16);
}

static bool supports_multi_nozzle_diameter_mode(const PresetCollection& printers, const Preset& preset)
{
    const Preset* base = printers.get_preset_base(preset);
    return base != nullptr && base->is_system && is_single_ext_multi_nozzle(base->config);
}

static bool is_canonical_multi_nozzle_diameter_printer_preset(const PresetCollection& printers, const Preset& preset)
{
    const Preset* base = printers.get_preset_base(preset);
    return base != nullptr && base->is_system &&
        is_single_ext_multi_nozzle(base->config) &&
        base->config.opt_string("printer_variant") == canonical_multi_nozzle_printer_variant();
}

static bool numeric_text(const std::string& text)
{
    if (text.empty())
        return false;
    // Not strtod: under a comma-decimal UI locale (e.g. French) it stops at the '.' in "0.4".
    size_t end = 0;
    string_to_double_decimal_point(text, &end);
    return end == text.size();
}

// Display-only rewrite: "Bambu Lab H3C 0.4 nozzle" -> "Bambu Lab H3C Multiple nozzle". Heuristic on the
// name shape - a name that does not end in "<number> nozzle" is returned untouched, because showing the
// real name is always better than inventing a wrong one. Never touches Preset::name itself.
static std::string multi_nozzle_printer_preset_name(const Preset& preset)
{
    std::string name = preset.name;

    const std::string nozzle_suffix = " nozzle";
    if (boost::algorithm::ends_with(name, nozzle_suffix)) {
        const size_t suffix_pos = name.size() - nozzle_suffix.size();
        const size_t diameter_pos = name.rfind(' ', suffix_pos - 1);
        if (diameter_pos != std::string::npos &&
            numeric_text(name.substr(diameter_pos + 1, suffix_pos - diameter_pos - 1)))
            name.replace(diameter_pos + 1, name.size() - diameter_pos - 1, "Multiple nozzle");
    }

    return name;
}

// Display-only rewrite: "0.08mm High Quality @BBL H3C" -> "Multi-diameter 0.08mm High Quality". Same stance
// as the printer rewrite: a name without an "@<machine>" tail is returned untouched. Never touches
// Preset::name itself.
static std::string multi_nozzle_process_preset_name(const Preset& preset)
{
    const size_t at_pos = preset.name.find('@');
    if (at_pos == std::string::npos)
        return preset.name;
    std::string quality = preset.name.substr(0, at_pos);
    boost::algorithm::trim(quality);
    return quality.empty() ? preset.name : "Multi-diameter " + quality;
}

// Only the printer gets relabelled, and only on the canonical variant: renaming every diameter would make
// a printer that some path wrongly re-pointed indistinguishable from the baseline the mode is supposed to
// slice against. User copies keep their real name for the same reason.
static bool use_multi_nozzle_printer_display_name(const PresetBundle* preset_bundle, Preset::Type type, const Preset& preset)
{
    return type == Preset::TYPE_PRINTER && preset_bundle != nullptr && preset.is_system &&
           multi_nozzle_diameter_mode_enabled(preset_bundle->project_config) &&
           is_canonical_multi_nozzle_diameter_printer_preset(preset_bundle->printers, preset);
}

// Process follows the printer: only system processes bound to the canonical printer are relabelled, and
// only while that printer is the active one. Processes of the sibling diameters keep their real names -
// with the machine tail stripped they would collapse onto the canonical ones ("... @BBL H3C 0.2 nozzle"
// and "... @BBL H3C" both become "Multi-diameter ..."), and the lists key their items by label.
static bool use_multi_nozzle_process_display_name(const PresetBundle* preset_bundle, Preset::Type type, const Preset& preset)
{
    if (type != Preset::TYPE_PRINT || preset_bundle == nullptr || !preset.is_system ||
        !multi_nozzle_diameter_mode_enabled(preset_bundle->project_config))
        return false;

    const PresetCollection& printers = preset_bundle->printers;
    const Preset&           printer  = printers.get_edited_preset();
    if (!is_canonical_multi_nozzle_diameter_printer_preset(printers, printer))
        return false;

    const Preset* printer_base        = printers.get_preset_base(printer);
    const auto*   compatible_printers = preset.config.option<ConfigOptionStrings>("compatible_printers");
    return compatible_printers != nullptr &&
           std::find(compatible_printers->values.begin(), compatible_printers->values.end(), printer_base->name) !=
               compatible_printers->values.end();
}

// Label multi-diameter mode gives a preset, or an empty string when the preset shows its real name.
static std::string multi_nozzle_display_name(const PresetBundle* preset_bundle, Preset::Type type, const Preset& preset)
{
    if (use_multi_nozzle_printer_display_name(preset_bundle, type, preset))
        return multi_nozzle_printer_preset_name(preset);
    if (use_multi_nozzle_process_display_name(preset_bundle, type, preset))
        return multi_nozzle_process_preset_name(preset);
    return {};
}

// Multi-diameter slicing anchors the project on the canonical printer variant, so in the compare dialog
// the sibling system diameters of that machine are hidden while the mode is on. User and project presets,
// other printer models, and machines that cannot do multi-diameter at all are untouched.
static bool hidden_by_multi_nozzle_diameter_mode(const PresetBundle*     preset_bundle,
                                                 const PresetCollection& collection,
                                                 Preset::Type            type,
                                                 const Preset&           preset)
{
    if (type != Preset::TYPE_PRINTER || preset_bundle == nullptr || !preset.is_system)
        return false;
    if (!multi_nozzle_diameter_mode_enabled(preset_bundle->project_config))
        return false;
    return supports_multi_nozzle_diameter_mode(collection, preset) &&
           !is_canonical_multi_nozzle_diameter_printer_preset(collection, preset);
}

// Whether `preset` is an installed system printer of a single-extruder multi-nozzle model, so the printer
// settings combo offers it across multi-diameter modes whatever printer model is currently selected.
// In multi-diameter mode only the edited printer's model qualifies: leaving the mode restores within the
// same model, so a ">single" target on another model would land on the wrong printer.
static bool installed_multi_nozzle_system_printer(const PresetCollection& printers, const Preset& preset, bool multi_nozzle_mode)
{
    if (!preset.is_system || !is_single_ext_multi_nozzle(preset.config))
        return false;
    else if (preset.is_visible)
        return !multi_nozzle_mode ||
               preset.config.opt_string("printer_model") == printers.get_edited_preset().config.opt_string("printer_model");
    return false;
}

// ---------------------------------
// ***  PresetComboBox  ***
// ---------------------------------

/* For PresetComboBox we use bitmaps that are created from images that are already scaled appropriately for Retina
 * (Contrary to the intuition, the `scale` argument for Bitmap's constructor doesn't mean
 * "please scale this to such and such" but rather
 * "the wxImage is already sized for backing scale such and such". )
 * Unfortunately, the constructor changes the size of wxBitmap too.
 * Thus We need to use unscaled size value for bitmaps that we use
 * to avoid scaled size of control items.
 * For this purpose control drawing methods and
 * control size calculation methods (virtual) are overridden.
 **/

PresetComboBox::PresetComboBox(wxWindow* parent, Preset::Type preset_type, const wxSize& size, PresetBundle* preset_bundle/* = nullptr*/) :
    ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, size, 0, nullptr, wxCB_READONLY),
    m_type(preset_type),
    m_last_selected(wxNOT_FOUND),
    m_em_unit(em_unit(this)),
    m_preset_bundle(preset_bundle ? preset_bundle : wxGetApp().preset_bundle)
{
#ifdef __WXMSW__
    if (preset_type == Preset::TYPE_FILAMENT)
        SetFont(Label::Body_13);
#endif // __WXMSW__

    switch (m_type)
    {
    case Preset::TYPE_PRINT: {
        m_collection = &m_preset_bundle->prints;
        m_main_bitmap_name = "cog";
        break;
    }
    case Preset::TYPE_FILAMENT: {
        m_collection = &m_preset_bundle->filaments;
        m_main_bitmap_name = "spool";
        break;
    }
    case Preset::TYPE_SLA_PRINT: {
        m_collection = &m_preset_bundle->sla_prints;
        m_main_bitmap_name = "cog";
        break;
    }
    case Preset::TYPE_SLA_MATERIAL: {
        m_collection = &m_preset_bundle->sla_materials;
        m_main_bitmap_name = "blank_16";
        break;
    }
    case Preset::TYPE_PRINTER: {
        m_collection = &m_preset_bundle->printers;
        m_main_bitmap_name = "printer";
        break;
    }
    default: break;
    }

    m_bitmapCompatible   = ScalableBitmap(this, "flag_green");
    m_bitmapIncompatible = ScalableBitmap(this, "flag_red");

    // parameters for an icon's drawing
    fill_width_height();
    Bind(wxEVT_COMBOBOX, &PresetComboBox::OnSelect, this);
}

void PresetComboBox::OnSelect(wxCommandEvent& evt)
{
    // Under OSX: in case of use of a same names written in different case (like "ENDER" and "Ender")
    // m_presets_choice->GetSelection() will return first item, because search in PopupListCtrl is case-insensitive.
    // So, use GetSelection() from event parameter
    auto selected_item = evt.GetSelection();

    auto marker = reinterpret_cast<Marker>(this->GetClientData(selected_item));
    if (marker >= LABEL_ITEM_DISABLED && marker < LABEL_ITEM_MAX)
        this->SetSelection(m_last_selected);
    else if (on_selection_changed && (m_last_selected != selected_item || m_collection->current_is_dirty())) {
        m_last_selected = selected_item;
        on_selection_changed(selected_item);
        evt.StopPropagation();
    }
    evt.Skip();
}

PresetComboBox::~PresetComboBox()
{
}

BitmapCache& PresetComboBox::bitmap_cache()
{
    static BitmapCache bmps;
    return bmps;
}

void PresetComboBox::set_label_marker(int item, LabelItemType label_item_type)
{
    this->SetClientData(item, (void*)label_item_type);
}

bool PresetComboBox::set_printer_technology(PrinterTechnology pt)
{
    if (printer_technology != pt) {
        printer_technology = pt;
        return true;
    }
    return false;
}

void PresetComboBox::invalidate_selection()
{
    m_last_selected = INT_MAX; // this value means that no one item is selected
}

void PresetComboBox::validate_selection(bool predicate/*=false*/)
{
    if (predicate &&
        // just in case: mark m_last_selected as a first added element
        m_last_selected == INT_MAX)
        m_last_selected = GetCount() - 1;
}

void PresetComboBox::update_selection()
{
    /* If selected_preset_item is still equal to INT_MAX, it means that
     * there is no presets added to the list.
     * So, select last combobox item ("Add/Remove preset")
     */
    //validate_selection();
    if (m_last_selected == INT_MAX) {
        m_last_selected = 1;
        // For filament comboboxes the AMS/nozzle entries are appended before the preset groups, so
        // item 1 belongs to a printer tray. Landing there would show that tray's filament name and
        // tray-number icon as if this slot used it, so step past the AMS block. Guarded on the
        // block being non-empty (m_last_ams_filament > m_first_ams_filament), not on
        // m_first_ams_filament itself: that block almost always starts at index 0 (the very first
        // thing appended into a freshly cleared combo), and 0 is falsy, so the old
        // `if (m_first_ams_filament && ...)` silently skipped this correction in the common case.
        if (m_last_ams_filament > m_first_ams_filament && m_last_selected >= m_first_ams_filament && m_last_selected < m_last_ams_filament &&
            m_last_ams_filament < (int) GetCount())
            m_last_selected = m_last_ams_filament;

        // The index above can still land squarely on a group-header row ("System presets" / "User
        // presets", appended via set_label_marker()/DD_ITEM_STYLE_SPLIT_ITEM) instead of a real
        // preset -- e.g. exactly one AMS/nozzle entry before the first preset group puts index 1
        // right on that group's header. Showing a header's raw label text as this slot's filament
        // name is worse than showing the wrong material, so step past any label-marker item(s).
        while (m_last_selected < (int) GetCount()) {
            auto marker = reinterpret_cast<Marker>(GetClientData(m_last_selected));
            if (marker < LABEL_ITEM_DISABLED || marker >= LABEL_ITEM_MAX)
                break;
            ++m_last_selected;
        }
    }

    SetSelection(m_last_selected);
#ifdef __WXMSW__
    // From the Windows 2004 the tooltip for preset combobox doesn't work after next call of SetTooltip()
    // (There was an issue, when tooltip doesn't appears after changing of the preset selection)
    // But this workaround seems to work: We should to kill tooltip and than set new tooltip value
    SetToolTip(NULL);
#endif
    SetToolTip(GetString(m_last_selected));

// A workaround for a set of issues related to text fitting into gtk widgets:
#if defined(__WXGTK20__) || defined(__WXGTK3__)
  GtkWidget* widget = m_widget;
    if (GTK_IS_CONTAINER(widget)) {
        GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
        if (children) {
            widget = GTK_WIDGET(children->data);
            g_list_free(children);
        }
    }
    if (GTK_IS_ENTRY(widget)) {
        // Set ellipsization for the entry
        gtk_entry_set_width_chars(GTK_ENTRY(widget), 20);  // Adjust this value as needed
        gtk_entry_set_max_width_chars(GTK_ENTRY(widget), 20);  // Adjust this value as needed
        // Create a PangoLayout for the entry and set ellipsization
        PangoLayout* layout = gtk_entry_get_layout(GTK_ENTRY(widget));
        if (layout) {
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        } else {
            g_warning("Unable to get PangoLayout from GtkEntry");
        }
    } else {
        g_warning("Expected GtkEntry, but got %s", G_OBJECT_TYPE_NAME(widget));
    }
#endif
}

int PresetComboBox::update_ams_color()
{
    if (m_filament_idx < 0) return -1;
    int idx = selected_ams_filament();
    std::string color;
    std::string ctype;
    std::vector<std::string> colors;
    if (idx < 0) {
        auto  name   = Preset::remove_suffix_modified(GetValue().ToUTF8().data());
        auto *preset = m_collection->find_preset(name);
        if (preset)
            color = preset->config.opt_string("default_filament_colour", 0u);
        if (color.empty()) return -1;
    } else {
        auto &ams_list = wxGetApp().preset_bundle->filament_ams_list;
        auto  iter     = ams_list.find(idx);
        if (iter == ams_list.end()) {
            BOOST_LOG_TRIVIAL(warning) << boost::format("ams %1% out of range %2%") % idx % ams_list.size();
            return -1;
        }
        color = iter->second.opt_string("filament_colour", 0u);
        ctype = iter->second.opt_string("filament_colour_type", 0u);
        colors = iter->second.opt<ConfigOptionStrings>("filament_multi_colour")->values;
    }
    DynamicPrintConfig *cfg        = &wxGetApp().preset_bundle->project_config;
    auto color_head = static_cast<ConfigOptionStrings*>(cfg->option("filament_colour")->clone()); // single color (the first color if multi-color filament)
    auto color_pack = static_cast<ConfigOptionStrings *>(cfg->option("filament_multi_colour")->clone()); // multi color (all colors in all kinds of filament)
    auto color_type = static_cast<ConfigOptionStrings*>(cfg->option("filament_colour_type")->clone()); // color type

    color_head->values[m_filament_idx] = color;
    color_type->values[m_filament_idx] = ctype;
    std::string color_str = ""; // Translate multi color info to config storage format
    for (auto &c : colors) {
        if (c.empty()) continue;
        color_str += c + " ";
    }
    if (color_str.empty()) color_str = color;
    else color_str.erase(color_str.size() - 1);
    color_pack->values[m_filament_idx] = color_str;

    // Update color informations in config
    DynamicPrintConfig new_cfg;
    new_cfg.set_key_value("filament_colour", color_head);
    new_cfg.set_key_value("filament_colour_type", color_type);
    new_cfg.set_key_value("filament_multi_colour", color_pack);
    cfg->apply(new_cfg);
    wxGetApp().plater()->on_config_change(new_cfg);
    //trigger the filament color changed
    wxCommandEvent *evt = new wxCommandEvent(EVT_FILAMENT_COLOR_CHANGED);
    evt->SetInt(m_filament_idx);
    wxQueueEvent(wxGetApp().plater(), evt);
    return idx;
}

wxColor PresetComboBox::different_color(wxColor const &clr)
{
    if (clr.GetLuminance() < 0.51) return *wxWHITE;
    return *wxBLACK;
}

wxString PresetComboBox::get_tooltip(const Preset &preset)
{
    wxString tooltip = from_u8(preset.name);
    // BBS: FIXME
#if 0
    if (m_type == Preset::TYPE_FILAMENT) {
        int temperature[4] = { 0,0,0,0 };
        if (preset.config.has("nozzle_temperature_initial_layer")) //get the nozzle_temperature_initial_layer
            temperature[0] = preset.config.opt_int("nozzle_temperature_initial_layer", 0);
        if (preset.config.has("nozzle_temperature")) //get the nozzle temperature
            temperature[1] = preset.config.opt_int("nozzle_temperature", 0);
        if (preset.config.has("bed_temperature_initial_layer")) //get the bed_temperature_initial_layer
            temperature[2] = preset.config.opt_int("bed_temperature_initial_layer", 0);
        if (preset.config.has("bed_temperature")) //get the bed_temperature
            temperature[3] = preset.config.opt_int("bed_temperature", 0);

        tooltip += wxString::Format("\nNozzle First Layer:%d, Other Layer:%d\n Bed First Layer:%d, Other Layers:%d",
            temperature[0], temperature[1], temperature[2], temperature[3]);
    }
#endif
    return tooltip;
}

wxString PresetComboBox::get_preset_item_name(unsigned int index)
{
    if (m_type == Preset::TYPE_PRINTER) {
        int idx = selected_connected_printer(index);
        if (idx < 0) {
            m_selected_dev_id.clear();
            return GetString(index);
        }
        else {
            DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
            if (!dev) {
                assert(false);
                m_selected_dev_id.clear();
                return GetString(index);
            }

            std::map<std::string, MachineObject *> machine_list = dev->get_my_machine_list();
            if (machine_list.empty()) {
                assert(false);
                m_selected_dev_id.clear();
                return GetString(index);
            }

            auto iter = m_backup_dev_list_sorted.begin();
            std::advance(iter, idx);
            if (iter != m_backup_dev_list_sorted.end() && machine_list.find(*iter) != machine_list.end()) {
                m_selected_dev_id = *iter;
                Preset* machine_preset = get_printer_preset(machine_list[*iter]);
                if (machine_preset) {
                    dev->set_selected_machine(*iter);
                    return from_u8(machine_preset->name);
                }
            }
        }
    }

    m_selected_dev_id.clear();
    return GetString(index);
}

wxString PresetComboBox::get_preset_name(const Preset & preset)
{
    const std::string display_name = multi_nozzle_display_name(m_preset_bundle, m_type, preset);
    if (!display_name.empty())
        return from_u8(display_name);
    return from_u8(preset.name/* + suffix(preset)*/);
}

std::string PresetComboBox::preset_name_for_selection(int selection) const
{
    const auto it = m_item_preset_names.find(selection);
    if (it != m_item_preset_names.end())
        return it->second;
    return selection >= 0 && selection < int(GetCount()) ? GetString(selection).ToUTF8().data() : std::string();
}

void PresetComboBox::update(std::string select_preset_name)
{
    Freeze();
    Clear();
    m_item_preset_names.clear();
    invalidate_selection();

    const std::deque<Preset>& presets = m_collection->get_presets();

    // The label is not always the preset name (multi-diameter mode rewrites the printer and process
    // labels), so the real name travels alongside the bitmap and lands in m_item_preset_names when the
    // item is appended.
    struct PresetItem {
        wxBitmap*   bitmap;
        bool        enabled;
        std::string preset_name;
    };
    std::map<wxString, PresetItem>  nonsys_presets;
    std::map<wxString, PresetItem>  incomp_presets;

    wxString selected = "";
    if (!presets.front().is_visible)
        set_label_marker(Append(_L("System presets"), wxNullBitmap, DD_ITEM_STYLE_SPLIT_ITEM));

    for (size_t i = presets.front().is_visible ? 0 : m_collection->num_default_presets(); i < presets.size(); ++i)
    {
        const Preset& preset = presets[i];
        if (!m_show_all && (!preset.is_visible || !preset.is_compatible))
            continue;

        // Exempt both the collection's own selection and the preset this call is asked to select:
        // the compare dialog restores a remembered pick here, which must not vanish from the list.
        if (i != m_collection->get_selected_idx() && preset.name != select_preset_name &&
            hidden_by_multi_nozzle_diameter_mode(m_preset_bundle, *m_collection, m_type, preset))
            continue;

        // marker used for disable incompatible printer models for the selected physical printer
        bool is_enabled = m_type == Preset::TYPE_PRINTER && printer_technology != ptAny ? preset.printer_technology() == printer_technology : true;
        if (select_preset_name.empty() && is_enabled)
            select_preset_name = preset.name;

        wxBitmap* bmp = get_bmp(preset);
        assert(bmp);

        if (!is_enabled)
            incomp_presets.emplace(get_preset_name(preset), PresetItem{bmp, is_enabled, preset.name});
        else if (preset.is_default || preset.is_system)
        {
            int item_id = Append(get_preset_name(preset), *bmp);
            m_item_preset_names.emplace(item_id, preset.name);
            validate_selection(preset.name == select_preset_name);
        }
        else
        {
            nonsys_presets.emplace(get_preset_name(preset), PresetItem{bmp, is_enabled, preset.name});
            if (preset.name == select_preset_name || (select_preset_name.empty() && is_enabled))
                selected = get_preset_name(preset);
        }
        if (i + 1 == m_collection->num_default_presets())
            set_label_marker(Append(_L("System presets"), wxNullBitmap, DD_ITEM_STYLE_SPLIT_ITEM));
    }
    if (!nonsys_presets.empty())
    {
        set_label_marker(Append(_L("User presets"), wxNullBitmap, DD_ITEM_STYLE_SPLIT_ITEM));
        for (auto it = nonsys_presets.begin(); it != nonsys_presets.end(); ++it) {
            int item_id = Append(it->first, *it->second.bitmap);
            m_item_preset_names.emplace(item_id, it->second.preset_name);
            bool is_enabled = it->second.enabled;
            if (!is_enabled)
                set_label_marker(item_id, LABEL_ITEM_DISABLED);
            validate_selection(it->first == selected);
        }
    }
    if (!incomp_presets.empty())
    {
        set_label_marker(Append(_L("Incompatible presets"), wxNullBitmap, DD_ITEM_STYLE_SPLIT_ITEM));
        for (auto it = incomp_presets.begin(); it != incomp_presets.end(); ++it) {
            int item_id = Append(it->first, *it->second.bitmap);
            m_item_preset_names.emplace(item_id, it->second.preset_name);
            set_label_marker(item_id, LABEL_ITEM_DISABLED);
        }
    }

    update_selection();
    Thaw();
}

void PresetComboBox::show_all(bool show_all)
{
    m_show_all = show_all;
    update();
}

void PresetComboBox::update()
{
    // Never the displayed text: in multi-diameter mode the printer and process labels are rewrites that
    // match no preset, so re-selecting by label would drop the current selection.
    this->update(preset_name_for_selection(this->GetSelection()));
}

void PresetComboBox::update_from_bundle()
{
    this->update(m_collection->get_selected_preset().name);
}

void PresetComboBox::add_connected_printers(std::string selected, bool alias_name)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    std::map<std::string, MachineObject *> machine_list = dev->get_my_machine_list();
    if (machine_list.empty())
        return;

    set_label_marker(Append(_L("My Printer"), wxNullBitmap, DD_ITEM_STYLE_SPLIT_ITEM));
    m_first_printer_idx = GetCount();

    // sort list
    std::vector<std::pair<std::string, MachineObject *>> user_machine_list;
    m_backup_dev_list.clear();
    for (auto &it : machine_list) {
        m_backup_dev_list.push_back(it.first);
        user_machine_list.push_back(it);
    }

    std::sort(user_machine_list.begin(), user_machine_list.end(), [&](auto &a, auto &b) {
        if (a.second && b.second) { return a.second->get_dev_name().compare(b.second->get_dev_name()) < 0; }
        return false;
    });

    m_backup_dev_list_sorted.clear();
    for (auto &it : user_machine_list) {
        m_backup_dev_list_sorted.push_back(it.first);
    }

    for (auto iter = user_machine_list.begin(); iter != user_machine_list.end(); ++iter) {
        Preset* printer_preset = get_printer_preset(iter->second);
        if (!printer_preset)
            continue;
        printer_preset->is_visible = true;
        auto printer_model = printer_preset->config.opt_string("printer_model");
        boost::replace_all(printer_model, "Bambu Lab ", "");
        auto text = iter->second->get_dev_name() + " (" + printer_model + ")";
        int item_id = Append(from_u8(text), wxNullBitmap, &m_first_printer_idx + std::distance(user_machine_list.begin(), iter));
        validate_selection(m_selected_dev_id == iter->first);
    }
    m_last_printer_idx = GetCount();
}

int PresetComboBox::selected_connected_printer(int index) const
{
    if (m_first_printer_idx && index >= m_first_printer_idx && index < m_last_printer_idx) {
        return reinterpret_cast<int *>(GetClientData(index)) - &m_first_printer_idx;
    }
    return -1;
}

bool PresetComboBox::add_ams_filaments_grouped_by_nozzle(std::string selected, bool alias_name)
{
    bool selected_in_ams = false;

    const bool multi_diameter_mode = multi_nozzle_diameter_mode_enabled(m_preset_bundle->project_config);

    // Resolve the slicing nozzle diameter for this combobox's extruder.
    double cur_nozzle_dia = 0.0;
    {
        int         extruder_index = 0;
        const auto *fil_map        = m_preset_bundle->project_config.option<ConfigOptionInts>("filament_map");
        if (fil_map && (size_t) m_filament_idx < fil_map->values.size()) { extruder_index = std::max(0, fil_map->values[m_filament_idx] - 1); }
        const auto *nozzles = m_preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
        if (nozzles && (size_t) extruder_index < nozzles->values.size()) { cur_nozzle_dia = nozzles->values[extruder_index]; }
    }

    auto     &filaments  = m_collection->get_presets();
    const int icon_width = tray_icon_width(m_preset_bundle->filament_ams_list);

    // The per-row synced badge lights up when the row is the AMS source of some currently-selected
    // filament slot. Gated on a matching connected printer so an unconnected/mismatched state never
    // lights up a row here either, regardless of any name/color match.
    const bool printer_connected_and_matched = wxGetApp().plater()->is_same_printer_for_connected_and_selected(false);
    const auto *slot_colors_opt = wxGetApp().preset_bundle->project_config.option<ConfigOptionStrings>("filament_colour");

    // Multi-diameter mode adds two extra sync criteria: the slot's selected nozzle diameter and
    // flow type must match the tray's. Hoist those values out of the per-tray loop below. When the
    // toggle is off, these are ignored (row_matches_current uses filament_id + color only) --
    // syncing in multi-diameter mode requires re-syncing since the previous non-multi-diameter sync
    // didn't record diameter/flow, so the previously-synced state is invalidated here.
    std::string      slot_selected_diameter;
    NozzleVolumeType slot_selected_volume = NozzleVolumeType::nvtStandard;
    if (multi_diameter_mode && m_filament_idx >= 0) {
        const auto *sel_diameters = m_preset_bundle->project_config.option<ConfigOptionStrings>("filament_diameter_map");
        if (sel_diameters && (size_t) m_filament_idx < sel_diameters->values.size())
            slot_selected_diameter = sel_diameters->values[m_filament_idx];
        if (Plater *plater = wxGetApp().plater()) {
            if (PartPlate *plate = plater->get_partplate_list().get_curr_plate()) {
                const std::vector<int> vmaps = plate->get_real_filament_volume_maps(m_preset_bundle->project_config);
                if ((size_t) m_filament_idx < vmaps.size()) slot_selected_volume = (NozzleVolumeType) vmaps[m_filament_idx];
            }
        }
    }

    auto append_one = [&](int entry_key, const DynamicPrintConfig &tray, bool is_incompatible_row,
                          bool flow_mismatch) {
        std::string filament_id = tray.opt_string("filament_id", 0u);
        auto        name        = tray.opt_string("tray_name", 0u);
        if (filament_id.empty()) {
            BOOST_LOG_TRIVIAL(info) << boost::format("%1% 's filament_id is empty.") % name;
            return;
        }

        auto iter = std::find_if(filaments.begin(), filaments.end(),
                                 [&filament_id, this](auto &f) { return f.is_compatible && m_collection->get_preset_base(f) == &f && f.filament_id == filament_id; });
        if (iter == filaments.end()) {
            auto filament_type = tray.opt_string("filament_type", 0u);
            if (!filament_type.empty()) {
                filament_type = "Generic " + filament_type;
                iter          = std::find_if(filaments.begin(), filaments.end(),
                                             [&filament_type](auto &f) { return f.is_compatible && f.is_system && boost::algorithm::starts_with(f.name, filament_type); });
            }
        }
        if (iter == filaments.end()) {
            BOOST_LOG_TRIVIAL(warning) << boost::format("filament_id %1% not found or system or compatible") % filament_id;
            return;
        }

        const_cast<Preset &>(*iter).is_visible = true;
        auto      color                        = tray.opt_string("filament_colour", 0u);
        wxBitmap *ams_bmp                      = tray_color_icon(tray, name, icon_width);
        if (!ams_bmp) return;
        wxBitmap bmp(*ams_bmp);
        auto     text = get_preset_name(*iter);

        // The nozzle goes in the item suffix, not the label, so the dropdown draws it as a lighter
        // secondary column after the filament name.
        wxString nozzle_suffix;
        if (multi_diameter_mode) {
            const auto *tray_nd = tray.option<ConfigOptionFloats>("tray_nozzle_diameter");
            const auto *tray_vt = tray.option<ConfigOptionInts>("tray_nozzle_volume_type");
            if (tray_nd && !tray_nd->values.empty() && tray_vt && !tray_vt->values.empty()) {
                auto volume_type = static_cast<NozzleVolumeType>(tray_vt->values.front());
                nozzle_suffix = "| " + from_u8(format_diameter_to_str(tray_nd->values.front()) + " " + DevNozzle::ToNozzleVolumeShortString(volume_type));
            }
        }

        // Match by filament_id + color (same criteria tray_is_synced_source uses for the per-row
        // dropdown badge), NOT by text/label comparison. Label matching was fragile: get_preset_name(*iter)
        // returns the fully-qualified name (with the printer/nozzle suffix) when the found variant's
        // alias is empty.
        bool row_matches_current = false;
        if (slot_colors_opt && (size_t) m_filament_idx < slot_colors_opt->values.size()) {
            const Preset *slot_preset = (size_t) m_filament_idx < m_preset_bundle->filament_presets.size()
                ? m_collection->find_preset(m_preset_bundle->filament_presets[m_filament_idx]) : nullptr;
            if (slot_preset && !slot_preset->filament_id.empty()
                && slot_preset->filament_id == filament_id
                && color == slot_colors_opt->values[m_filament_idx]) {
                row_matches_current = true;
            }
        }
        // Multi-diameter mode: also require the tray's nozzle diameter + flow type to match the
        // slot's current selection. Without this, toggling the multi-diameter switch on carries
        // over the previous non-multi-diameter sync state (which never validated diameter/flow),
        // giving a false "synced" indication.
        if (row_matches_current && multi_diameter_mode) {
            const auto *tray_nd = tray.option<ConfigOptionFloats>("tray_nozzle_diameter");
            const auto *tray_vt = tray.option<ConfigOptionInts>("tray_nozzle_volume_type");
            if (tray_nd && !tray_nd->values.empty() && tray_vt && !tray_vt->values.empty()) {
                const std::string tray_diameter_str = format_diameter_to_str(tray_nd->values.front());
                const NozzleVolumeType tray_volume = static_cast<NozzleVolumeType>(tray_vt->values.front());
                if (slot_selected_diameter != tray_diameter_str || slot_selected_volume != tray_volume)
                    row_matches_current = false;
            } else {
                // Tray missing diameter/volume metadata under multi-diameter mode -- can't verify, drop it.
                row_matches_current = false;
            }
        }
        // Single-diameter mode on a single-extruder multi-nozzle machine: only ONE physical nozzle
        // is active at a time (the one the user picked via the diameter buttons). Trays physically
        // bound to the other nozzles route through nozzles that aren't part of this print, so they
        // shouldn't count as the "synced source" for any slot -- otherwise every AMS-loaded filament
        // shows the sync badge even when only one nozzle is actually in use. Filter to the trays
        // whose diameter matches the printer's currently-active primary diameter.
        if (row_matches_current && !multi_diameter_mode && is_single_ext_multi_nozzle(m_preset_bundle->full_config())) {
            const auto *tray_nd = tray.option<ConfigOptionFloats>("tray_nozzle_diameter");
            const auto *printer_nd = m_preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
            if (tray_nd && !tray_nd->values.empty() && printer_nd && !printer_nd->values.empty()) {
                const std::string tray_diameter_str    = format_diameter_to_str(tray_nd->values.front());
                const std::string printer_diameter_str = format_diameter_to_str(printer_nd->values.front());
                if (tray_diameter_str != printer_diameter_str)
                    row_matches_current = false;
            }
        }
        // Changing diameter/flow via FilamentNozzlePopup needs no separate "user diverged" bookkeeping:
        // that popup only exists in multi-diameter mode, and the check above already compares the tray
        // against the slot's live filament_diameter_map / volume map entry. Setting the slot back to a
        // combination the rack really carries therefore lights the badge again, as it should.

        // Per-row dropdown badge: only light up when this tray is the source of THIS combo's own
        // currently-selected preset (filament_id + color match) and printer is connected/matched.
        // row_matches_current already computes exactly that condition above -- reuse it here so
        // the dropdown row lights up in lockstep with the outer badge (which is also driven by
        // selected_in_ams |= row_matches_current). Incompatible-row trays never qualify since they
        // can't be mounted on the current extruder right now.
        int item_style = 0;
        if (!is_incompatible_row && printer_connected_and_matched && row_matches_current) item_style |= DD_ITEM_STYLE_SYNCED;
        // A tray in the unavailable section cannot be printed from as things stand -- either it sits
        // at a diameter this print is not using, or its material does not go with the nozzle it is
        // loaded on. Greyed and refusing the click either way, rather than accepting a selection the
        // slot would reject straight afterwards; the tooltip below says which of the two it is.
        if (is_incompatible_row) item_style |= DD_ITEM_STYLE_DISABLED;

        const int index = Append(text, bmp.ConvertToImage(), &m_first_ams_filament + entry_key, item_style);
        SetFlag(GetCount() - 1, (int) FilamentAMSType::FROM_AMS);
        if (!nozzle_suffix.IsEmpty())
            SetItemSuffix(index, nozzle_suffix);
        // Two ways a tray can be unusable, and they need different answers: a flow mismatch is about
        // this tray's own material and nozzle, which the nozzle settings cannot reconcile, while a
        // diameter mismatch only means the project is printing at another diameter right now.
        if (is_incompatible_row)
            SetItemTooltip(index, flow_mismatch
                ? _L("This filament is not compatible with the nozzle it is loaded on")
                : _L("To select this filament, change the nozzle settings first."));
        if (row_matches_current) selected_in_ams = true;
    };

    // Can the material in this tray print on the nozzle the tray is actually loaded on? Asked of the
    // tray alone, independent of which slot the list is being opened for, because selecting the tray
    // would bring both halves along. A tray that fails this cannot be used however the project is
    // configured -- offering it as selectable only leads to a slot that flags itself immediately
    // after the pick.
    auto tray_fits_its_own_nozzle = [this, &filaments](const DynamicPrintConfig &tray) -> bool {
        const auto *tray_vt = tray.option<ConfigOptionInts>("tray_nozzle_volume_type");
        if (tray_vt == nullptr || tray_vt->values.empty())
            return true; // the machine said nothing about the flow; no grounds to rule the tray out
        const NozzleVolumeType tray_flow = static_cast<NozzleVolumeType>(tray_vt->values.front());

        // Same two-step lookup the row's own label goes through, so the verdict describes the preset
        // the user would actually end up with rather than one that was never offered.
        const std::string filament_id = tray.opt_string("filament_id", 0u);
        if (filament_id.empty())
            return true;
        auto iter = std::find_if(filaments.begin(), filaments.end(),
                                 [&filament_id, this](auto &f) { return f.is_compatible && m_collection->get_preset_base(f) == &f && f.filament_id == filament_id; });
        if (iter == filaments.end()) {
            auto filament_type = tray.opt_string("filament_type", 0u);
            if (!filament_type.empty()) {
                filament_type = "Generic " + filament_type;
                iter          = std::find_if(filaments.begin(), filaments.end(),
                                             [&filament_type](auto &f) { return f.is_compatible && f.is_system && boost::algorithm::starts_with(f.name, filament_type); });
            }
        }
        if (iter == filaments.end())
            return true; // nothing to judge against; the row itself will not be listed either

        std::vector<std::string> filament_variants;
        if (const auto *opt = iter->config.option<ConfigOptionStrings>("filament_extruder_variant"))
            filament_variants = opt->values;
        const bool variants_complete = m_collection->is_bbl_brand_filament(*iter);
        return is_nozzle_printable_for_filament(tray_flow, filament_variants, variants_complete);
    };

    auto diameter_mismatch = [cur_nozzle_dia, multi_diameter_mode](const DynamicPrintConfig &tray) -> bool {
        if (multi_diameter_mode) return false;
        if (cur_nozzle_dia <= 0.0) return false;
        const auto *tray_nd = tray.option<ConfigOptionFloats>("tray_nozzle_diameter");
        if (!tray_nd || tray_nd->values.empty()) return false;
        double tray_dia = tray_nd->values.front();
        return tray_dia > 0.0 && std::abs(tray_dia - cur_nozzle_dia) > 1e-3;
    };

    set_label_marker(Append(_L("Available Nozzles"), wxNullBitmap, DD_ITEM_STYLE_SPLIT_ITEM));
    m_first_ams_filament = GetCount();

    // Third element records which of the two reasons applies, so the row can say the right thing.
    std::vector<std::tuple<int, const DynamicPrintConfig *, bool>> incompatible_entries;
    std::vector<std::pair<int, const DynamicPrintConfig *>>        compatible_entries;
    for (auto &entry : m_preset_bundle->filament_ams_list) {
        const bool flow_mismatch = !tray_fits_its_own_nozzle(entry.second);
        if (flow_mismatch || diameter_mismatch(entry.second))
            incompatible_entries.emplace_back(entry.first, &entry.second, flow_mismatch);
        else
            compatible_entries.emplace_back(entry.first, &entry.second);
    }

    // No re-sort: filament_ams_list's own order (a map iterates by key) already reflects the
    // physical layout -- an externally-attached unit sits right after the nozzle it plugs into
    // (e.g. "1, A1, A2, 2, 3, ..."). Sorting by tray_name here used to push every "A"/"B" unit to
    // the bottom.

    for (auto &entry : compatible_entries) { append_one(entry.first, *entry.second, false, false); }

    if (!incompatible_entries.empty()) {
        set_label_marker(Append(_L("Unavailable Nozzles"), wxNullBitmap, DD_ITEM_STYLE_SPLIT_ITEM));
        for (auto &kv : incompatible_entries)
            append_one(std::get<0>(kv), *std::get<1>(kv), true, std::get<2>(kv));
    }

    m_last_ams_filament = GetCount();
    return selected_in_ams;
}

bool PresetComboBox::add_ams_filaments(std::string selected, bool alias_name)
{
    auto &cur_printer = m_preset_bundle->printers.get_edited_preset();

    if(!cur_printer.is_bbl_vendor_preset(m_preset_bundle) || m_preset_bundle->filament_ams_list.empty()) {
        return {};
    }

    // for machines like N1X, which treat hotends as logical extruders
    if (auto *obj = wxGetApp().getDeviceManager()->get_selected_machine()) {
        if (obj->GetNozzleRack()->IsDirectConnectedRack()) { return add_ams_filaments_grouped_by_nozzle(selected, alias_name); }
    }

    bool selected_in_ams      = false;
    bool fila_switch_ready = wxGetApp().sidebar().is_fila_switch_ready();
    bool dual_extruder   = (m_preset_bundle->filament_ams_list.begin()->first & 0x10000) == 0;

    if (fila_switch_ready) {
        set_label_marker(Append(_L("AMS filaments"), wxNullBitmap, DD_ITEM_STYLE_SPLIT_ITEM));
    } else {
        set_label_marker(Append(dual_extruder ? _L("Left filaments") : _L("AMS filaments"), wxNullBitmap, DD_ITEM_STYLE_SPLIT_ITEM));
    }

    m_first_ams_filament = GetCount();
    auto &filaments      = m_collection->get_presets();

    const int icon_width = tray_icon_width(m_preset_bundle->filament_ams_list);

    std::set<std::pair<std::string, std::string>> added_filaments;

    for (auto &entry : m_preset_bundle->filament_ams_list) {
        if (!fila_switch_ready && dual_extruder && (entry.first & 0x10000)) {
            dual_extruder = false;
            set_label_marker(Append(_L("Right filaments"), wxNullBitmap, DD_ITEM_STYLE_SPLIT_ITEM));
        }
        auto &      tray        = entry.second;
        std::string filament_id = tray.opt_string("filament_id", 0u);
        auto        name        = tray.opt_string("tray_name", 0u);
        if (filament_id.empty()) {
            BOOST_LOG_TRIVIAL(info) << boost::format("%1% 's filament_id is empty.") % name;
            continue;
        }
        if (fila_switch_ready && name == "Ext") {
            continue;
        }

        if (fila_switch_ready) {
            auto filament_pair = std::make_pair(name, filament_id);
            if (added_filaments.find(filament_pair) != added_filaments.end()) {
                continue;
            }
            added_filaments.insert(filament_pair);
        }

        auto iter = std::find_if(filaments.begin(), filaments.end(),
            [&filament_id, this](auto &f) { return f.is_compatible && m_collection->get_preset_base(f) == &f && f.filament_id == filament_id; });
        if (iter == filaments.end()) {
            auto filament_type = tray.opt_string("filament_type", 0u);
            if (!filament_type.empty()) {
                filament_type = "Generic " + filament_type;
                iter          = std::find_if(filaments.begin(), filaments.end(),
                                    [&filament_type](auto &f) { return f.is_compatible && f.is_system && boost::algorithm::starts_with(f.name, filament_type); });
            }
        }
        if (iter == filaments.end()) {
            BOOST_LOG_TRIVIAL(warning) << boost::format("filament_id %1% not found or system or compatible") % filament_id;
            continue;
        }
        const_cast<Preset&>(*iter).is_visible = true;
        auto      color                       = tray.opt_string("filament_colour", 0u);
        wxBitmap *ams_bmp                     = tray_color_icon(tray, name, icon_width);
        if (!ams_bmp) continue;
        wxBitmap bmp(*ams_bmp);
        auto text = get_preset_name(*iter);
        int      item_id = Append(text, bmp.ConvertToImage(), &m_first_ams_filament + entry.first);
        SetFlag(GetCount() - 1, (int) FilamentAMSType::FROM_AMS);
        if (text == selected) {
            DynamicPrintConfig *cfg    = &wxGetApp().preset_bundle->project_config;
            if (cfg) {
                auto colors = static_cast<ConfigOptionStrings *>(cfg->option("filament_colour")->clone());
                if (m_filament_idx < colors->values.size()) {
                    auto cur_color = colors->values[m_filament_idx];
                    if (color == cur_color) {
                        selected_in_ams = true;
                    }
                }
            }
        }
        //validate_selection(id->value == selected); // can not select
    }
    m_last_ams_filament = GetCount();
    return selected_in_ams;
}

int PresetComboBox::selected_ams_filament() const
{
    if (m_first_ams_filament && m_last_selected >= m_first_ams_filament && m_last_selected < m_last_ams_filament) {
        return reinterpret_cast<int *>(GetClientData(m_last_selected)) - &m_first_ams_filament;
    }
    return -1;
}

void PresetComboBox::msw_rescale()
{
    m_em_unit = em_unit(this);
    Rescale();

    m_bitmapIncompatible.msw_rescale();
    m_bitmapCompatible.msw_rescale();

    // parameters for an icon's drawing
    fill_width_height();

    // update the control to redraw the icons
    update();
}

void PresetComboBox::sys_color_changed()
{
    wxGetApp().UpdateDarkUI(this);
    msw_rescale();
}

void PresetComboBox::fill_width_height()
{
    // To avoid asserts, each added bitmap to wxBitmapCombobox should be the same size, so
    // set a bitmap's height to m_bitmapCompatible->GetHeight() and norm_icon_width to m_bitmapCompatible->GetWidth()
    icon_height     = m_bitmapCompatible.GetBmpHeight();
    norm_icon_width = m_bitmapCompatible.GetBmpWidth();

    /* It's supposed that standard size of an icon is 16px*16px for 100% scaled display.
    * So set sizes for solid_colored icons used for filament preset
    * and scale them in respect to em_unit value
    */
    const float scale_f = (float)m_em_unit * 0.1f;

    thin_icon_width = lroundf(8 * scale_f);          // analogue to 8px;
    wide_icon_width = norm_icon_width + thin_icon_width;

    space_icon_width      = lroundf(2 * scale_f);
    thin_space_icon_width = lroundf(4 * scale_f);
    wide_space_icon_width = lroundf(6 * scale_f);
}

wxString PresetComboBox::separator(const std::string& label)
{
    return wxString::FromUTF8(separator_head()) + _(label) + wxString::FromUTF8(separator_tail());
}

wxBitmap* PresetComboBox::get_bmp(  std::string bitmap_key, bool wide_icons, const std::string& main_icon_name,
                                    bool is_compatible/* = true*/, bool is_system/* = false*/, bool is_single_bar/* = false*/,
                                    const std::string& filament_rgb/* = ""*/, const std::string& extruder_rgb/* = ""*/, const std::string& material_rgb/* = ""*/)
{
    // BBS: no icon
#if 1
    static wxBitmap bmp;
    return &bmp;
#else
    // If the filament preset is not compatible and there is a "red flag" icon loaded, show it left
    // to the filament color image.
    if (wide_icons)
        bitmap_key += is_compatible ? ",cmpt" : ",ncmpt";

    bitmap_key += is_system ? ",syst" : ",nsyst";
    bitmap_key += ",h" + std::to_string(icon_height);
    bool dark_mode = wxGetApp().dark_mode();
    if (dark_mode)
        bitmap_key += ",dark";
    bitmap_key += material_rgb;

    wxBitmap* bmp = bitmap_cache().find(bitmap_key);
    if (bmp == nullptr) {
        // Create the bitmap with color bars.
        std::vector<wxBitmap> bmps;
        if (wide_icons)
            // Paint a red flag for incompatible presets.
            bmps.emplace_back(is_compatible ? bitmap_cache().mkclear(norm_icon_width, icon_height) : m_bitmapIncompatible.bmp());

        if (m_type == Preset::TYPE_FILAMENT && !filament_rgb.empty())
        {
            // BBS
            // Paint a lock at the system presets.
            bmps.emplace_back(bitmap_cache().mkclear(space_icon_width, icon_height));
        }
        else
        {
            // BBS
#if 0
            // Paint the color bars.
            bmps.emplace_back(bitmap_cache().mkclear(thin_space_icon_width, icon_height));
            if (m_type == Preset::TYPE_SLA_MATERIAL)
                bmps.emplace_back(create_scaled_bitmap(main_icon_name, this, 16, false, material_rgb));
            else
                bmps.emplace_back(create_scaled_bitmap(main_icon_name));
#endif
            // Paint a lock at the system presets.
            bmps.emplace_back(bitmap_cache().mkclear(wide_space_icon_width, icon_height));
        }
        bmps.emplace_back(is_system ? create_scaled_bitmap("unlock_normal") : bitmap_cache().mkclear(norm_icon_width, icon_height));
        bmp = bitmap_cache().insert(bitmap_key, bmps);
    }

    return bmp;
#endif
}

wxBitmap *PresetComboBox::get_bmp(Preset const &preset)
{
    static wxBitmap sbmp;
    if (m_type == Preset::TYPE_FILAMENT) {
        Preset const & preset2 = &m_collection->get_selected_preset() == &preset ? m_collection->get_edited_preset() : preset;
        wxString color = preset2.config.opt_string("default_filament_colour", 0);
        // // If this preset is the one currently assigned to this combo box's filament slot,
        // // use the per-slot color from project_config so the dropdown matches the closed-combo swatch.
        // if (m_preset_bundle && m_filament_idx >= 0 &&
        //     m_filament_idx < (int)m_preset_bundle->filament_presets.size() &&
        //     m_preset_bundle->filament_presets[m_filament_idx] == preset.name) {
        //     if (const auto *colours_opt = m_preset_bundle->project_config.option<ConfigOptionStrings>("filament_colour")) {
        //         if (m_filament_idx < (int)colours_opt->values.size() && !colours_opt->values[m_filament_idx].empty())
        //             color = from_u8(colours_opt->values[m_filament_idx]);
        //     }
        // }
        wxColour clr(color);
        if (clr.IsOk()) {
            std::string bitmap_key = "filament_colour_" + color.ToStdString();
            wxBitmap *bmp        = bitmap_cache().find(bitmap_key);
            if (bmp == nullptr) {
                wxImage img(16, 16);
                if (clr.Red() > 224 && clr.Blue() > 224 && clr.Green() > 224) {
                    img.SetRGB(wxRect({0, 0}, img.GetSize()), 128, 128, 128);
                    img.SetRGB(wxRect({1, 1}, img.GetSize() - wxSize{2, 2}), clr.Red(), clr.Green(), clr.Blue());
                } else {
                    img.SetRGB(wxRect({0, 0}, img.GetSize()), clr.Red(), clr.Green(), clr.Blue());
                }
                bmp = new wxBitmap(img);
                bmp = bitmap_cache().insert(bitmap_key, *bmp);
            }
            return bmp;
        }
    }
    return &sbmp;
}

wxBitmap *PresetComboBox::get_bmp(std::string        bitmap_key,
                                  const std::string &main_icon_name,
                                  const std::string &next_icon_name,
                                    bool is_enabled/* = true*/, bool is_compatible/* = true*/, bool is_system/* = false*/)
{
    // BBS: no icon
#if 1
    static wxBitmap bmp;
    return &bmp;
#else
    bitmap_key += !is_enabled ? "_disabled" : "";
    bitmap_key += is_compatible ? ",cmpt" : ",ncmpt";
    bitmap_key += is_system ? ",syst" : ",nsyst";
    bitmap_key += ",h" + std::to_string(icon_height);
    if (wxGetApp().dark_mode())
        bitmap_key += ",dark";

    wxBitmap* bmp = bitmap_cache().find(bitmap_key);
    if (bmp == nullptr) {
        // Create the bitmap with color bars.
        std::vector<wxBitmap> bmps;
        bmps.emplace_back(m_type == Preset::TYPE_PRINTER ? create_scaled_bitmap(main_icon_name, this, 16, !is_enabled) :
                          is_compatible ? m_bitmapCompatible.bmp() : m_bitmapIncompatible.bmp());
        // Paint a lock at the system presets.
        bmps.emplace_back(is_system ? create_scaled_bitmap(next_icon_name, this, 16, !is_enabled) : bitmap_cache().mkclear(norm_icon_width, icon_height));
        bmp = bitmap_cache().insert(bitmap_key, bmps);
    }

    return bmp;
#endif
}

bool PresetComboBox::is_selected_physical_printer()
{
    auto selected_item = this->GetSelection();
    auto marker = reinterpret_cast<Marker>(this->GetClientData(selected_item));
    return marker == LABEL_ITEM_PHYSICAL_PRINTER;
}

bool PresetComboBox::is_selected_printer_model()
{
    auto selected_item = this->GetSelection();
    auto marker = reinterpret_cast<Marker>(this->GetClientData(selected_item));
    return marker == LABEL_ITEM_PRINTER_MODELS;
}

bool PresetComboBox::selection_is_changed_according_to_physical_printers()
{
    if (m_type != Preset::TYPE_PRINTER || !is_selected_physical_printer())
        return false;

    PhysicalPrinterCollection& physical_printers = m_preset_bundle->physical_printers;

    std::string selected_string = this->GetString(this->GetSelection()).ToUTF8().data();

    std::string old_printer_full_name, old_printer_preset;
    if (physical_printers.has_selection()) {
        old_printer_full_name = physical_printers.get_selected_full_printer_name();
        old_printer_preset = physical_printers.get_selected_printer_preset_name();
    }
    else
        old_printer_preset = m_collection->get_edited_preset().name;
    // Select related printer preset on the Printer Settings Tab
    physical_printers.select_printer(selected_string);
    std::string preset_name = physical_printers.get_selected_printer_preset_name();

    // if new preset wasn't selected, there is no need to call update preset selection
    if (old_printer_preset == preset_name) {
        // we need just to update according Plater<->Tab PresetComboBox
        if (dynamic_cast<PlaterPresetComboBox*>(this)!=nullptr) {
            wxGetApp().get_tab(m_type)->update_preset_choice();
            // Synchronize config.ini with the current selections.
            m_preset_bundle->export_selections(*wxGetApp().app_config);
        }
        else if (dynamic_cast<TabPresetComboBox*>(this)!=nullptr)
            wxGetApp().sidebar().update_presets(m_type);

        this->update();
        return true;
    }

    Tab* tab = wxGetApp().get_tab(Preset::TYPE_PRINTER);
    if (tab)
        tab->select_preset(preset_name, false, old_printer_full_name);
    return true;
}

// ---------------------------------
// ***  PlaterPresetComboBox  ***
// ---------------------------------

PlaterPresetComboBox::PlaterPresetComboBox(wxWindow *parent, Preset::Type preset_type) :
    PresetComboBox(parent, preset_type, wxSize(25 * wxGetApp().em_unit(), 30 * wxGetApp().em_unit() / 10))
{
    GetDropDown().SetUseContentWidth(true,true);

    if (m_type == Preset::TYPE_FILAMENT)
    {
        // BBS: not show color picker
#if 0
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) {
            const Preset* selected_preset = m_collection->find_preset(m_preset_bundle->filament_presets[m_filament_idx]);
            // Wide icons are shown if the currently selected preset is not compatible with the current printer,
            // and red flag is drown in front of the selected preset.
            bool          wide_icons = selected_preset && !selected_preset->is_compatible;
            float scale = m_em_unit*0.1f;

            int shifl_Left = wide_icons ? int(scale * 16 + 0.5) : 0;
#if defined(wxBITMAPCOMBOBOX_OWNERDRAWN_BASED)
            shifl_Left  += int(scale * 4 + 0.5f); // IMAGE_SPACING_RIGHT = 4 for wxBitmapComboBox -> Space left of image
#endif
            int icon_right_pos = shifl_Left + int(scale * (24+4) + 0.5);
            int mouse_pos = event.GetLogicalPosition(wxClientDC(this)).x;
            if (mouse_pos < shifl_Left || mouse_pos > icon_right_pos ) {
                // Let the combo box process the mouse click.
                event.Skip();
                return;
            }

            // BBS
            // Swallow the mouse click and open the color picker.
            //change_extruder_color();
        });
#endif
    }

    // BBS
    if (m_type == Preset::TYPE_FILAMENT) {
        int em = wxGetApp().em_unit();
        clr_picker = new wxBitmapButton(parent, wxID_ANY, {}, wxDefaultPosition, wxSize(FromDIP(20), FromDIP(20)), wxBU_EXACTFIT | wxBU_AUTODRAW | wxBORDER_NONE);
        clr_picker->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
        clr_picker->SetToolTip(_L("Click to pick filament color"));
        clr_picker->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
            // Check if it's an official filament
            auto fila_type = Preset::remove_suffix_modified(GetValue().ToUTF8().data());
            bool is_official = boost::algorithm::starts_with(fila_type, "Bambu");
            if (is_official) {
                // Get filament_id from filament_presets
                const std::string& preset_name = m_preset_bundle->filament_presets[m_filament_idx];
                const Preset* selected_preset = m_collection->find_preset(preset_name);
                wxString fila_id = selected_preset ? wxString::FromUTF8(selected_preset->filament_id) : "GFA00";
                FilamentColor fila_color = get_cur_color_info();

                // Show filament picker dialog
                FilamentPickerDialog dialog(this, fila_id, fila_color, fila_type);

                if (!dialog.IsDataLoaded()) {
                    // If FilamentPicker fails, fallback to default color picker
                    show_default_color_picker();
                } else if (dialog.ShowModal() == wxID_OK) {
                    // Get selected filament color data
                    FilamentColor fila_color = dialog.GetSelectedFilamentColor();

                    // Check if we have valid color data
                    if (!fila_color.GetColors().empty()) {
                        // Convert to storage format. GetColors() is already ordered with the
                        // primary color first.
                        std::vector<std::string> colors;
                        for (const wxColour& color : fila_color.GetColors()) {
                            colors.push_back(color.GetAsString(wxC2S_HTML_SYNTAX).ToStdString());
                        }

                        bool is_gradient = (fila_color.m_color_type == FilamentColor::ColorType::GRADIENT_CLR);
                        this->sync_colour_config(colors, is_gradient);
                    } else {
                        // Fallback to basic color if no FilamentColor data
                        wxColour selected_color = dialog.GetSelectedColour();
                        if (selected_color.IsOk()) {
                            std::vector<std::string> color = {selected_color.GetAsString(wxC2S_HTML_SYNTAX).ToStdString()};
                            this->sync_colour_config(color, false);
                        }
                    }
                }
            } else {
                show_default_color_picker();
            }
            wxCommandEvent *evt = new wxCommandEvent(EVT_FILAMENT_COLOR_CHANGED);
            evt->SetInt(m_filament_idx);
            wxQueueEvent(wxGetApp().plater(), evt);
        });
    }
    else {
        edit_btn = new ScalableButton(parent, wxID_ANY, "cog");
        edit_btn->SetToolTip(_L("Click to edit preset"));

        edit_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent)
            {
                // In a case of a physical printer, for its editing open PhysicalPrinterDialog
                if (m_type == Preset::TYPE_PRINTER
#ifdef __linux__
                    // To edit extruder color from the sidebar
                    || m_type == Preset::TYPE_FILAMENT
#endif //__linux__
                    )
                    show_edit_menu();
                else
                    switch_to_tab();
            });
#ifdef __linux__
        edit_btn->Hide();
#endif //__linux__
    }
}

PlaterPresetComboBox::~PlaterPresetComboBox()
{
    if (edit_btn)
        edit_btn->Destroy();

    // BBS.
    if (clr_picker)
        clr_picker->Destroy();
}

static void run_wizard(ConfigWizard::StartPage sp)
{
    wxGetApp().run_wizard(ConfigWizard::RR_USER, sp);
}

void PlaterPresetComboBox::OnSelect(wxCommandEvent &evt)
{
    auto selected_item = evt.GetSelection();

    auto marker = reinterpret_cast<Marker>(this->GetClientData(selected_item));
    if (marker >= LABEL_ITEM_DISABLED && marker < LABEL_ITEM_MAX) {
        this->SetSelection(m_last_selected);
        if (LABEL_ITEM_WIZARD_ADD_PRINTERS == marker) {
            evt.Skip();
            return;
        }
        evt.StopPropagation();
        if (marker == LABEL_ITEM_MARKER || marker == LABEL_ITEM_DISABLED)
            return;
        //if (marker == LABEL_ITEM_WIZARD_PRINTERS)
        //    show_add_menu();
        //else {
            ConfigWizard::StartPage sp = ConfigWizard::SP_WELCOME;
            switch (marker) {
            case LABEL_ITEM_WIZARD_PRINTERS: sp = ConfigWizard::SP_PRINTERS; break;
            case LABEL_ITEM_WIZARD_FILAMENTS: sp = ConfigWizard::SP_FILAMENTS; break;
            case LABEL_ITEM_WIZARD_MATERIALS: sp = ConfigWizard::SP_MATERIALS; break;
            default: break;
            }
            wxTheApp->CallAfter([sp]() { run_wizard(sp); });
        //}
        return;
    } else if (marker == LABEL_ITEM_PHYSICAL_PRINTER ||  selected_item >= 0 || m_collection->current_is_dirty()) {
        m_last_selected = selected_item;
        if (m_type == Preset::TYPE_FILAMENT)
            update_ams_color();
    }

    evt.Skip();
}

void PlaterPresetComboBox::update_badge_according_flag() {
    // Use the cached authoritative result from the most recent update() (m_last_selected_in_ams) --
    // do NOT re-query filament_ams_list or the FROM_AMS flag here, both can transiently disagree with
    // what update() actually computed.
    ShowBadge(m_last_selected_in_ams);
}

bool PlaterPresetComboBox::switch_to_tab()
{
    Tab* tab = wxGetApp().get_tab(m_type);
    if (!tab)
        return false;

    //BBS  Select NoteBook Tab params
    if (tab->GetParent() == wxGetApp().params_panel())
        wxGetApp().mainframe->select_tab(MainFrame::tp3DEditor);
    else
        wxGetApp().params_dialog()->Popup();
    tab->restore_last_select_item();

    const Preset* selected_filament_preset = nullptr;
    if (m_type == Preset::TYPE_FILAMENT)
    {
        const std::string& selected_preset = GetString(GetSelection()).ToUTF8().data();
        if (!boost::algorithm::starts_with(selected_preset, Preset::suffix_modified()))
        {
            // Open the concrete preset that THIS filament slot actually uses. Several filament slots can
            // share one alias while sitting on different nozzle-diameter variants (multi-diameter mode).
            // get_preset_name_by_alias() has no per-slot context, so resolving the displayed alias would
            // collapse every slot onto a single variant (the main diameter / whichever is selected in the
            // collection) and make one slot's edit bleed into the others. filament_presets[m_filament_idx]
            // is the exact variant of this slot, so prefer it when it still matches the displayed alias.
            auto              preset_bundle = wxGetApp().preset_bundle;
            std::string       preset_name;
            const std::string slot_preset  = (m_filament_idx >= 0 && m_filament_idx < (int) preset_bundle->filament_presets.size())
                ? preset_bundle->filament_presets[m_filament_idx]
                : std::string();
            const Preset *slot_preset_ptr = slot_preset.empty() ? nullptr : preset_bundle->filaments.find_preset(slot_preset);
            if (slot_preset_ptr != nullptr && (slot_preset_ptr->alias == selected_preset || slot_preset_ptr->name == selected_preset))
                preset_name = slot_preset;
            else
                preset_name = preset_bundle->filaments.get_preset_name_by_alias(selected_preset);
            if (wxGetApp().get_tab(m_type)->select_preset(preset_name))
                wxGetApp().get_tab(m_type)->get_combo_box()->set_filament_idx(m_filament_idx);
            else {
                wxGetApp().params_dialog()->Hide();
                return false;
            }
        }
    }

    /*
    if (int page_id = wxGetApp().tab_panel()->FindPage(tab); page_id != wxNOT_FOUND)
    {
        wxGetApp().tab_panel()->SetSelection(page_id);
        // Switch to Settings NotePad
        wxGetApp().mainframe->select_tab();

        //In a case of a multi-material printing, for editing another Filament Preset
        //it's needed to select this preset for the "Filament settings" Tab
        if (m_type == Preset::TYPE_FILAMENT && wxGetApp().extruders_edited_cnt() > 1)
        {
            const std::string& selected_preset = GetString(GetSelection()).ToUTF8().data();
            // Call select_preset() only if there is new preset and not just modified
            if (!boost::algorithm::ends_with(selected_preset, Preset::suffix_modified()))
            {
                const std::string& preset_name = wxGetApp().preset_bundle->filaments.get_preset_name_by_alias(selected_preset);
                wxGetApp().get_tab(m_type)->select_preset(preset_name);
            }
        }
    }
    */

    return true;
}

void PlaterPresetComboBox::change_extruder_color()
{
    // get current color
    DynamicPrintConfig* cfg = &wxGetApp().preset_bundle->project_config;
    auto colors = static_cast<ConfigOptionStrings*>(cfg->option("filament_colour")->clone());
    wxColour clr(colors->values[m_filament_idx]);
    if (!clr.IsOk())
        clr = wxColour(0, 0, 0); // Don't set alfa to transparence

    auto data = new wxColourData();
    data->SetChooseFull(1);
    data->SetColour(clr);

    wxColourDialog dialog(this, data);
    dialog.CenterOnParent();
    if (dialog.ShowModal() == wxID_OK)
    {
        colors->values[m_filament_idx] = dialog.GetColourData().GetColour().GetAsString(wxC2S_HTML_SYNTAX).ToStdString();

        DynamicPrintConfig cfg_new = *cfg;
        cfg_new.set_key_value("filament_colour", colors);

        wxGetApp().get_tab(Preset::TYPE_PRINTER)->load_config(cfg_new);
        this->update();
        wxGetApp().plater()->on_config_change(cfg_new);
    }
}

void PlaterPresetComboBox::show_add_menu()
{
    wxMenu* menu = new wxMenu();

    append_menu_item(menu, wxID_ANY, _L("Add/Remove presets"), "",
        [](wxCommandEvent&) {
            wxTheApp->CallAfter([]() { run_wizard(ConfigWizard::SP_PRINTERS); });
        }, "menu_edit_preset", menu, []() { return true; }, wxGetApp().plater());

    wxGetApp().plater()->PopupMenu(menu);
}

void PlaterPresetComboBox::show_edit_menu()
{
    wxMenu* menu = new wxMenu();

    append_menu_item(menu, wxID_ANY, _L("Edit preset"), "",
        [this](wxCommandEvent&) { this->switch_to_tab(); }, "cog", menu, []() { return true; }, wxGetApp().plater());

#ifdef __linux__
    // To edit extruder color from the sidebar
    if (m_type == Preset::TYPE_FILAMENT) {
        append_menu_item(menu, wxID_ANY, _devL("Change extruder color"), "",
            [this](wxCommandEvent&) { this->change_extruder_color(); }, "blank_14", menu, []() { return true; }, wxGetApp().plater());
        wxGetApp().plater()->PopupMenu(menu);
        return;
    }
#endif //__linux__

    append_menu_item(menu, wxID_ANY, _L("Add/Remove presets"), "",
        [](wxCommandEvent&) {
            wxTheApp->CallAfter([]() { run_wizard(ConfigWizard::SP_PRINTERS); });
        }, "menu_edit_preset", menu, []() { return true; }, wxGetApp().plater());

    wxGetApp().plater()->PopupMenu(menu);
}

wxString PlaterPresetComboBox::get_preset_name(const Preset& preset)
{
    return from_u8(preset.label(false));
}

// Only the compatible presets are shown.
// If an incompatible preset is selected, it is shown as well.
void PlaterPresetComboBox::update()
{
    if (m_type == Preset::TYPE_FILAMENT &&
        (m_preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA ||
        m_preset_bundle->filament_presets.size() <= (size_t)m_filament_idx) )
        return;

    // Otherwise fill in the list from scratch.
    this->Freeze();
    this->Clear();
    invalidate_selection();

    const Preset* selected_filament_preset = nullptr;
    if (m_type == Preset::TYPE_FILAMENT)
    {
        std::vector<wxBitmap *> bitmaps = get_extruder_color_icons(true);
        if (m_filament_idx < bitmaps.size()) {
            clr_picker->SetBitmap(*bitmaps[m_filament_idx]);
        } else {
            return;
        }
#ifdef __WXOSX__
        clr_picker->SetLabel(clr_picker->GetLabel()); // Let setBezelStyle: be called
        clr_picker->Refresh();
#endif
        selected_filament_preset = m_collection->find_preset(m_preset_bundle->filament_presets[m_filament_idx]);
        if (!selected_filament_preset) {
            //can not find this filament, should be caused by project embedded presets, will be updated later
            return;
        }
        //assert(selected_filament_preset);
    }

    bool has_selection = m_collection->get_selected_idx() != size_t(-1);
    const Preset* selected_preset = m_type == Preset::TYPE_FILAMENT ? selected_filament_preset : has_selection ? &m_collection->get_selected_preset() : nullptr;
    // Show wide icons if the currently selected preset is not compatible with the current printer,
    // and draw a red flag in front of the selected preset.
    bool wide_icons = selected_preset && !selected_preset->is_compatible;

    std::map<wxString, wxBitmap*> nonsys_presets;
    //BBS: add project embedded presets logic
    std::map<wxString, wxBitmap*>  project_embedded_presets;
    std::map<wxString, wxBitmap *> system_presets;
    std::map<wxString, wxBitmap *>  uncompatible_presets;
    std::unordered_set<std::string> system_printer_models;
    std::map<wxString, wxString>   preset_descriptions;
    std::map<wxString, std::string> preset_filament_vendors;
    std::map<wxString, std::string> preset_filament_types;
    //BBS:  move system to the end
    wxString selected_system_preset;
    wxString selected_user_preset;
    wxString selected_uncompatible_preset;
    wxString tooltip;
    const std::deque<Preset>& presets = m_collection->get_presets();

    //BBS:  move system to the end
    /*if (!presets.front().is_visible)
        this->set_label_marker(this->Append(separator(L("System presets")), wxNullBitmap));*/

    for (size_t i = presets.front().is_visible ? 0 : m_collection->num_default_presets(); i < presets.size(); ++i)
    {
        const Preset& preset = presets[i];

        bool is_selected =  m_type == Preset::TYPE_FILAMENT ?
                            m_preset_bundle->filament_presets[m_filament_idx] == preset.name :
                            // The case, when some physical printer is selected
                            m_type == Preset::TYPE_PRINTER && m_preset_bundle->physical_printers.has_selection() ? false :
                            i == m_collection->get_selected_idx();

        if (!is_selected && !preset.is_visible)
        {
            continue;
        }
        else if (is_selected && !preset.is_visible)
        {
            Preset& selected_preset = const_cast<Preset&>(preset);
            selected_preset.is_visible = true;
        }

        bool single_bar = false;
        wxString name = get_preset_name(preset);
        if (m_type == Preset::TYPE_FILAMENT)
        {
#if 0
            // Assign an extruder color to the selected item if the extruder color is defined.
            filament_rgb = is_selected ? selected_filament_preset->config.opt_string("filament_colour", 0) :
                                         preset.config.opt_string("filament_colour", 0);
            extruder_rgb = (is_selected && !filament_color.empty()) ? filament_color : filament_rgb;
            single_bar = filament_rgb == extruder_rgb;

            bitmap_key += single_bar ? filament_rgb : filament_rgb + extruder_rgb;
#endif
            if (preset.is_system) {
                if (!preset.is_compatible && preset_filament_vendors.count(name) > 0) {
                    // This exact variant is being dropped as a duplicate of an already-listed,
                    // compatible sibling with the same alias - but if it's the one this slot
                    // actually uses, its alias must still be recorded as the selection here,
                    // before we skip it, or is_selected never fires for this alias below and
                    // update_selection() falls back to item 1 (see comment above at ~line 1512).
                    if (is_selected)
                        selected_system_preset = name;
                    continue;
                }
                else if (preset.is_compatible && preset_filament_vendors.count(name) > 0)
                    uncompatible_presets.erase(name);
                preset_filament_vendors[name] = preset.config.option<ConfigOptionStrings>("filament_vendor")->values.at(0);
                if (preset_filament_vendors[name] == "Bambu Lab")
                    preset_filament_vendors[name] = "Bambu";
                preset_filament_types[name] = preset.config.option<ConfigOptionStrings>("filament_type")->values.at(0);
            }
        }
        wxBitmap* bmp = get_bmp(preset);
        assert(bmp);

        preset_descriptions.emplace(name, _L(preset.description));

        // Diameter-specific system presets share one display alias. If the exact preset used by this
        // slot is currently marked incompatible and a compatible sibling with the same alias was
        // visited first, the duplicate filter below skips the exact preset before recording it as the
        // selection. update_selection() then falls back to item 1, which is commonly an AMS PLA Basic
        // row. Preserve the alias up front so rebuilding the combo keeps showing the current material.
        if (m_type == Preset::TYPE_FILAMENT && preset.is_system && is_selected)
            selected_system_preset = name;

        if (!preset.is_compatible) {
            if (boost::ends_with(name, " template"))
                continue;
            uncompatible_presets.emplace(name, bmp);
            // An incompatible preset can still be the one this slot really uses, e.g. a nozzle-diameter
            // variant the active printer does not accept. It must take part in validate_selection(),
            // otherwise m_last_selected stays INT_MAX and update_selection() falls back to item 1 -
            // the first AMS/nozzle entry - so the slot silently shows another tray's filament and icon.
            if (is_selected) {
                selected_uncompatible_preset = name;
                tooltip                      = get_tooltip(preset);
            }
        }
        else if (preset.is_default || preset.is_system) {
            //BBS: move system to the end
            if (m_type == Preset::TYPE_PRINTER) {
                auto printer_model = preset.config.opt_string("printer_model");
                name = from_u8(printer_model);
                if (system_printer_models.count(printer_model) == 0) {
                    system_presets.emplace(name, bmp);
                    system_printer_models.insert(printer_model);
                }
            } else {
                system_presets.emplace(name, bmp);
            }
            if (is_selected) {
                tooltip = get_tooltip(preset);
                selected_system_preset = name;
            }
            //Append(get_preset_name(preset), *bmp);
            //validate_selection(is_selected);
            //if (is_selected)
                //BBS set tooltip
            //    tooltip = get_tooltip(preset);
        }
        //BBS: add project embedded preset logic
        else if (preset.is_project_embedded)
        {
            project_embedded_presets.emplace(name, bmp);
            if (is_selected) {
                selected_user_preset = name;
                tooltip = wxString::FromUTF8(preset.name.c_str());
            }
        }
        else
        {
            nonsys_presets.emplace(name, bmp);
            if (is_selected) {
                selected_user_preset = name;
                //BBS set tooltip
                tooltip = get_tooltip(preset);
            }
        }
        //BBS: move system to the end
        //if (i + 1 == m_collection->num_default_presets())
        //    set_label_marker(Append(separator(L("System presets")), wxNullBitmap));
    }
    //if (m_type == Preset::TYPE_PRINTER)
    //    add_connected_printers("", true);
    bool selected_in_ams = false;
    if (m_type == Preset::TYPE_FILAMENT) {
        set_replace_text("Bambu", "BambuStudioBlack");
        // Some printers keep a per-nozzle-diameter variant of every filament preset; after restart
        // the currently-selected variant is often the one for a diameter that isn't the active nozzle
        // right now, so it lands in `selected_uncompatible_preset` above and both selected_user_preset
        // AND selected_system_preset stay empty. Without the fallback below, add_ams_filaments would
        // then be called with "" -> no tray name matches -> selected_in_ams stays false -> the local
        // preset row never gets FROM_AMS -> outer sync badge silently disappears on restart, even
        // though tray_is_synced_source (which resolves via m_collection->find_preset directly and
        // isn't gated on is_compatible) still lights up the per-row badges inside the dropdown.
        wxString effective_selected = selected_user_preset.empty() ? selected_system_preset : selected_user_preset;
        if (effective_selected.IsEmpty()) effective_selected = selected_uncompatible_preset;
        selected_in_ams = add_ams_filaments(into_u8(effective_selected), true);
    }

    auto  add_presets       = [this, &preset_descriptions, &preset_filament_vendors, &preset_filament_types, &selected_in_ams]
            (std::map<wxString, wxBitmap *> const &presets, wxString const &selected, std::string const &group, wxString const &groupName) {
        if (!presets.empty()) {
            set_label_marker(Append(_L(group), wxNullBitmap, DD_ITEM_STYLE_SPLIT_ITEM));
            if (m_type == Preset::TYPE_FILAMENT) {
                std::vector<std::map<wxString, wxBitmap *>::value_type const*> list(presets.size(), nullptr);
                std::transform(presets.begin(), presets.end(), list.begin(), [](auto & pair) { return &pair; });
                bool groupByGroup = group != "System presets";
                //if (groupByGroup) {
                //    if (GetCount() == 1) Clear();
                //    else SetString(GetCount() - 1, "");
                //}
                if (group == "System presets" || group == "Unsupported presets") {
                    auto key = [&preset_filament_vendors, &preset_filament_types](const wxString &name) {
                        return FilamentListKey{into_u8(name), preset_filament_vendors[name], preset_filament_types[name]};
                    };
                    std::sort(list.begin(), list.end(), [&key](auto *l, auto *r) { return filament_list_less(key(l->first), key(r->first)); });
                }
                bool unsupported = group == "Unsupported presets";
                for (auto it : list) {
                    auto groupName2 = groupByGroup ? groupName : preset_filament_vendors[it->first];
                    int  index = Append(it->first, *it->second, groupName2, nullptr, unsupported ? DD_ITEM_STYLE_DISABLED : 0);
                    if (unsupported)
                        set_label_marker(index, LABEL_ITEM_DISABLED);
                    SetItemTooltip(index, preset_descriptions[it->first]);
                    bool is_selected = it->first == selected;
                    validate_selection(is_selected);
                    if (is_selected  && selected_in_ams) {
                        SetFlag(GetCount() - 1, (int) FilamentAMSType::FROM_AMS);
                    }
                }
            } else {
                for (std::map<wxString, wxBitmap *>::const_iterator it = presets.begin(); it != presets.end(); ++it) {
                    SetItemTooltip(Append(it->first, *it->second), preset_descriptions[it->first]);
                    if (group == "System presets")
                        set_label_marker(GetCount() - 1, LABEL_ITEM_PRINTER_MODELS);
                    validate_selection(it->first == selected);
                }
            }
        }
    };

    // The selected preset's alias can end up in the unsupported group rather than the system one: the
    // duplicate filter above records it as selected_system_preset before skipping it, but whichever
    // sibling actually put the alias in a map may have been an incompatible one. Re-point the marker at
    // the group that really carries it -- otherwise add_presets() flags no row as current and
    // update_selection() falls back to the first selectable item, silently showing another material.
    // selected_system_preset is left alone: add_ams_filaments() above already consumed it, and
    // add_presets() simply finds no match for it in system_presets.
    if (m_type == Preset::TYPE_FILAMENT && selected_uncompatible_preset.IsEmpty() &&
        !selected_system_preset.IsEmpty() && system_presets.count(selected_system_preset) == 0 &&
        uncompatible_presets.count(selected_system_preset) > 0)
        selected_uncompatible_preset = selected_system_preset;

    //BBS: add project embedded preset logic
    add_presets(project_embedded_presets, selected_user_preset, L("Project-inside presets"), _L("Project") + " ");
    add_presets(nonsys_presets, selected_user_preset, L("User presets"), _L("Custom") + " ");
    // BBS: move system to the end
    add_presets(system_presets, selected_system_preset, L("System presets"), _L("System"));
    add_presets(uncompatible_presets, selected_uncompatible_preset, L("Unsupported presets"), _L("Unsupported") + " ");

    //BBS: remove unused pysical printer logic
    /*if (m_type == Preset::TYPE_PRINTER)
    {
        // add Physical printers, if any exists
        if (!m_preset_bundle->physical_printers.empty()) {
            set_label_marker(Append(separator(L("Physical printers")), wxNullBitmap));
            const PhysicalPrinterCollection& ph_printers = m_preset_bundle->physical_printers;

            for (PhysicalPrinterCollection::ConstIterator it = ph_printers.begin(); it != ph_printers.end(); ++it) {
                for (const std::string& preset_name : it->get_preset_names()) {
                    Preset* preset = m_collection->find_preset(preset_name);
                    if (!preset || !preset->is_visible)
                        continue;
                    std::string main_icon_name, bitmap_key = main_icon_name = preset->printer_technology() == ptSLA ? "sla_printer" : m_main_bitmap_name;
                    wxBitmap* bmp = get_bmp(main_icon_name, wide_icons, main_icon_name);
                    assert(bmp);

                    set_label_marker(Append(from_u8(it->get_full_name(preset_name) + suffix(preset)), *bmp), LABEL_ITEM_PHYSICAL_PRINTER);
                    validate_selection(ph_printers.is_selected(it, preset_name));
                }
            }
        }
    }*/

    if (m_type == Preset::TYPE_PRINTER || m_type == Preset::TYPE_FILAMENT || m_type == Preset::TYPE_SLA_MATERIAL) {
        wxBitmap* bmp = get_bmp("edit_preset_list", wide_icons, "edit_uni");
        assert(bmp);

        if (m_type == Preset::TYPE_FILAMENT)
            set_label_marker(Append(separator(L("Add/Remove filaments")), *bmp), LABEL_ITEM_WIZARD_FILAMENTS);
        else if (m_type == Preset::TYPE_SLA_MATERIAL)
            set_label_marker(Append(separator(L("Add/Remove materials")), *bmp), LABEL_ITEM_WIZARD_MATERIALS);
        else {
            set_label_marker(Append(separator(L("Select/Remove printers(system presets)")), *bmp), LABEL_ITEM_WIZARD_PRINTERS);
            set_label_marker(Append(separator(L("Create printer")), *bmp), LABEL_ITEM_WIZARD_ADD_PRINTERS);
        }
    }

    update_selection();
    if (m_type == Preset::TYPE_FILAMENT) {
        // Cache the authoritative "this slot has a matching AMS tray" result and drive the outer
        // badge from it. Uses the value add_ams_filaments (grouped variant included) just returned
        // via row_matches_current -- matched by filament_id + color, same criteria the per-row
        // dropdown badge (tray_is_synced_source) uses, so outer and dropdown light up together.
        m_last_selected_in_ams = selected_in_ams;
        if (wxGetApp().plater()->is_same_printer_for_connected_and_selected(false))
            ShowBadge(selected_in_ams);
    }
    Thaw();

    if (!tooltip.IsEmpty()) {
#ifdef __WXMSW__
        // From the Windows 2004 the tooltip for preset combobox doesn't work after next call of SetTooltip()
        // (There was an issue, when tooltip doesn't appears after changing of the preset selection)
        // But this workaround seems to work: We should to kill tooltip and than set new tooltip value
        // See, https://groups.google.com/g/wx-users/c/mOEe3fgHrzk
        SetToolTip(NULL);
#endif
        SetToolTip(tooltip);
    }

#ifdef __WXMSW__
    // Use this part of code just on Windows to avoid of some layout issues on Linux
    // Update control min size after rescale (changed Display DPI under MSW)
    if (GetMinWidth() != 10 * m_em_unit)
        SetMinSize(wxSize(10 * m_em_unit, GetSize().GetHeight()));
#endif //__WXMSW__
}

void PlaterPresetComboBox::msw_rescale()
{
    PresetComboBox::msw_rescale();
    SetMinSize({-1, 30 * m_em_unit / 10});

    if (clr_picker)
        clr_picker->SetSize(20 * m_em_unit / 10, 20 * m_em_unit / 10);
    // BBS
    if (edit_btn != nullptr)
        edit_btn->msw_rescale();
}


FilamentColor PlaterPresetComboBox::get_cur_color_info()
{
    std::vector<std::string> filaments_multi_color = Slic3r::GUI::wxGetApp().plater()->get_filament_colors_render_info();
    std::vector<std::string> filament_color_type = Slic3r::GUI::wxGetApp().plater()->get_filament_color_render_type();

    if (m_filament_idx < 0 || m_filament_idx >= static_cast<int>(filaments_multi_color.size())) {
        BOOST_LOG_TRIVIAL(warning) << boost::format("m_filament_idx %1% out of range %2%") % m_filament_idx % filaments_multi_color.size();
        return FilamentColor();
    }

    if (m_filament_idx >= static_cast<int>(filament_color_type.size())) {
        BOOST_LOG_TRIVIAL(warning) << boost::format("m_filament_idx %1% out of range for color_type %2%") % m_filament_idx % filament_color_type.size();
        return FilamentColor();
    }
    std::string filament_color_info = filaments_multi_color[m_filament_idx];
    std::vector<std::string> colors;
    colors = Slic3r::split_string(filament_color_info, ' ');
    FilamentColor fila_color;
    for (const std::string& color_str : colors) {
        if (!color_str.empty()) {
            wxColour color(color_str);
            if (color.IsOk()) {
                fila_color.AddColor(color);
            }
        }
    }

    fila_color.EndSet(filament_color_type[m_filament_idx] == "0" ? 0 : 1);
    return fila_color;
}

void PlaterPresetComboBox::show_default_color_picker()
{
    DynamicPrintConfig* cfg = &wxGetApp().preset_bundle->project_config;
    auto colors = static_cast<ConfigOptionStrings*>(cfg->option("filament_colour")->clone());
    wxColour current_clr(colors->values[m_filament_idx]);
    if (!current_clr.IsOk())
        current_clr = wxColour(0, 0, 0); // Don't set alfa to transparence

    m_clrData.SetColour(current_clr);

    wxColourData data = show_sys_picker_dialog(this, m_clrData);
    if (m_clrData.GetColour() != data.GetColour()) {
        std::vector<std::string> color = {data.GetColour().GetAsString(wxC2S_HTML_SYNTAX).ToStdString()};
        m_clrData.SetColour(data.GetColour());
        sync_colour_config(color, false);
    }
}

void PlaterPresetComboBox::sync_colour_config(const std::vector<std::string> &clrs, bool is_gradient)
{
    DynamicPrintConfig *cfg = &wxGetApp().preset_bundle->project_config;

    // Clone the string vector and patch the value at current extruder index.
    auto multi_colour_opt = static_cast<ConfigOptionStrings *>(cfg->option("filament_multi_colour")->clone());
    auto colour_type_opt = static_cast<ConfigOptionStrings *>(cfg->option("filament_colour_type")->clone());
    auto colour_opt = static_cast<ConfigOptionStrings *>(cfg->option("filament_colour")->clone());

    if (m_filament_idx >= multi_colour_opt->values.size()) multi_colour_opt->values.resize(m_filament_idx + 1);
    if (m_filament_idx >= colour_type_opt->values.size()) colour_type_opt->values.resize(m_filament_idx + 1);
    if (m_filament_idx >= colour_opt->values.size()) colour_opt->values.resize(m_filament_idx + 1);

    std::string clr_str = "";
    for(auto &clr : clrs) {
        clr_str += clr + " ";
    }
    clr_str.pop_back();

    multi_colour_opt->values[m_filament_idx] = clr_str;
    colour_opt->values[m_filament_idx] = clrs[0];
    colour_type_opt->values[m_filament_idx] = is_gradient ? "0" : "1";
    DynamicPrintConfig cfg_new = *cfg;
    cfg_new.set_key_value("filament_multi_colour", multi_colour_opt);
    cfg_new.set_key_value("filament_colour", colour_opt);
    cfg_new.set_key_value("filament_colour_type", colour_type_opt);
    cfg->apply(cfg_new);

    wxGetApp().plater()->update_project_dirty_from_presets();

    wxGetApp().preset_bundle->export_selections(*wxGetApp().app_config);
    update();  // refresh the preset combobox with new config

    wxGetApp().plater()->on_config_change(cfg_new);
}

// ---------------------------------
// ***  TabPresetComboBox  ***
// ---------------------------------

TabPresetComboBox::TabPresetComboBox(wxWindow* parent, Preset::Type preset_type) :
    // BBS: new layout
    PresetComboBox(parent, preset_type, wxSize(20 * wxGetApp().em_unit(), 30 * wxGetApp().em_unit() / 10))
{
}

void TabPresetComboBox::OnSelect(wxCommandEvent &evt)
{
    // Under OSX: in case of use of a same names written in different case (like "ENDER" and "Ender")
    // m_presets_choice->GetSelection() will return first item, because search in PopupListCtrl is case-insensitive.
    // So, use GetSelection() from event parameter
    auto selected_item = evt.GetSelection();

    auto marker = reinterpret_cast<Marker>(this->GetClientData(selected_item));
    if (marker >= LABEL_ITEM_DISABLED && marker < LABEL_ITEM_MAX) {
        this->SetSelection(m_last_selected);
        // BBS: Add/Remove filaments
        ConfigWizard::StartPage sp = ConfigWizard::SP_WELCOME;
        switch (marker) {
        case LABEL_ITEM_WIZARD_PRINTERS: sp = ConfigWizard::SP_PRINTERS; break;
        case LABEL_ITEM_WIZARD_FILAMENTS: sp = ConfigWizard::SP_FILAMENTS; break;
        case LABEL_ITEM_WIZARD_MATERIALS: sp = ConfigWizard::SP_MATERIALS; break;
        default: break;
        }
        if (sp != ConfigWizard::SP_WELCOME) {
            wxTheApp->CallAfter([this, sp]() {
                run_wizard(sp);
            });
        }
    }
    else if (const auto target = m_item_mode_targets.find(selected_item); target != m_item_mode_targets.end()) {
        // An item of the other multi-diameter mode: the combo keeps showing the preset in use, and the
        // switch runs once this event is done, because it rebuilds this very combo.
        this->SetSelection(m_last_selected);
        const MultiNozzleModeTarget mode_target = target->second;
        wxGetApp().CallAfter([mode_target]() {
            wxGetApp().plater()->sidebar().select_printer_across_multi_nozzle_mode(mode_target.printer_name,
                                                                                   mode_target.to_multi);
        });
    }
    else if (on_selection_changed && (m_last_selected != selected_item || m_collection->current_is_dirty())) {
        m_last_selected = selected_item;
        // BBS: ams
        update_ams_color();
        on_selection_changed(selected_item);
    }

    evt.StopPropagation();
#ifdef __WXMSW__
    // From the Win 2004 preset combobox lose a focus after change the preset selection
    // and that is why the up/down arrow doesn't work properly
    // So, set the focus to the combobox explicitly
    this->SetFocus();
#endif
}

wxString TabPresetComboBox::get_preset_name(const Preset& preset)
{
    // The "modified" mark is prepended by hand here, because the rewrite replaces part of the name
    // and so cannot go through Preset::label(), which is what supplies the mark on the plain path.
    const std::string display_name = multi_nozzle_display_name(m_preset_bundle, m_type, preset);
    if (!display_name.empty())
        return from_u8((preset.is_dirty ? Preset::suffix_modified() : "") + display_name);
    return from_u8(preset.label(true));
}

// Update the choice UI from the list of presets.
// If show_incompatible, all presets are shown, otherwise only the compatible presets are shown.
// If an incompatible preset is selected, it is shown as well.
void TabPresetComboBox::update()
{
    const std::deque<Preset>& presets = m_collection->get_presets();

    struct PresetItem {
        wxBitmap* bitmap;
        bool enabled;
        std::string preset_name;
        wxString description;
        std::optional<MultiNozzleModeTarget> mode_target;
    };
    std::map<wxString, PresetItem> nonsys_presets;
    //BBS: add project embedded presets logic
    std::map<wxString, PresetItem>  project_embedded_presets;
    //BBS:  move system to the end
    std::map<wxString, PresetItem>  system_presets;
    std::vector<wxString> available_presets;

    wxString selected = "";
    //BBS:  move system to the end
    /*if (!presets.front().is_visible)
        set_label_marker(Append(separator(L("System presets")), wxNullBitmap));*/
    size_t idx_selected = m_collection->get_selected_idx();
    const bool multi_nozzle_mode = m_preset_bundle != nullptr &&
        multi_nozzle_diameter_mode_enabled(m_preset_bundle->project_config);

    // unselect printer
    if (m_type == Preset::TYPE_PRINTER && m_preset_bundle->physical_printers.has_selection()) {
        std::string sel_preset_name = m_preset_bundle->physical_printers.get_selected_printer_preset_name();
        Preset* preset = m_collection->find_preset(sel_preset_name);
        if (!preset) m_preset_bundle->physical_printers.unselect_printer();
    }

    for (size_t i = presets.front().is_visible ? 0 : m_collection->num_default_presets(); i < presets.size(); ++i)
    {
        const Preset& preset = presets[i];
        const bool installed_multi_nozzle =
            m_type == Preset::TYPE_PRINTER && installed_multi_nozzle_system_printer(*m_collection, preset, multi_nozzle_mode);
        const bool is_canonical_multi_nozzle =
            installed_multi_nozzle && is_canonical_multi_nozzle_diameter_printer_preset(*m_collection, preset);
        if ((!preset.is_visible && !is_canonical_multi_nozzle) ||
            (!show_incompatible && !preset.is_compatible && i != idx_selected))
            continue;

        // marker used for disable incompatible printer models for the selected physical printer
        bool is_enabled = true;

        wxBitmap* bmp = get_bmp(preset);
        assert(bmp);

        const wxString name = get_preset_name(preset);
        available_presets.push_back(name);

        const wxString description = preset.is_system ? _L(preset.description) : wxString();

        if (preset.is_default || preset.is_system) {
            //BBS: move system to the end
            if (installed_multi_nozzle) {
                // In the printer settings combo, list all single-diameter system variants alongside the
                // "Multiple nozzle" entry in both modes; picking the other mode's entry switches the mode.
                if (multi_nozzle_mode) {
                    if (preset.is_visible || i == idx_selected) {
                        const wxString single_name = (!is_canonical_multi_nozzle && i == idx_selected) ?
                            name : from_u8(preset.name);
                        available_presets.push_back(single_name + " >single");
                        system_presets.emplace(single_name, PresetItem{bmp, is_enabled, preset.name, description,
                                                                       MultiNozzleModeTarget{preset.name, false}});
                        if (!is_canonical_multi_nozzle && i == idx_selected)
                            selected = single_name;
                    }
                    if (is_canonical_multi_nozzle) {
                        system_presets.emplace(name, PresetItem{bmp, is_enabled, preset.name, description, std::nullopt});
                        if (i == idx_selected)
                            selected = name;
                    }
                } else {
                    if (preset.is_visible || i == idx_selected) {
                        system_presets.emplace(name, PresetItem{bmp, is_enabled, preset.name, description, std::nullopt});
                        if (i == idx_selected)
                            selected = name;
                    }
                    if (is_canonical_multi_nozzle) {
                        const wxString multi_name = from_u8(multi_nozzle_printer_preset_name(preset));
                        available_presets.push_back(multi_name + " >multi");
                        system_presets.emplace(multi_name, PresetItem{bmp, is_enabled, preset.name, description,
                                                                      MultiNozzleModeTarget{preset.name, true}});
                    }
                }
            } else {
                system_presets.emplace(name, PresetItem{bmp, is_enabled, preset.name, description, std::nullopt});
                if (i == idx_selected)
                    selected = name;
            }
            //int item_id = Append(get_preset_name(preset), *bmp);
            //if (!is_enabled)
            //    set_label_marker(item_id, LABEL_ITEM_DISABLED);
            //validate_selection(i == idx_selected);
        }
        //BBS: add project embedded preset logic
        else if (preset.is_project_embedded)
        {
            //std::pair<wxBitmap*, bool> pair(bmp, is_enabled);
            std::optional<MultiNozzleModeTarget> mode_target;
            if (m_type == Preset::TYPE_PRINTER && m_preset_bundle != nullptr && i != idx_selected &&
                supports_multi_nozzle_diameter_mode(*m_collection, preset)) {
                const bool preset_multi_mode = m_preset_bundle->is_multi_nozzle_user_printer_preset(preset);
                if (preset_multi_mode != multi_nozzle_mode) {
                    mode_target = MultiNozzleModeTarget{preset.name, preset_multi_mode};
                    available_presets.back() += preset_multi_mode ? " >multi" : " >single";
                }
            }
            project_embedded_presets.emplace(name, PresetItem{bmp, is_enabled, preset.name, description, mode_target});
            if (i == idx_selected)
                selected = name;
        }
        else
        {
            std::optional<MultiNozzleModeTarget> mode_target;
            if (m_type == Preset::TYPE_PRINTER && m_preset_bundle != nullptr && i != idx_selected &&
                supports_multi_nozzle_diameter_mode(*m_collection, preset)) {
                const bool preset_multi_mode = m_preset_bundle->is_multi_nozzle_user_printer_preset(preset);
                if (preset_multi_mode != multi_nozzle_mode) {
                    mode_target = MultiNozzleModeTarget{preset.name, preset_multi_mode};
                    available_presets.back() += preset_multi_mode ? " >multi" : " >single";
                }
            }
            nonsys_presets.emplace(name, PresetItem{bmp, is_enabled, preset.name, description, mode_target});
            if (i == idx_selected)
                selected = name;
        }
        //BBS: move system to the end
        //if (i + 1 == m_collection->num_default_presets())
        //    set_label_marker(Append(separator(L("System presets")), wxNullBitmap));
    }

    if (m_type == Preset::TYPE_FILAMENT)
        add_ams_filaments(into_u8(selected));

    // workaround for updating too many times, which may casue ui flicking
    if(m_last_presets == available_presets && m_last_select_name == selected) return;
    m_last_presets = std::move(available_presets);
    m_last_select_name = selected;

    Freeze();
    Clear();
    m_item_preset_names.clear();
    m_item_mode_targets.clear();
    invalidate_selection();

    //BBS: add project embedded preset logic
    if (!project_embedded_presets.empty())
    {
        set_label_marker(Append(_L("Project-inside presets"), wxNullBitmap, DD_ITEM_STYLE_SPLIT_ITEM));
        for (auto it = project_embedded_presets.begin(); it != project_embedded_presets.end(); ++it) {
            int item_id = Append(it->first, *it->second.bitmap);
            if (it->second.mode_target)
                m_item_mode_targets.emplace(item_id, *it->second.mode_target);
            else
                m_item_preset_names.emplace(item_id, it->second.preset_name);
            SetItemTooltip(item_id, it->second.description);
            bool is_enabled = it->second.enabled;
            if (!is_enabled)
                set_label_marker(item_id, LABEL_ITEM_DISABLED);
            validate_selection(it->first == selected);
        }
    }
    if (!nonsys_presets.empty())
    {
        set_label_marker(Append(_L("User presets"), wxNullBitmap, DD_ITEM_STYLE_SPLIT_ITEM));
        for (auto it = nonsys_presets.begin(); it != nonsys_presets.end(); ++it) {
            int item_id = Append(it->first, *it->second.bitmap);
            if (it->second.mode_target)
                m_item_mode_targets.emplace(item_id, *it->second.mode_target);
            else
                m_item_preset_names.emplace(item_id, it->second.preset_name);
            SetItemTooltip(item_id, it->second.description);
            bool is_enabled = it->second.enabled;
            if (!is_enabled)
                set_label_marker(item_id, LABEL_ITEM_DISABLED);
            validate_selection(it->first == selected);
        }
    }
    //BBS: move system to the end
    if (!system_presets.empty())
    {
        set_label_marker(Append(_L("System presets"), wxNullBitmap, DD_ITEM_STYLE_SPLIT_ITEM));
        for (auto it = system_presets.begin(); it != system_presets.end(); ++it) {
            int item_id = Append(it->first, *it->second.bitmap);
            if (it->second.mode_target)
                m_item_mode_targets.emplace(item_id, *it->second.mode_target);
            else
                m_item_preset_names.emplace(item_id, it->second.preset_name);
            SetItemTooltip(item_id, it->second.description);
            bool is_enabled = it->second.enabled;
            if (!is_enabled)
                set_label_marker(item_id, LABEL_ITEM_DISABLED);
            validate_selection(it->first == selected);
        }
    }

    if (m_type == Preset::TYPE_PRINTER)
    {
        //BBS: remove unused pysical printer logic
        /*// add Physical printers, if any exists
        if (!m_preset_bundle->physical_printers.empty()) {
            set_label_marker(Append(separator(L("Physical printers")), wxNullBitmap));
            const PhysicalPrinterCollection& ph_printers = m_preset_bundle->physical_printers;

            for (PhysicalPrinterCollection::ConstIterator it = ph_printers.begin(); it != ph_printers.end(); ++it) {
                for (const std::string& preset_name : it->get_preset_names()) {
                    Preset* preset = m_collection->find_preset(preset_name);
                    if (!preset || !preset->is_visible)
                        continue;
                    std::string main_icon_name = preset->printer_technology() == ptSLA ? "sla_printer" : m_main_bitmap_name;

                    wxBitmap* bmp = get_bmp(main_icon_name, main_icon_name, "", true, true, false);
                    assert(bmp);

                    set_label_marker(Append(from_u8(it->get_full_name(preset_name) + suffix(preset)), *bmp), LABEL_ITEM_PHYSICAL_PRINTER);
                    validate_selection(ph_printers.is_selected(it, preset_name));
                }
            }
        }*/

        // add "Add/Remove printers" item
        //std::string icon_name = "edit_uni";
        //wxBitmap* bmp = get_bmp("edit_preset_list, tab,", icon_name, "");
        //assert(bmp);

        //set_label_marker(Append(separator(L("Add/Remove printers")), *bmp), LABEL_ITEM_WIZARD_PRINTERS);
    }

    // BBS Add/Remove filaments select
    //wxBitmap* bmp = get_bmp("edit_preset_list", false, "edit_uni");
    //assert(bmp);
    //if (m_type == Preset::TYPE_FILAMENT)
    //    set_label_marker(Append(separator(L("Add/Remove filaments")), *bmp), LABEL_ITEM_WIZARD_FILAMENTS);
    //else if (m_type == Preset::TYPE_SLA_MATERIAL)
    //    set_label_marker(Append(separator(L("Add/Remove materials")), *bmp), LABEL_ITEM_WIZARD_MATERIALS);

    update_selection();
    Thaw();
}

void TabPresetComboBox::msw_rescale()
{
    PresetComboBox::msw_rescale();
    // BBS: new layout
    wxSize sz = wxSize(20 * m_em_unit, 30 * m_em_unit / 10);
    SetMinSize(sz);
    SetSize(sz);
}

void TabPresetComboBox::update_dirty()
{
    // 1) Update the dirty flag of the current preset.
    m_collection->update_dirty();

    // 2) Update the labels.
    wxWindowUpdateLocker noUpdates(this);
    for (unsigned int ui_id = 0; ui_id < GetCount(); ++ui_id) {
        auto marker = reinterpret_cast<Marker>(this->GetClientData(ui_id));
        if (marker >= LABEL_ITEM_MARKER)
            continue;
        // Items of the other multi-diameter mode keep their label: the preset behind them is not the
        // one in use, and the label says which mode the pick leads to.
        if (m_item_mode_targets.count(ui_id) > 0)
            continue;

        std::string   old_label = GetString(ui_id).utf8_str().data();
        const auto    mapped_name = m_item_preset_names.find(ui_id);
        std::string   preset_name = mapped_name == m_item_preset_names.end() ?
            Preset::remove_suffix_modified(old_label) : mapped_name->second;
        std::string   ph_printer_name;

        if (marker == LABEL_ITEM_PHYSICAL_PRINTER) {
            ph_printer_name = PhysicalPrinter::get_short_name(preset_name);
            preset_name = PhysicalPrinter::get_preset_name(preset_name);
        }

        Preset* preset = m_collection->find_preset(preset_name, false);
        if (preset) {
            std::string new_label = get_preset_name(*preset).ToUTF8().data();

            if (marker == LABEL_ITEM_PHYSICAL_PRINTER)
                new_label = ph_printer_name + PhysicalPrinter::separator() + new_label;

            if (old_label != new_label) {
                SetString(ui_id, from_u8(new_label));
                SetItemBitmap(ui_id, *get_bmp(*preset));
                if (ui_id == GetSelection()) SetToolTip(wxString::FromUTF8(new_label.c_str())); // BBS
            }
        }
    }
#ifdef __APPLE__
    // wxWidgets on OSX do not upload the text of the combo box line automatically.
    // Force it to update by re-selecting.
    SetSelection(GetSelection());
#endif /* __APPLE __ */
}

} // namespace GUI
GUI::CalibrateFilamentComboBox::CalibrateFilamentComboBox(wxWindow *parent)
: PlaterPresetComboBox(parent, Preset::TYPE_FILAMENT)
{
    m_chip_size = wxSize(FromDIP(20), FromDIP(20));
    clr_picker->SetMinSize(m_chip_size);
    clr_picker->SetSize(m_chip_size);
    clr_picker->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    clr_picker->SetToolTip("");
    clr_picker->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {});
}

GUI::CalibrateFilamentComboBox::~CalibrateFilamentComboBox()
{
}

void GUI::CalibrateFilamentComboBox::SetChipSize(const wxSize &size)
{
    if (size.x <= 0 || size.y <= 0 || size == m_chip_size) return;

    m_chip_size = size;
    if (!clr_picker) return;

    clr_picker->SetMinSize(m_chip_size);
    clr_picker->SetSize(m_chip_size);

    if (m_filament_exist || !m_filament_color.empty()) {
        const std::string color = m_filament_exist ? m_filament_color : std::string("#F0F0F0FF");
        clr_picker->SetBitmap(*get_extruder_color_icon(color, m_tray_name, m_chip_size.x, m_chip_size.y));
        clr_picker->SetBitmapDisabled(*get_extruder_color_icon(color, m_tray_name, m_chip_size.x, m_chip_size.y));
    }
}

void GUI::CalibrateFilamentComboBox::load_tray(const DynamicPrintConfig &config)
{
    m_tray_name = config.opt_string("tray_name", 0u);
    size_t pos = m_tray_name.find("HT-");
    if (pos != std::string::npos) {
        m_tray_name = m_tray_name.substr(pos + 3);
    }
    m_filament_id = config.opt_string("filament_id", 0u);
    m_tag_uid = config.opt_string("tag_uid", 0u);
    m_filament_type  = config.opt_string("filament_type", 0u);
    m_filament_color = config.opt_string("filament_colour", 0u);
    m_filament_exist = config.opt_bool("filament_exist", 0u);
    wxColor clr(m_filament_color);
    clr_picker->SetBitmap(*get_extruder_color_icon(m_filament_color, m_tray_name, m_chip_size.x, m_chip_size.y));
    clr_picker->SetBitmapDisabled(*get_extruder_color_icon(m_filament_color, m_tray_name, m_chip_size.x, m_chip_size.y));
#ifdef __WXOSX__
    clr_picker->SetLabel(clr_picker->GetLabel()); // Let setBezelStyle: be called
    clr_picker->Refresh();
#endif
    if (!m_filament_exist) {
        SetValue(_L("Empty"));
        m_selected_preset = nullptr;
        m_is_compatible = false;
        clr_picker->SetBitmap(*get_extruder_color_icon("#F0F0F0FF", m_tray_name, m_chip_size.x, m_chip_size.y));
        clr_picker->SetBitmapDisabled(*get_extruder_color_icon("#F0F0F0FF", m_tray_name, m_chip_size.x, m_chip_size.y));
    } else {
        auto &filaments = m_collection->get_presets();
        auto  iter      = std::find_if(filaments.begin(), filaments.end(), [this](auto &f) {
            if (!f.is_system) // Only match system preset
                return false;
            bool is_compatible = m_calibrate_filaments.find(f.name) != m_calibrate_filaments.end();
            return is_compatible && f.filament_id == m_filament_id;
            });

        // Prioritize matching system presets. If there are no system presets, match all presets.
        if (iter == filaments.end()) {
            iter = std::find_if(filaments.begin(), filaments.end(), [this](auto &f) {
                if (f.is_system) // Only match system preset
                    return false;
                bool is_compatible = m_calibrate_filaments.find(f.name) != m_calibrate_filaments.end();
                return is_compatible && f.filament_id == m_filament_id;
            });
        }
        //if (iter == filaments.end() && !m_filament_type.empty()) {
        //    auto filament_type = "Generic " + m_filament_type;
        //    iter               = std::find_if(filaments.begin(), filaments.end(),
        //                        [this , &filament_type](auto &f) {
        //            bool is_compatible = m_preset_bundle->calibrate_filaments.find(&f) != m_preset_bundle->calibrate_filaments.end();
        //            return is_compatible && f.is_system && boost::algorithm::starts_with(f.name, filament_type); });
        //}
        if (iter != filaments.end()) {
            m_selected_preset = &*iter;
            m_is_compatible = true;
            SetValue(get_preset_name(*iter));
        }
        else {
            m_selected_preset = nullptr;
            m_is_compatible = false;
            SetValue(_L("Incompatible"));
        }
        Enable();
    }
}

void GUI::CalibrateFilamentComboBox::update()
{
    if (m_preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA)
        return;

    // Otherwise fill in the list from scratch.
    this->Freeze();
    this->Clear();
    invalidate_selection();

    const Preset* selected_filament_preset = nullptr;

    m_nonsys_presets.clear();
    m_system_presets.clear();

    wxString selected_preset = m_selected_preset ? get_preset_name(*m_selected_preset) : GetValue();

    wxString tooltip;
    const std::deque<Preset>& presets = m_collection->get_presets();

    for (size_t i = presets.front().is_visible ? 0 : m_collection->num_default_presets(); i < presets.size(); ++i)
    {
        const Preset& preset = presets[i];
        auto display_name = get_preset_name(preset);
        bool          is_selected   = m_selected_preset == &preset;
        if (m_calibrate_filaments.empty()) {
            Thaw();
            return;
        }
        bool          is_compatible = m_calibrate_filaments.find(preset.name) != m_calibrate_filaments.end();
        ;
        if (!preset.is_visible || (!is_compatible && !is_selected))
            continue;

        if (is_selected) {
            tooltip = get_tooltip(preset);
        }

        wxBitmap* bmp = get_bmp(preset);
        assert(bmp);

        if (preset.is_default || preset.is_system) {
            m_system_presets.emplace(display_name, std::make_pair( preset.name, bmp ));
        }
        else {
            m_nonsys_presets.emplace(display_name, std::make_pair( preset.name, bmp ));
        }

    }

    if (!m_nonsys_presets.empty())
    {
        set_label_marker(Append(_L("User presets"), wxNullBitmap, DD_ITEM_STYLE_SPLIT_ITEM));
        for (auto it = m_nonsys_presets.begin(); it != m_nonsys_presets.end(); ++it) {
            Append(it->first, *(it->second.second));
            validate_selection(it->first == selected_preset);
        }
    }
    if (!m_system_presets.empty())
    {
        set_label_marker(Append(_L("System presets"), wxNullBitmap, DD_ITEM_STYLE_SPLIT_ITEM));
        for (auto it = m_system_presets.begin(); it != m_system_presets.end(); ++it) {
            Append(it->first, *(it->second.second));
            validate_selection(it->first == selected_preset);
        }
    }

    update_selection();
    Thaw();

    SetToolTip(NULL);
}

void GUI::CalibrateFilamentComboBox::msw_rescale()
{
    if (clr_picker) {
        clr_picker->SetSize(m_chip_size);
        clr_picker->SetMinSize(m_chip_size);
        clr_picker->SetBitmap(*get_extruder_color_icon(m_filament_color, m_tray_name, m_chip_size.x, m_chip_size.y));
    }
    // BBS
    if (edit_btn != nullptr)
        edit_btn->msw_rescale();
}

void GUI::CalibrateFilamentComboBox::OnSelect(wxCommandEvent &evt)
{
    auto marker = reinterpret_cast<Marker>(this->GetClientData(evt.GetSelection()));
    if (marker >= LABEL_ITEM_DISABLED && marker < LABEL_ITEM_MAX) {
        this->SetSelection(evt.GetSelection() + 1);
        wxCommandEvent event(wxEVT_COMBOBOX);
        event.SetInt(evt.GetSelection() + 1);
        event.SetString(GetString(evt.GetSelection() + 1));
        wxPostEvent(this, event);
        return;
    }
    m_is_compatible = true;
    static_cast<FilamentComboBox*>(m_parent)->Enable(true);

    wxString display_name = evt.GetString();
    std::string preset_name;
    if (m_system_presets.find(evt.GetString()) != m_system_presets.end()) {
        preset_name = m_system_presets.at(display_name).first;
    }
    else if (m_nonsys_presets.find(evt.GetString()) != m_nonsys_presets.end()) {
        preset_name = m_nonsys_presets.at(display_name).first;
    }
    m_selected_preset       = m_collection->find_preset(preset_name);

    // if the selected preset is null, do not send tray_change event
    if (!m_selected_preset) {
        MessageDialog msg_dlg(nullptr, _L("The selected preset is null!"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    wxCommandEvent e(EVT_CALI_TRAY_CHANGED);
    e.SetEventObject(m_parent);
    wxPostEvent(m_parent, e);
}

GUI::VirtualFilamentComboBox::VirtualFilamentComboBox(wxWindow *parent, const std::string &preset_name, const std::string &color_hex)
    : PlaterPresetComboBox(parent, Preset::TYPE_FILAMENT)
    , m_preset_name(preset_name)
    , m_color_hex(color_hex)
{
    clr_picker->SetToolTip(_L("Click to pick filament color"));
    // Bound after the base handler, so it runs first and stops the event: the base handler
    // edits project_config through m_filament_idx, which this combo does not own.
    clr_picker->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { pick_color(); });
    update_color_chip();
    update();
}

void GUI::VirtualFilamentComboBox::set_color(const std::string &color_hex)
{
    m_color_hex = color_hex;
    update_color_chip();
}

void GUI::VirtualFilamentComboBox::update_color_chip()
{
    if (!clr_picker)
        return;
    const int   em     = wxGetApp().em_unit();
    const int   width  = lround(2 * em);
    const int   height = lround(2 * em);
    wxBitmap   *bmp    = get_extruder_color_icon(m_color_hex, "", width, height);
    if (bmp) {
        clr_picker->SetBitmap(*bmp);
        clr_picker->SetBitmapDisabled(*bmp);
    }
#ifdef __WXOSX__
    clr_picker->SetLabel(clr_picker->GetLabel()); // Let setBezelStyle: be called
#endif
    clr_picker->Refresh();
}

void GUI::VirtualFilamentComboBox::pick_color()
{
    const std::string fila_type   = Preset::remove_suffix_modified(GetValue().ToUTF8().data());
    const bool        is_official = boost::algorithm::starts_with(fila_type, "Bambu");
    std::string       new_color;

    auto pick_from_system_dialog = [this, &new_color]() {
        wxColour current_clr(m_color_hex);
        if (!current_clr.IsOk())
            current_clr = wxColour(0, 0, 0);
        m_clrData.SetColour(current_clr);
        wxColourData data = show_sys_picker_dialog(this, m_clrData);
        if (data.GetColour() != current_clr) {
            m_clrData.SetColour(data.GetColour());
            new_color = data.GetColour().GetAsString(wxC2S_HTML_SYNTAX).ToStdString();
        }
    };

    if (is_official) {
        const Preset *preset  = m_collection->find_preset(m_preset_name);
        wxString      fila_id = preset ? wxString::FromUTF8(preset->filament_id) : "GFA00";
        FilamentColor fila_color;
        wxColour      current_clr(m_color_hex);
        if (current_clr.IsOk())
            fila_color.AddColor(current_clr);
        fila_color.EndSet(1);

        FilamentPickerDialog dialog(this, fila_id, fila_color, fila_type);
        if (!dialog.IsDataLoaded()) {
            pick_from_system_dialog();
        } else if (dialog.ShowModal() == wxID_OK) {
            const FilamentColor &picked = dialog.GetSelectedFilamentColor();
            if (picked.GetColors().size() > 1 || picked.m_color_type == FilamentColor::ColorType::GRADIENT_CLR) {
                MessageDialog msg(this, _L("Color mixing only supports single-color filaments."), _L("Warning"), wxOK | wxICON_WARNING);
                msg.ShowModal();
                return;
            }
            if (!picked.GetColors().empty()) {
                new_color = picked.GetColors().front().GetAsString(wxC2S_HTML_SYNTAX).ToStdString();
            } else {
                wxColour selected_color = dialog.GetSelectedColour();
                if (selected_color.IsOk())
                    new_color = selected_color.GetAsString(wxC2S_HTML_SYNTAX).ToStdString();
            }
        }
    } else {
        pick_from_system_dialog();
    }

    if (new_color.empty() || new_color == m_color_hex)
        return;
    set_color(new_color);
    if (m_on_changed)
        m_on_changed();
}

// Same presets as the sidebar filament combo (visible and compatible, the chosen one always
// listed), without the AMS block and without any per-slot project state.
void GUI::VirtualFilamentComboBox::update()
{
    if (m_preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA)
        return;

    this->Freeze();
    this->Clear();
    invalidate_selection();
    m_item_presets.clear();

    struct Entry
    {
        wxBitmap   *bmp = nullptr;
        std::string vendor;
        std::string type;
        std::string preset_name;
    };
    std::map<wxString, Entry> user_presets;
    std::map<wxString, Entry> system_presets;
    wxString                  selected_name;
    wxString                  tooltip;

    auto first_string = [](const Preset &preset, const char *key) -> std::string {
        auto *opt = preset.config.option<ConfigOptionStrings>(key);
        return (opt && !opt->values.empty()) ? opt->values.front() : std::string();
    };

    const std::deque<Preset> &presets = m_collection->get_presets();
    for (size_t i = presets.front().is_visible ? 0 : m_collection->num_default_presets(); i < presets.size(); ++i) {
        const Preset &preset      = presets[i];
        const bool    is_selected = preset.name == m_preset_name;
        if (!is_selected && (!preset.is_visible || !preset.is_compatible))
            continue;

        const wxString name  = get_preset_name(preset);
        auto          &group = (preset.is_default || preset.is_system) ? system_presets : user_presets;
        // Diameter variants share one alias; keep the first one unless this is the chosen preset.
        if (group.count(name) > 0 && !is_selected)
            continue;

        Entry entry;
        entry.bmp = get_bmp(preset);
        if (preset.is_system) {
            entry.vendor = first_string(preset, "filament_vendor");
            if (entry.vendor == "Bambu Lab")
                entry.vendor = "Bambu";
            entry.type = first_string(preset, "filament_type");
        }
        entry.preset_name = preset.name;
        group[name]       = entry;
        if (is_selected) {
            selected_name = name;
            tooltip       = get_tooltip(preset);
        }
    }

    set_replace_text("Bambu", "BambuStudioBlack");

    auto record_item = [this](int item, bool is_system, const std::string &preset_name) {
        if (item < 0)
            return;
        if (m_item_presets.size() <= size_t(item))
            m_item_presets.resize(size_t(item) + 1);
        m_item_presets[item] = {is_system, preset_name};
    };
    const bool selected_is_system = [this]() {
        const Preset *preset = m_collection->find_preset(m_preset_name);
        return preset && (preset->is_default || preset->is_system);
    }();

    if (!user_presets.empty()) {
        set_label_marker(Append(_L("User presets"), wxNullBitmap, DD_ITEM_STYLE_SPLIT_ITEM));
        for (const auto &[name, entry] : user_presets) {
            record_item(Append(name, entry.bmp ? *entry.bmp : wxNullBitmap, _L("Custom") + " ", nullptr), false, entry.preset_name);
            validate_selection(!selected_is_system && name == selected_name);
        }
    }

    if (!system_presets.empty()) {
        std::vector<const std::pair<const wxString, Entry> *> list;
        list.reserve(system_presets.size());
        for (const auto &item : system_presets)
            list.push_back(&item);
        std::sort(list.begin(), list.end(), [](auto *l, auto *r) {
            return filament_list_less({into_u8(l->first), l->second.vendor, l->second.type},
                                      {into_u8(r->first), r->second.vendor, r->second.type});
        });

        set_label_marker(Append(_L("System presets"), wxNullBitmap, DD_ITEM_STYLE_SPLIT_ITEM));
        for (auto *item : list) {
            record_item(Append(item->first, item->second.bmp ? *item->second.bmp : wxNullBitmap, from_u8(item->second.vendor), nullptr),
                        true, item->second.preset_name);
            validate_selection(selected_is_system && item->first == selected_name);
        }
    }

    update_selection();
    Thaw();

    // The initial preset is not in the list (e.g. incompatible and hidden): keep the stored name in
    // sync with the item update_selection() fell back to, so the dialog adds what it shows.
    if (selected_name.IsEmpty() && m_last_selected >= 0 && size_t(m_last_selected) < m_item_presets.size() &&
        !m_item_presets[m_last_selected].name.empty()) {
        m_preset_name = m_item_presets[m_last_selected].name;
        if (const Preset *preset = m_collection->find_preset(m_preset_name))
            tooltip = get_tooltip(*preset);
    }

    if (!tooltip.IsEmpty()) {
#ifdef __WXMSW__
        SetToolTip(NULL);
#endif
        SetToolTip(tooltip);
    }
}

void GUI::VirtualFilamentComboBox::OnSelect(wxCommandEvent &evt)
{
    const int selected_item = evt.GetSelection();
    auto      marker        = reinterpret_cast<Marker>(this->GetClientData(selected_item));
    if (marker >= LABEL_ITEM_DISABLED && marker < LABEL_ITEM_MAX) {
        this->SetSelection(m_last_selected);
        return;
    }
    if (selected_item < 0 || size_t(selected_item) >= m_item_presets.size() || m_item_presets[selected_item].name.empty()) {
        this->SetSelection(m_last_selected);
        return;
    }
    const ItemPreset &item        = m_item_presets[selected_item];
    std::string       preset_name = item.name;
    if (item.is_system) {
        // Diameter variants share one alias; resolve it the way the sidebar does, so the
        // variant matching the current nozzle is picked.
        const std::string &resolved = m_preset_bundle->get_preset_name_by_alias(Preset::TYPE_FILAMENT,
            Preset::remove_suffix_modified(into_u8(GetString(selected_item))));
        if (m_collection->find_preset(resolved))
            preset_name = resolved;
    }
    m_last_selected = selected_item;
    if (preset_name == m_preset_name)
        return;
    m_preset_name = preset_name;
    if (const Preset *preset = m_collection->find_preset(m_preset_name))
        SetToolTip(get_tooltip(*preset));
    if (m_on_changed)
        m_on_changed();
}

void GUI::VirtualFilamentComboBox::msw_rescale()
{
    PresetComboBox::msw_rescale();
    SetMinSize({-1, 30 * m_em_unit / 10});
    if (clr_picker)
        clr_picker->SetSize(20 * m_em_unit / 10, 20 * m_em_unit / 10);
    update_color_chip();
}

void GUI::VirtualFilamentComboBox::sys_color_changed()
{
    PlaterPresetComboBox::sys_color_changed();
    update_color_chip();
}

void PlaterPresetComboBox::sys_color_changed()
{
    PresetComboBox::sys_color_changed();
    if (clr_picker) {
        clr_picker->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    }
}
} // namespace Slic3r
