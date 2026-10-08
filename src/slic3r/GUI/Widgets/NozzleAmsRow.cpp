#include "NozzleAmsRow.hpp"

#include <algorithm>
#include <vector>

#include <boost/algorithm/string/join.hpp>

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/gdicmn.h>
#include <wx/graphics.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>

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
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r::GUI {

namespace {

MachineObject *selected_machine()
{
    auto *dev = wxGetApp().getDeviceManager();
    return dev ? dev->get_selected_machine() : nullptr;
}

// Live AMS content for one extruder, bucketed by slot count. Empty when disconnected.
struct AmsBuckets
{
    std::vector<AMSinfo> ams4; // 4-slot AMS units bound to the extruder
    std::vector<AMSinfo> ams1; // 1-slot (N3S) AMS units bound to the extruder
};

// Collect the live AMS content bound to one logical extruder from the selected machine.
AmsBuckets resolve_ams_infos(int extruder_idx)
{
    AmsBuckets     buckets;
    MachineObject *obj = selected_machine();
    if (!obj || !obj->GetFilaSystem()) return buckets;

    const bool remain         = obj->GetFilaSystem()->IsDetectRemainEnabled();
    const bool humidity       = obj->is_support_ams_humidity;
    const int  extruder_count = obj->GetExtderSystem() ? obj->GetExtderSystem()->GetTotalExtderCount() : 1;
    for (const auto &[id, ams] : obj->GetFilaSystem()->GetAmsList()) {
        // GetUniqueBindedExtruderId is a PHYSICAL id; extruder_idx is LOGICAL.
        auto bound = ams->GetUniqueBindedExtruderId();
        if (bound && DevExtder::to_logical_extruder_id(extruder_count, bound.value()) != extruder_idx) continue;

        AMSinfo info;
        info.parse_ams_info(obj, ams, remain, humidity);
        if (ams->GetAmsType() == DevAmsType::N3S)
            buckets.ams1.push_back(info);
        else if (ams->GetAmsType() != DevAmsType::ZMULTI_A && ams->GetAmsType() != DevAmsType::ZMULTI_B)
            buckets.ams4.push_back(info);
    }
    return buckets;
}

// Persist an AMS-count edit for one extruder: registry + app_config + slice-status refresh.
void set_ams_count(int extruder_index, int ams4, int ams1)
{
    PresetBundle &pb = *wxGetApp().preset_bundle;
    pb.set_ams_count(extruder_index, ams4, ams1);
    std::vector<std::string> parts = save_extruder_ams_count_to_string(pb.extruder_ams_counts);
    wxGetApp().app_config->set("presets", "extruder_ams_count", boost::algorithm::join(parts, ","));
    if (auto *plater = wxGetApp().plater()) {
        // AMS presence gates the purge-mode button; refresh it whenever counts change.
        plater->sidebar().update_purge_mode_btn();
        plater->update();
    }
}

// One "N slots AMS" menu row: a rounded blue highlight with white text on hover
class AmsMenuItem : public wxPanel
{
public:
    AmsMenuItem(wxWindow *parent, const wxString &text, bool enabled) : wxPanel(parent, wxID_ANY), m_text(text), m_enabled(enabled)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetMinSize(FromDIP(wxSize(94, 24)));
        Bind(wxEVT_PAINT, &AmsMenuItem::OnPaint, this);
        if (m_enabled) {
            Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &e) {
                m_hover = true;
                Refresh();
                e.Skip();
            });
            Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &e) {
                m_hover = false;
                Refresh();
                e.Skip();
            });
        }
    }

private:
    void OnPaint(wxPaintEvent &)
    {
        wxPaintDC          pdc(this);
        wxGCDC             dc(pdc);
        wxGraphicsContext *gc = dc.GetGraphicsContext();
        if (gc == nullptr) return;
        gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

        const wxSize   sz = GetClientSize();
        const wxColour bg = wxGetApp().dark_mode() ? StateColor::darkModeColorFor(*wxWHITE) : *wxWHITE;
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->SetBrush(wxBrush(bg));
        gc->DrawRectangle(0, 0, sz.GetWidth(), sz.GetHeight());

        if (m_hover) {
            gc->SetBrush(wxBrush(ThemeColor::Link));
            gc->DrawRoundedRectangle(0, 0, sz.GetWidth(), sz.GetHeight(), FromDIP(4));
        }

        const wxColour fg = m_hover ? *wxWHITE : (m_enabled ? ThemeColor::TextPrimary : ThemeColor::Grey400);
        gc->SetFont(Label::Body_13, wxGetApp().dark_mode() && !m_hover ? StateColor::darkModeColorFor(fg) : fg);
        double tw = 0, th = 0;
        gc->GetTextExtent(m_text, &tw, &th);
        gc->DrawText(m_text, FromDIP(8), (sz.GetHeight() - th) / 2.0);
    }

    wxString m_text;
    bool     m_enabled{true};
    bool     m_hover{false};
};

