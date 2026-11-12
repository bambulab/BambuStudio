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
#include "slic3r/GUI/Widgets/IconView.hpp"
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
constexpr size_t MAX_AMS_4_SLOT = 4; // 4-slot units per extruder, as the add menu caps them
constexpr size_t MAX_AMS_1_SLOT = 8; // 1-slot units per extruder, as the add menu caps them

// "Add AMS" dropdown (4 slots / 1 slot, gated by the machine-wide caps), anchored under `anchor`.
// Built in the same single-level cascade style as the nozzle diameter menu, so the two read alike.
// `current` is the caller's slot for the open menu: an open menu in it closes first, and the menu
// clears it when it closes, so it must outlive the menu.
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
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_extruder_index(extruder_index)
    , m_style(style)
    , m_on_changed(std::move(on_changed))
{
    // An inline row is a region of the card that hosts it, so it takes the card's surface.
    const bool     inline_row = style == Style::Inline;
    const wxColour surface    = inline_row ? parent->GetBackgroundColour() : wxColour(ThemeColor::Grey200);
    SetBackgroundColour(surface);

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
    m_label->SetFont(inline_row ? Label::Body_12 : Label::Body_14);
    m_label->SetForegroundColour(ThemeColor::TextPrimary);
    m_label->SetBackgroundColour(surface);
    m_label->Show(!label_text.empty());
    const bool band = style == Style::Band;
    row->Add(m_label, 0, wxALIGN_CENTER_VERTICAL | (band ? wxLEFT : 0) | wxRIGHT, FromDIP(inline_row ? 4 : 8));

    m_previews_container = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_previews_container->SetBackgroundColour(surface);
    if (!inline_row) m_previews_container->SetMinSize(wxSize(-1, FromDIP(40)));
    m_previews_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_previews_container->SetSizer(m_previews_sizer);

    // Fixed reusable pool: 4 four-slot AMS + 8 one-slot N3S. refresh_previews() shows a prefix.
    auto make_preview = [&](DevAmsType type) {
        AMSinfo info;
        info.ams_type = type;
        auto *preview = new AMSPreview(m_previews_container, wxID_ANY, info);
        // The preview draws over whatever was erased beneath it, and a window never inherits its
        // parent's colour, so without this nothing is erased and it renders on black.
        preview->SetBackgroundColour(surface);
        preview->Close();
        preview->Bind(wxEVT_ENTER_WINDOW, [this, preview](wxMouseEvent &e) {
            if (m_editable && m_feedable && hover_may_open_popup()) show_delete_popup(preview);
            e.Skip();
        });
        m_previews.push_back(preview);
        m_previews_sizer->Add(preview, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(2));
    };
    for (size_t i = 0; i < MAX_AMS_4_SLOT; ++i) make_preview(DevAmsType::AMS);
    for (size_t i = 0; i < MAX_AMS_1_SLOT; ++i) make_preview(DevAmsType::N3S);
    row->Add(m_previews_container, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(inline_row ? 2 : 6));

    m_add_btn = new IconView(this, "add_filament", inline_row ? 16 : 12);
    m_add_btn->SetBackgroundColour(surface);
    m_add_btn->SetToolTip(_L("Add AMS"));
    m_add_btn->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &e) {
        if (hover_may_open_popup()) show_add_popup();
        e.Skip();
    });
    row->Add(m_add_btn, 0, wxALIGN_CENTER_VERTICAL);

    // A band pads its content; a plain or inline row sits flush with the owner's other rows.
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

    // An inline row outlives printer switches, so refresh() decides its visibility each time. A dialog
    // themes its own rows when it shows; the sidebar does not, so an inline row themes itself.
    if (inline_row) {
        wxGetApp().UpdateDarkUIWin(this);
        refresh();
        return;
    }
    if (!m_has_ams) {
        Hide();
        return;
    }
    refresh_previews(); // initial populate is silent; on_changed fires only on user edits
}

NozzleAmsRow::~NozzleAmsRow()
{
    // The add menu and the delete popup clear this slot when they close, so close them while it exists.
    if (m_popup) m_popup->Dismiss();
}

bool NozzleAmsRow::set_editable(bool editable)
{
    if (m_editable == editable) return false;
    m_editable = editable;
    m_drawn    = false;
    return refresh();
}

