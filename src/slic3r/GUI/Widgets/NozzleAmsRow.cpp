#include "NozzleAmsRow.hpp"

#include <algorithm>
#include <vector>

#include <boost/algorithm/string/join.hpp>

#include <wx/gdicmn.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/wx.h>

#include "slic3r/GUI/ExtruderHelpers.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"
#include "slic3r/GUI/Widgets/PopupWindow.hpp"
#include "slic3r/GUI/Widgets/RoundedPopupWindow.hpp"
#include "slic3r/GUI/Widgets/AMSItem.hpp"
#include "slic3r/GUI/Widgets/CascadeMenu.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r::GUI {

namespace {

MachineObject *selected_machine()
{
    auto *dev = wxGetApp().getDeviceManager();
    return dev ? dev->get_selected_machine() : nullptr;
}

// A hover only opens a popup while Studio is the foreground app. The popups take activation, so one
// opened from a background hover wins it back from the modal dialog when the user returns.
bool hover_may_open_popup() { return wxTheApp->IsActive(); }

// Live AMS content for one extruder, bucketed by slot count. Empty when disconnected.
struct AmsBuckets
{
    std::vector<AMSinfo> ams4; // 4-slot AMS units bound to the extruder
    std::vector<AMSinfo> ams1; // 1-slot (N3S) AMS units bound to the extruder
    std::string          device_id;
};

// Collect the live AMS content bound to one logical extruder from the selected machine.
AmsBuckets resolve_ams_infos(int extruder_idx)
{
    AmsBuckets     buckets;
    MachineObject *obj = selected_machine();
    PresetBundle  *preset_bundle = wxGetApp().preset_bundle;
    if (!obj || !obj->is_online() || !obj->is_info_ready() || !obj->GetFilaSystem() || !obj->GetExtderSystem() || !preset_bundle ||
        preset_bundle->printers.get_edited_preset().get_printer_type(preset_bundle) != obj->get_show_printer_type())
        return buckets;
    buckets.device_id = obj->get_dev_id();

    const bool  remain   = obj->GetFilaSystem()->IsDetectRemainEnabled();
    const bool  humidity = obj->is_support_ams_humidity;
    const auto &units    = obj->GetFilaSystem()->GetAmsList();
    const auto  ids      = collect_ams_ids(obj);
    auto append = [&](int slot_count, std::vector<AMSinfo> &target) {
        const auto found = ids.find({extruder_idx, slot_count});
        if (found == ids.end()) return;
        for (const std::string &id : found->second) {
            const auto unit = units.find(id);
            if (unit == units.end()) continue;
            AMSinfo info;
            info.parse_ams_info(obj, unit->second, remain, humidity);
            target.push_back(std::move(info));
        }
    };
    append(4, buckets.ams4);
    append(1, buckets.ams1);
    return buckets;
}

// Persist an AMS-count edit for one extruder: registry + app_config + slice-status refresh.
void set_ams_count(int extruder_index, int ams4, int ams1)
{
    PresetBundle &pb = *wxGetApp().preset_bundle;
    pb.set_ams_count(extruder_index, ams4, ams1);
    std::vector<std::string> parts = save_extruder_ams_count_to_string(pb.extruder_ams_counts);
    wxGetApp().app_config->set("presets", "extruder_ams_count", boost::algorithm::join(parts, ","));
    if (auto *plater = wxGetApp().plater()) plater->update();
}

std::vector<std::string> ams_ids(const std::vector<AMSinfo> &infos)
{
    std::vector<std::string> ids;
    ids.reserve(infos.size());
    for (const AMSinfo &info : infos) ids.push_back(info.ams_id);
    return ids;
}

ExtruderAmsSelection *ensure_ams_selection(int extruder_index, const AmsBuckets &inventory)
{
    PresetBundle &pb = *wxGetApp().preset_bundle;
    pb.initialize_ams_selection(extruder_index, inventory.device_id, ams_ids(inventory.ams4), ams_ids(inventory.ams1));
    return pb.get_ams_selection(extruder_index);
}

void persist_ams_selection(int extruder_index, const ExtruderAmsSelection &selection)
{
    set_ams_count(extruder_index, static_cast<int>(selection.ams4.size()), static_cast<int>(selection.ams1.size()));
}

void add_ams_unit(int extruder_index, int slot_count)
{
    const AmsBuckets inventory = resolve_ams_infos(extruder_index);
    auto            *selection = ensure_ams_selection(extruder_index, inventory);
    if (!selection) return;

    const int n4 = static_cast<int>(selection->ams4.size()) + (slot_count == 4 ? 1 : 0);
    const int n1 = static_cast<int>(selection->ams1.size()) + (slot_count == 1 ? 1 : 0);
    wxGetApp().preset_bundle->resize_ams_selection(extruder_index, inventory.device_id,
                                                   ams_ids(inventory.ams4), ams_ids(inventory.ams1), n4, n1);
    if (const auto *updated = wxGetApp().preset_bundle->get_ams_selection(extruder_index))
        persist_ams_selection(extruder_index, *updated);
}

void remove_ams_unit(int extruder_index, int slot_count, size_t position)
{
    const AmsBuckets inventory = resolve_ams_infos(extruder_index);
    auto            *selection = ensure_ams_selection(extruder_index, inventory);
    if (!selection) return;

    std::vector<std::string> &selected = slot_count == 4 ? selection->ams4 : selection->ams1;
    if (position >= selected.size()) return;
    selected.erase(selected.begin() + position);
    persist_ams_selection(extruder_index, *selection);
}

// Single-extruder multi-nozzle printers accept at most this many external AMS units, any mix of slot kinds.
constexpr int kSingleExtMultiNozzleMaxAms = 4;

// "Add AMS" dropdown (4 slots / 1 slot, gated by the machine-wide caps), anchored under `anchor`.
// Built in the same single-level cascade style as the nozzle diameter menu, so the two read alike.
void show_ams_add_menu(wxWindow *anchor, int extruder_index, PopupWindow *&current, std::function<void()> on_changed)
{
    if (!anchor) return;
    if (current) {
        current->Dismiss();
        current->Destroy();
        current = nullptr;
    }

    PresetBundle *pb          = wxGetApp().preset_bundle;
    const int     other_idx   = 1 - extruder_index;
    const auto   &registry    = pb->extruder_ams_counts;
    const auto [self4, self1] = pb->get_ams_count(extruder_index);
    // Caps are machine-wide; the other extruder only contributes on a dual machine (registry has 2).
    const auto [oth4, oth1] = (other_idx >= 0 && other_idx < (int) registry.size()) ? pb->get_ams_count(other_idx) : std::pair<int, int>{0, 0};
    const bool has_room     = !is_single_ext_multi_nozzle(pb->printers.get_edited_preset().config) ||
                          self4 + self1 + oth4 + oth1 < kSingleExtMultiNozzleMaxAms;

    auto *menu  = new CascadeMenuPopup(anchor);
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    current     = menu;

    // A fresh menu is built for every gesture, so let the outgoing one go with the close rather than
    // linger as a hidden child of the anchor. The tracked pointer is dropped with it, or the next
    // open would dismiss a destroyed window.
    menu->SetDestroyOnClose(true);
    menu->SetOnClosed([&current, menu] {
        if (current == menu) current = nullptr;
    });
    menu->Bind(wxEVT_IDLE, [anchor, menu](wxIdleEvent &e) {
        e.Skip();
        wxRect hover_area = anchor->GetScreenRect();
        hover_area.Union(menu->GetScreenRect());
        hover_area.Inflate(menu->FromDIP(2));
        if (!hover_area.Contains(wxGetMousePosition())) menu->CloseCascade();
    });

    bool first_row = true;
    auto add_item  = [&](const wxString &label, bool enabled, int slot_count) {
        auto *item = new CascadeMenuItem(menu, label, enabled, false, false);
        if (enabled)
            item->Bind(wxEVT_BUTTON, [menu, extruder_index, slot_count, on_changed](wxCommandEvent &) {
                add_ams_unit(extruder_index, slot_count);
                if (on_changed) on_changed();
                menu->CloseCascade();
            });
        CascadeMenu::AddItem(sizer, item, first_row);
        first_row = false;
    };
    add_item(_L("4 slots AMS"), has_room && self4 + oth4 < 4, 4);
    add_item(_L("1 slot AMS"), has_room && self1 + oth1 < 8, 1);

    CascadeMenu::Finish(menu, sizer);

    // Position() is given the anchor's size so it can flip the menu above it near the bottom of the
    // screen; the x offset lines the rows up with the anchor, past the panel's inset.
    menu->Position(anchor->GetScreenPosition() - wxPoint(menu->FromDIP(CascadeMenu::Pad), 0), anchor->GetSize());
    menu->Popup();
}

// Per-preview trash popup that decrements the AMS count for `preview` (4-slot if index < 4).
// Tracked through `current` (shared with the add menu) and dismissed first, so hovering across
// previews replaces the popup instead of stacking one per preview.
void show_ams_delete_popup(int extruder_index, AMSPreview *preview, const std::vector<AMSPreview *> &previews, PopupWindow *&current, std::function<void()> on_changed)
{
    if (!preview || !preview->IsShown()) return;
    if (current) {
        current->Dismiss();
        current->Destroy();
        current = nullptr;
    }

    auto *popup = new RoundedPopupWindow(preview, 2, false);
    popup->SetBackgroundColour(ThemeColor::White);

    const ScalableBitmap trash_icon(popup, wxGetApp().dark_mode() ? "trash_dark" : "trash", 12);
    auto                *del = new wxStaticBitmap(popup, wxID_ANY, trash_icon.bmp());
    del->SetBackgroundColour(wxGetApp().dark_mode() ? StateColor::darkModeColorFor(ThemeColor::White) : ThemeColor::White);
    del->SetToolTip(_L("Remove this AMS"));

    auto remove_this_ams = [extruder_index, preview, &previews, on_changed, popup](wxMouseEvent &) {
        auto it = std::find(previews.begin(), previews.end(), preview);
        if (it != previews.end()) {
            const size_t index  = std::distance(previews.begin(), it);
            if (index < 4)
                remove_ams_unit(extruder_index, 4, index);
            else
                remove_ams_unit(extruder_index, 1, index - 4);
            if (on_changed) on_changed();
        }
        popup->Dismiss();
    };

    // The release is the usual click, except on macOS: wxStaticBitmap is an NSImageView, and
    // wxWidgetCocoaImpl::mouseEvent hands any mouse event wx leaves unhandled to the native
    // NSControl, whose own mouseDown takes the button sequence over so the release never arrives.
    // Acting on the press there keeps wx from calling super at all.
#ifdef __WXOSX__
    del->Bind(wxEVT_LEFT_DOWN, remove_this_ams);
#else
    del->Bind(wxEVT_LEFT_UP, remove_this_ams);
#endif

    auto *sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(del, 0, wxALL, popup->FromDIP(2));
    popup->SetSizer(sizer);
    popup->Finalize();

    current              = popup;
    const wxPoint pos    = preview->GetScreenPosition();
    const wxSize  psize  = preview->GetSize();
    const int     offset = preview->FromDIP(4);
    const int     pw     = popup->GetSize().GetWidth();

    // wxPopupTransientWindow::Popup() hands the mouse capture, and the handler that drives dismissal,
    // to the popup's single child when it has exactly one -- here the trash bitmap. Cocoa then routes
    // every mouse event in the application to that bitmap, bypassing PopupWindow's own hit-test
    // forwarding, and wx reads the bitmap's rectangle as the popup's, so a press just off the icon
    // counts as a click outside and dismisses the popup before the click lands. A hidden filler keeps
    // the child count above one; it sits outside the sizer, so it changes no layout, and PopupWindow's
    // hit test skips it. See CascadeMenuPopup::Popup, which carries the same workaround.
    // Windows runs a different wxPopupTransientWindow that never moves the capture off the popup.
#ifndef __WXMSW__
    (new wxWindow(popup, wxID_ANY, wxDefaultPosition, wxSize(0, 0)))->Hide();
#endif
    popup->Position(wxPoint(pos.x + psize.x - pw + offset, pos.y - offset), wxSize(0, 0));
    popup->Popup();
}

} // namespace