// "Add AMS" dropdown (4 slots / 1 slot, gated by the machine-wide caps), anchored under `anchor`.
void show_ams_add_menu(wxWindow *anchor, int extruder_index, PopupWindow *&current, std::function<void()> on_changed)
{
    if (!anchor) return;
    if (current) {
        current->Dismiss();
        current->Destroy();
        current = nullptr;
    }

    auto *popup = new RoundedPopupWindow(anchor, 8, false);
    auto *sizer = new wxBoxSizer(wxVERTICAL);

    PresetBundle *pb          = wxGetApp().preset_bundle;
    const int     other_idx   = 1 - extruder_index;
    const auto   &registry    = pb->extruder_ams_counts;
    const auto [self4, self1] = pb->get_ams_count(extruder_index);
    // Caps are machine-wide; the other extruder only contributes on a dual machine (registry has 2).
    const auto [oth4, oth1] = (other_idx >= 0 && other_idx < (int) registry.size()) ? pb->get_ams_count(other_idx) : std::pair<int, int>{0, 0};

    auto add_item = [&](const wxString &label, bool enabled, std::function<void()> on_click) {
        auto *item = new AmsMenuItem(popup, label, enabled);
        if (enabled)
            item->Bind(wxEVT_LEFT_DOWN, [popup, on_click](wxMouseEvent &e) {
                on_click();
                popup->Dismiss();
                e.Skip();
            });
        sizer->Add(item, 0, wxEXPAND | wxLEFT | wxRIGHT, popup->FromDIP(5));
    };

    sizer->AddSpacer(popup->FromDIP(5));
    add_item(_L("4 slots AMS"), self4 + oth4 < 4, [extruder_index, on_changed] {
        const auto [a4, a1] = wxGetApp().preset_bundle->get_ams_count(extruder_index);
        set_ams_count(extruder_index, a4 + 1, a1);
        if (on_changed) on_changed();
    });
    add_item(_L("1 slot AMS"), self1 + oth1 < 8, [extruder_index, on_changed] {
        const auto [a4, a1] = wxGetApp().preset_bundle->get_ams_count(extruder_index);
        set_ams_count(extruder_index, a4, a1 + 1);
        if (on_changed) on_changed();
    });
    sizer->AddSpacer(popup->FromDIP(5));

    popup->SetSizer(sizer);
    popup->Finalize(); // sizes to content and clips to the rounded silhouette

    current             = popup;
    const wxPoint pos   = anchor->GetScreenPosition();
    const wxSize  bsize = anchor->GetSize();
    popup->Position(wxPoint(pos.x, pos.y + bsize.y), wxSize(0, 0));
    popup->Popup();
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
    del->Bind(wxEVT_LEFT_UP, [extruder_index, preview, &previews, on_changed, popup](wxMouseEvent &) {
        auto it = std::find(previews.begin(), previews.end(), preview);
        if (it != previews.end()) {
            const size_t index  = std::distance(previews.begin(), it);
            const auto [a4, a1] = wxGetApp().preset_bundle->get_ams_count(extruder_index);
            if (index < 4)
                set_ams_count(extruder_index, std::max(0, a4 - 1), a1);
            else
                set_ams_count(extruder_index, a4, std::max(0, a1 - 1));
            if (on_changed) on_changed();
        }
        popup->Dismiss();
    });

    auto *sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(del, 0, wxALL, popup->FromDIP(2));
    popup->SetSizer(sizer);
    popup->Finalize();

    current              = popup;
    const wxPoint pos    = preview->GetScreenPosition();
    const wxSize  psize  = preview->GetSize();
    const int     offset = preview->FromDIP(4);
    const int     pw     = popup->GetSize().GetWidth();
    popup->Position(wxPoint(pos.x + psize.x - pw + offset, pos.y - offset), wxSize(0, 0));
    popup->Popup();
}

} // namespace