bool NozzleAmsRow::set_feedable(bool feedable)
{
    if (m_feedable == feedable) return false;
    m_feedable = feedable;
    m_drawn    = false;
    return refresh();
}

bool NozzleAmsRow::refresh()
{
    PresetBundle &bundle = *wxGetApp().preset_bundle;
    // The printer preset owns the extruder count. A redraw can come after a printer switch but before
    // the owner hides this row, so never draw an extruder that the current preset lacks.
    const int extruder_count = bundle.get_printer_extruder_count();
    m_has_ams                = bundle.printers.get_edited_preset().config.opt_int("ams_nozzle_idx", 0) >= 0;
    const bool exists        = m_has_ams && m_extruder_index < extruder_count;

    bool changed = exists && refresh_previews();
    bool visible = exists;
    if (m_style == Style::Inline) {
        visible = visible && (m_editable || !m_shown.empty());
        // One extruder needs no side name.
        changed |= m_label->Show(extruder_count > 1 && !m_label->GetLabel().empty());
    }
    changed |= m_add_btn->Show(m_editable);
    changed |= Show(visible);
    if (changed) Layout();
    return changed;
}

// Show the first n4 4-slot + n1 1-slot previews of the AMS selection. A unit shows the machine's trays
// only while the machine's nozzles of this extruder are synced, else an empty placeholder, so the row
// always matches the count the user set.
bool NozzleAmsRow::refresh_previews()
{
    if (!m_previews_sizer) return false;

    AmsBuckets            content   = resolve_ams_infos(m_extruder_index);
    ExtruderAmsSelection *selection = ensure_ams_selection(m_extruder_index, content);
    if (!selection) return false;
    const bool use_device_content = m_feedable && is_extruder_stat_synced(m_extruder_index);

    std::map<std::string, const AMSinfo *> device4;
    std::map<std::string, const AMSinfo *> device1;
    for (const AMSinfo &info : content.ams4) device4[info.ams_id] = &info;
    for (const AMSinfo &info : content.ams1) device1[info.ams_id] = &info;

    std::vector<AMSinfo> wanted;
    auto add_kind = [&](const std::vector<std::string> &selected, size_t cap, const std::map<std::string, const AMSinfo *> &device,
                        DevAmsType type) {
        AMSinfo placeholder;
        placeholder.ams_type = type;
        placeholder.cans.resize(type == DevAmsType::AMS ? 4 : 1);
        // A unit that cannot feed shows greyed trays.
        if (!m_feedable)
            for (Caninfo &can : placeholder.cans) can.material_colour = AMS_CONTROL_DISABLE_COLOUR;
        for (size_t i = 0; i < selected.size() && i < cap; ++i) {
            const auto found = device.find(selected[i]);
            wanted.push_back(use_device_content && found != device.end() ? *found->second : placeholder);
        }
    };
    add_kind(selection->ams4, MAX_AMS_4_SLOT, device4, DevAmsType::AMS);
    const size_t wanted_4 = wanted.size();
    add_kind(selection->ams1, MAX_AMS_1_SLOT, device1, DevAmsType::N3S);

    update_sync_badge();
    // The machine reports many times a second; redraw only what it changed.
    if (m_drawn && wanted == m_shown && wanted_4 == m_shown_4) return false;

    m_previews_sizer->Clear(false);
    for (auto *p : m_previews) p->Close();
    for (size_t i = 0; i < wanted.size(); ++i) {
        AMSPreview *p = m_previews[i < wanted_4 ? i : MAX_AMS_4_SLOT + (i - wanted_4)];
        p->Update(wanted[i]);
        p->Enable(m_feedable);
        p->Refresh();
        p->Open();
        m_previews_sizer->Add(p, 0, wxALL, FromDIP(2));
    }
    m_shown   = std::move(wanted);
    m_shown_4 = wanted_4;
    m_drawn   = true;

    Layout();
    return true;
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
        refresh();
        if (m_on_changed) m_on_changed();
    });
}

void NozzleAmsRow::show_delete_popup(AMSPreview *preview)
{
    show_ams_delete_popup(m_extruder_index, preview, m_previews, m_popup, [this] {
        refresh();
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
    refresh();
}

void NozzleAmsRow::Rescale()
{
    if (m_previews_container && m_style != Style::Inline) m_previews_container->SetMinSize(wxSize(-1, FromDIP(40)));
    Layout();
}

} // namespace Slic3r::GUI