NozzleAmsRow::NozzleAmsRow(wxWindow *parent, int extruder_index, const wxString &label_text, Style style, std::function<void()> on_changed)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), m_extruder_index(extruder_index), m_on_changed(std::move(on_changed))
{
    SetBackgroundColour(ThemeColor::Grey200);

    m_has_ams  = wxGetApp().preset_bundle->printers.get_edited_preset().config.opt_int("ams_nozzle_idx", 0) >= 0;
    m_snapshot = wxGetApp().preset_bundle->get_ams_count(m_extruder_index);
    if (const auto *selection = wxGetApp().preset_bundle->get_ams_selection(m_extruder_index)) {
        m_selection_snapshot_initialized = selection->initialized;
        m_selection_snapshot_device_id   = selection->device_id;
        m_ams4_snapshot                  = selection->ams4;
        m_ams1_snapshot                  = selection->ams1;
    }

    auto *row = new wxBoxSizer(wxHORIZONTAL);

    m_label = new Label(this, label_text);
    m_label->SetFont(Label::Body_14);
    m_label->SetBackgroundColour(ThemeColor::Grey200);
    const bool band = style == Style::Band;
    row->Add(m_label, 0, wxALIGN_CENTER_VERTICAL | (band ? wxLEFT : 0) | wxRIGHT, FromDIP(8));

    m_previews_container = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_previews_container->SetBackgroundColour(ThemeColor::Grey200);
    m_previews_container->SetMinSize(wxSize(-1, FromDIP(40)));
    m_previews_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_previews_container->SetSizer(m_previews_sizer);

    // Fixed reusable pool: 4 four-slot AMS + 8 one-slot N3S. refresh_previews() shows a prefix.
    auto make_preview = [&](DevAmsType type) {
        AMSinfo info;
        info.ams_type = type;
        auto *preview = new AMSPreview(m_previews_container, wxID_ANY, info);
        preview->Close();
        preview->Bind(wxEVT_ENTER_WINDOW, [this, preview](wxMouseEvent &e) {
            if (hover_may_open_popup()) show_delete_popup(preview);
            e.Skip();
        });
        m_previews.push_back(preview);
        m_previews_sizer->Add(preview, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(2));
    };
    for (int i = 0; i < 4; ++i) make_preview(DevAmsType::AMS);
    for (int i = 0; i < 8; ++i) make_preview(DevAmsType::N3S);
    row->Add(m_previews_container, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

    m_add_btn = new ScalableButton(this, wxID_ANY, "add_filament", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true, 12);
    m_add_btn->SetBackgroundColour(ThemeColor::Grey200);
    m_add_btn->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &e) {
        if (hover_may_open_popup()) show_add_popup();
        e.Skip();
    });
    row->Add(m_add_btn, 0, wxALIGN_CENTER_VERTICAL);

    // A band pads its content; a plain row sits flush with the owner's other rows.
    auto *outer = new wxBoxSizer(wxHORIZONTAL);
    outer->Add(row, 0, wxALL, band ? FromDIP(6) : 0);
    outer->AddStretchSpacer();

    // Corner check, outside the content padding: this row's 4-slot and 1-slot counts match the connected printer.
    if (band) {
        m_sync_badge = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("badge", this, 16), wxDefaultPosition, FromDIP(wxSize(16, 16)), 0);
        m_sync_badge->Hide();
        outer->Add(m_sync_badge, 0, wxALIGN_TOP);
    }
    SetSizer(outer);

    if (!m_has_ams) {
        Hide();
        return;
    }
    refresh_previews(); // initial populate is silent; on_changed fires only on user edits
}