NozzleAmsRow::NozzleAmsRow(wxWindow *parent, int extruder_index, const wxString &label_text, Placement placement, std::function<void()> on_changed)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), m_extruder_index(extruder_index), m_placement(placement), m_on_changed(std::move(on_changed))
{
    SetBackgroundColour(ThemeColor::Grey200);

    m_has_ams  = wxGetApp().preset_bundle->printers.get_edited_preset().config.opt_int("ams_nozzle_idx", 0) >= 0;
    m_snapshot = wxGetApp().preset_bundle->get_ams_count(m_extruder_index);

    m_row_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto *row   = m_row_sizer;

    m_label = new Label(this, label_text);
    m_label->SetFont(Label::Body_14);
    m_label->SetBackgroundColour(ThemeColor::Grey200);
    row->Add(m_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(8));

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
            show_delete_popup(preview);
            e.Skip();
        });
        m_previews.push_back(preview);
        m_previews_sizer->Add(preview, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(2));
    };
    for (int i = 0; i < 4; ++i) make_preview(DevAmsType::AMS);
    for (int i = 0; i < 8; ++i) make_preview(DevAmsType::N3S);
    row->Add(m_previews_container, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    m_add_btn = new ScalableButton(this, wxID_ANY, "add_filament", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true, 12);
    m_add_btn->SetBackgroundColour(ThemeColor::Grey200);
    m_add_btn->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &e) {
        show_add_popup();
        e.Skip();
    });
    row->Add(m_add_btn, 0, wxALIGN_CENTER_VERTICAL);

    // Right-end check: this row's 4-slot and 1-slot counts match the connected printer.
    row->AddStretchSpacer();
    m_sync_badge = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("badge", this, 16), wxDefaultPosition, FromDIP(wxSize(16, 16)), 0);
    m_sync_badge->Hide();
    row->Add(m_sync_badge, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(8));

    // A standalone band pads itself; an inline row sits flush against the nozzle row it joins.
    const int pad   = m_placement == Placement::StandaloneRow ? FromDIP(6) : 0;
    auto     *outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(row, 0, wxEXPAND | wxALL, pad);
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

    const auto [n4, n1] = wxGetApp().preset_bundle->get_ams_count(m_extruder_index);
    AmsBuckets content  = resolve_ams_infos(m_extruder_index);

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

    int shown = 0;
    for (int i = 0; i < n4 && i < 4; ++i) {
        m_previews[i]->Update(i < (int) content.ams4.size() ? content.ams4[i] : placeholder4);
        m_previews[i]->Open();
        m_previews_sizer->Add(m_previews[i], 0, wxALL, FromDIP(2));
        ++shown;
    }
    for (int i = 0; i < n1 && i < 8; ++i) {
        AMSPreview *p = m_previews[4 + i];
        p->Update(i < (int) content.ams1.size() ? content.ams1[i] : placeholder1);
        p->Open();
        m_previews_sizer->Add(p, 0, wxALL, FromDIP(2));
        ++shown;
    }

    if (m_placement == Placement::Inline) m_label->Show(shown > 0);

    update_sync_badge();
    Layout();
}

// Show the check only when this extruder's local 4-slot and 1-slot counts equal the connected
// printer's. Offline, not ready, or a different printer model hides it.
void NozzleAmsRow::update_sync_badge()
{
    if (!m_sync_badge || !m_row_sizer) return;

    bool matched = false;
    MachineObject *obj = selected_machine();
    PresetBundle  *preset_bundle = wxGetApp().preset_bundle;
    if (obj && obj->is_online() && obj->is_info_ready() && obj->GetFilaSystem() && obj->GetExtderSystem() && preset_bundle &&
        preset_bundle->printers.get_edited_preset().get_printer_type(preset_bundle) == obj->get_show_printer_type()) {
        const auto counts   = collect_ams_counts(obj);
        const auto [n4, n1] = preset_bundle->get_ams_count(m_extruder_index);
        const auto found4   = counts.find(std::make_pair(m_extruder_index, 4));
        const auto found1   = counts.find(std::make_pair(m_extruder_index, 1));
        const int  machine_n4 = found4 == counts.end() ? 0 : found4->second;
        const int  machine_n1 = found1 == counts.end() ? 0 : found1->second;
        matched = machine_n4 == n4 && machine_n1 == n1;
    }

    m_row_sizer->Show(m_sync_badge, matched);
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

void NozzleAmsRow::restore() { set_ams_count(m_extruder_index, m_snapshot.first, m_snapshot.second); }

void NozzleAmsRow::Rescale()
{
    if (m_previews_container) m_previews_container->SetMinSize(wxSize(-1, FromDIP(40)));
    Layout();
}

} // namespace Slic3r::GUI