// Show the first n4 4-slot + n1 1-slot previews driven by the count registry; content resolved
// live from the machine (placeholder when absent). Inline placement hides the label when empty.
void NozzleAmsRow::refresh_previews()
{
    if (!m_previews_sizer) return;

    AmsBuckets            content   = resolve_ams_infos(m_extruder_index);
    ExtruderAmsSelection *selection = ensure_ams_selection(m_extruder_index, content);
    if (!selection) return;
    const bool use_device_content = is_extruder_stat_synced();

    static AMSinfo placeholder4 = [] {
        AMSinfo a;
        a.ams_type = DevAmsType::AMS;
        a.cans.resize(4);
        return a;
    }();
    static AMSinfo placeholder1 = [] {
        AMSinfo a;
        a.ams_type = DevAmsType::N3S;
        a.cans.resize(1);
        return a;
    }();

    m_previews_sizer->Clear(false);
    for (auto *p : m_previews) p->Close();

    std::map<std::string, const AMSinfo *> device4;
    std::map<std::string, const AMSinfo *> device1;
    for (const AMSinfo &info : content.ams4) device4[info.ams_id] = &info;
    for (const AMSinfo &info : content.ams1) device1[info.ams_id] = &info;

    for (size_t i = 0; i < selection->ams4.size() && i < 4; ++i) {
        const auto found = device4.find(selection->ams4[i]);
        m_previews[i]->Update(use_device_content && found != device4.end() ? *found->second : placeholder4);
        m_previews[i]->Refresh();
        m_previews[i]->Open();
        m_previews_sizer->Add(m_previews[i], 0, wxALL, FromDIP(2));
    }
    for (size_t i = 0; i < selection->ams1.size() && i < 8; ++i) {
        AMSPreview *p = m_previews[4 + i];
        const auto found = device1.find(selection->ams1[i]);
        p->Update(use_device_content && found != device1.end() ? *found->second : placeholder1);
        p->Refresh();
        p->Open();
        m_previews_sizer->Add(p, 0, wxALL, FromDIP(2));
    }

    update_sync_badge();
    Layout();
}

// Show the check only when the nozzle setup is synced and this row contains exactly the device AMS
// units. Offline, not ready, a different printer, or any virtual/missing unit hides it.
void NozzleAmsRow::update_sync_badge()
{
    if (!m_sync_badge) return;

    bool matched = false;
    MachineObject *obj = selected_machine();
    PresetBundle  *preset_bundle = wxGetApp().preset_bundle;
    if (obj && obj->is_online() && obj->is_info_ready() && obj->GetFilaSystem() && obj->GetExtderSystem() && preset_bundle &&
        preset_bundle->printers.get_edited_preset().get_printer_type(preset_bundle) == obj->get_show_printer_type()) {
        const auto ids       = collect_ams_ids(obj);
        const auto found4    = ids.find({m_extruder_index, 4});
        const auto found1    = ids.find({m_extruder_index, 1});
        const auto *selection = preset_bundle->get_ams_selection(m_extruder_index);
        const std::vector<std::string> empty;
        const auto &machine4 = found4 == ids.end() ? empty : found4->second;
        const auto &machine1 = found1 == ids.end() ? empty : found1->second;
        matched = is_extruder_stat_synced() && selection && selection->initialized &&
                  selection->ams4 == machine4 && selection->ams1 == machine1;
    }

    m_sync_badge->Show(matched);
}

void NozzleAmsRow::show_add_popup()
{
    show_ams_add_menu(m_add_btn, m_extruder_index, m_popup, [this] {
        refresh_previews();
        if (m_on_changed) m_on_changed();
    });
}

void NozzleAmsRow::show_delete_popup(AMSPreview *preview)
{
    show_ams_delete_popup(m_extruder_index, preview, m_previews, m_popup, [this] {
        refresh_previews();
        if (m_on_changed) m_on_changed();
    });
}

void NozzleAmsRow::restore()
{
    if (auto *selection = wxGetApp().preset_bundle->get_ams_selection(m_extruder_index)) {
        selection->initialized = m_selection_snapshot_initialized;
        selection->device_id   = m_selection_snapshot_device_id;
        selection->ams4        = m_ams4_snapshot;
        selection->ams1        = m_ams1_snapshot;
    }
    set_ams_count(m_extruder_index, m_snapshot.first, m_snapshot.second);
}

void NozzleAmsRow::Rescale()
{
    if (m_previews_container) m_previews_container->SetMinSize(wxSize(-1, FromDIP(40)));
    Layout();
}

} // namespace Slic3r::GUI
