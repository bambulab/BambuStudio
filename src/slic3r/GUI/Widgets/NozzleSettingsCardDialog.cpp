#include "NozzleSettingsCardDialog.hpp"

#include <algorithm>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/format.hpp>

#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/graphics.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/ExtruderHelpers.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/PillView.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"
#include "slic3r/GUI/Widgets/PopupWindow.hpp"
#include "slic3r/GUI/Widgets/RoundedPopupWindow.hpp"
#include "slic3r/GUI/Widgets/CascadeMenu.hpp"
#include "slic3r/GUI/Widgets/AMSItem.hpp"
#include "slic3r/GUI/DeviceCore/DevManager.h"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h"
#include "slic3r/GUI/DeviceCore/DevExtruderSystem.h"
#include "slic3r/GUI/Widgets/NozzleAmsRow.hpp"
#include "slic3r/GUI/DeviceManager.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r::GUI {
namespace {

constexpr int CARD_W   = 100;
constexpr int CARD_H   = 88; // header pill + the diameter and type rows; the combos that set it to 100 are gone
constexpr int CARD_GAP = 16;
constexpr int ROW_PAD  = 8; // card edge to a caption or a value
constexpr int ROW_GAP  = 4; // least space between a caption and its value

/**
 * \brief Set on \p value the largest body font, 12 down to 8, at which \p text fits beside \p caption
 *
 * Falls back to the smallest size when even that overflows. The caption keeps its 12px font.
 *
 * \param value    the value label of a card row
 * \param caption  the row's caption text, such as "Diameter"
 * \param text     the new value text
 */
void set_fitting_value(Label *value, const wxString &caption, const wxString &text)
{
    int caption_w = 0, text_w = 0, text_h = 0;
    value->GetTextExtent(caption, &caption_w, &text_h, nullptr, nullptr, &Label::Body_12);
    const int max_w = value->FromDIP(CARD_W - 2 * ROW_PAD - ROW_GAP) - caption_w;

    const wxFont *fonts[] = {&Label::Body_12, &Label::Body_11, &Label::Body_10, &Label::Body_9, &Label::Body_8};
    const wxFont *fit     = fonts[std::size(fonts) - 1];
    for (const wxFont *font : fonts) {
        value->GetTextExtent(text, &text_w, &text_h, nullptr, nullptr, font);
        if (text_w <= max_w) {
            fit = font;
            break;
        }
    }
    value->SetFont(*fit);
    value->SetLabel(text);
}

// Card fill/border/corner as ThemeColor tokens; mapped through StateColor's dark map at paint time.
inline wxColour themed(const wxColour &c) { return wxGetApp().dark_mode() ? StateColor::darkModeColorFor(c) : c; }

// encode_volume / decode_volume live in ExtruderHelpers.hpp; the external-AMS row (previews,
// popups, count edits) is encapsulated in the shared NozzleAmsRow widget.

// One flow type offered for a nozzle. Every entry is a nozzle that exists and can be fitted, so
// this dialog -- which configures what the rack holds, independently of any connected machine --
// has no unselectable rows at all.
struct FlowOption
{
    NozzleVolumeType volume{nvtStandard};
    wxString         label;
};

/**
 * \brief The flow types offered for a nozzle of \p diameter on \p extruder_index.
 *
 * Two sources decide the outcome. The printer preset's extruder_variant_list says which types that
 * printer is sold with at all; anything outside it (and hybrid, which is a slicing construct rather
 * than a nozzle anyone can install) is left out. That list is of parameter variants rather than of
 * nozzles, so nozzle_variant_exists() then drops the combinations it names that no one makes.
 */
std::vector<FlowOption> flow_options_for_diameter(int extruder_index, const wxString &diameter)
{
    std::vector<FlowOption> options;

    auto *preset_bundle      = wxGetApp().preset_bundle;
    auto  nozzle_volumes_def = preset_bundle->project_config.def()->get("nozzle_volume_type");

    auto printer_preset            = preset_bundle->get_similar_printer_preset({}, diameter.ToStdString());
    auto extruder_variants         = printer_preset->config.option<ConfigOptionStrings>("extruder_variant_list");
    auto extruders_def             = printer_preset->config.def()->get("extruder_type");
    auto extruders                 = printer_preset->config.option<ConfigOptionEnumsGeneric>("extruder_type");
    auto extruder_max_nozzle_count = printer_preset->config.option<ConfigOptionIntsNullable>("extruder_max_nozzle_count");

    const int         idx          = std::max(0, std::min(extruder_index, static_cast<int>(extruders->values.size()) - 1));
    const auto        type         = extruders_def->enum_labels[extruders->values[idx]];
    const std::string diameter_str = diameter.ToStdString();

    for (int j = 0; j < (int) nozzle_volumes_def->enum_labels.size(); ++j) {
        const auto volume     = nozzle_volumes_def->enum_keys_map->at(nozzle_volumes_def->enum_values[j]);
        const bool variant_ok = boost::algorithm::contains(extruder_variants->values[idx], type + " " + nozzle_volumes_def->enum_labels[j]);
        const bool hybrid_ok  = extruder_max_nozzle_count->values[idx] > 1 && volume == nvtHybrid;
        if (!variant_ok && !hybrid_ok) continue;
        if (volume == NozzleVolumeType::nvtHybrid) continue;
        if (!nozzle_variant_exists(diameter_str, static_cast<NozzleVolumeType>(volume))) continue;

        options.push_back({static_cast<NozzleVolumeType>(volume), _L(nozzle_volumes_def->enum_labels[j])});
    }
    return options;
}

// The user-facing name of a flow type, as the nozzle_volume_type enum declares it.
wxString flow_label(NozzleVolumeType volume)
{
    auto      def = wxGetApp().preset_bundle->project_config.def()->get("nozzle_volume_type");
    const int idx = static_cast<int>(volume);
    if (def && idx >= 0 && idx < (int) def->enum_labels.size()) return _L(def->enum_labels[idx]);
    return wxString::FromUTF8(DevNozzle::ToNozzleVolumeShortString(volume));
}

} // namespace

// ---------------------------------------------------------------------------
// NozzleCard — one nozzle as a vertical card
//   [Nozzle N            ✎]   (title + edit icon; green corner flag when synced)
//   [ Diameter     0.4 mm ]    read-only
//   [ Type       Standard ]    read-only
// Both values are edited together through the two-level menu the edit icon opens.
// ---------------------------------------------------------------------------
class NozzleCard : public wxPanel
{
public:
    // Told after the user picks a new value, with the slot as it was and as it is now.
    using ChangedFn = std::function<void(const NozzleConfig &from, const NozzleConfig &to)>;

    // machine_cfg: what the connected printer reports for this nozzle slot right now, or nullptr when
    // no printer is connected. The synced badge compares the card against this, never against the seed.
    // machine_slot: what the printer shows in this slot; Blank when not synced.
    NozzleCard(wxWindow *parent, int number, int extruder_index, const NozzleConfig &seed, const NozzleConfig *machine_cfg,
               MachineSlot machine_slot, const std::vector<std::string> &diameters, ChangedFn on_changed);
    ~NozzleCard() override;

    // Consulted by apply_pick() before the card changes. Returning false abandons the pick, leaving
    // the card exactly as the user found it.
    void SetConfirmChange(std::function<bool(const NozzleConfig &)> cb) { m_confirm_change = std::move(cb); }

    NozzleConfig GetConfig() const;
    SlotFace     face() const { return slot_face(GetConfig(), m_machine_slot); } // what the hotend under the card shows
    wxString     GetDiameter() const; // parsed value ("" when Empty)
    void RefreshState(); // re-derive the synced badge after an edit
    void Rescale();

private:
    void build_header(wxSizer *v, int number);
    void build_value_rows(wxSizer *v);
    void refresh_values();
    void show_edit_menu();                                   // the diameter menu, with a flow submenu per diameter
    void close_menus();                                      // both levels, in the order the guard requires
    void apply_pick(const wxString &diameter, NozzleVolumeType volume);
    bool is_synced() const; // current diameter/volume matches what the connected printer reports for this slot
    void OnPaint(wxPaintEvent &evt);

    int                              m_extruder_index{0};
    NozzleConfig                     m_seed;
    std::optional<NozzleConfig>      m_machine_cfg; // connected printer's config for this slot; unset when disconnected
    MachineSlot                      m_machine_slot{MachineSlot::Blank};
    std::vector<std::string>         m_diameters;
    ChangedFn                        m_on_changed;
    std::function<bool(const NozzleConfig &)> m_confirm_change;
    NozzleStatus                     m_status{NozzleStatus::nsUnknown};
    std::vector<std::pair<int, int>> m_ams_slots;   // preserved and passed back through GetConfig

    // The card's own state, which the menu writes and the value rows read. Held here rather than in
    // the widgets so the two stay in lockstep and so an empty slot needs no sentinel list entry.
    wxString         m_diameter; // empty means the slot carries no nozzle
    NozzleVolumeType m_volume{nvtStandard};

    PillView         *m_title{nullptr};
    wxStaticBitmap *m_edit_icon{nullptr};
    wxBitmap        m_badge_bmp;     // "badge" (synced check), drawn at the card's top-right corner
    bool            m_synced{false}; // whether this nozzle still matches the machine-seeded config
    Label          *m_diameter_value{nullptr};
    Label          *m_flow_value{nullptr};
    // Weak: the pair destroys itself when the cascade closes, and is destroyed with the card when
    // it does not. Either way these must not be left pointing at a window that is already gone.
    wxWeakRef<CascadeMenuPopup> m_menu;     // the open diameter menu, if any
    wxWeakRef<CascadeMenuPopup> m_sub_menu; // the open flow menu, if any
};

NozzleCard::NozzleCard(wxWindow *parent, int number, int extruder_index, const NozzleConfig &seed, const NozzleConfig *machine_cfg,
                       MachineSlot machine_slot, const std::vector<std::string> &diameters, ChangedFn on_changed)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_extruder_index(extruder_index)
    , m_seed(seed)
    , m_machine_cfg(machine_cfg ? std::optional<NozzleConfig>(*machine_cfg) : std::nullopt)
    , m_machine_slot(machine_slot)
    , m_diameters(diameters)
    , m_on_changed(std::move(on_changed))
    , m_status(seed.status)
    , m_ams_slots(seed.ams_slots)
    , m_diameter(seed.diameter)
    , m_volume(seed.volume)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    // The card body colour. OnPaint fills with it and every child copies it, so they blend into the body.
    SetBackgroundColour(themed(ThemeColor::Grey250));
    SetMinSize(FromDIP(wxSize(CARD_W, CARD_H)));
    Bind(wxEVT_PAINT, &NozzleCard::OnPaint, this);

    auto *v = new wxBoxSizer(wxVERTICAL);
    build_header(v, number);
    build_value_rows(v);
    SetSizer(v);
}

// Closing the dialog with a menu still up would otherwise leave the pair to DestroyChildren(),
// which deletes them in creation order and with wx's mouse capture still parked on one of them.
// Nothing in their destructors hands that capture back, and the stale entry is read on the next
// mouse event anywhere in the application.
NozzleCard::~NozzleCard()
{
    if (m_menu) m_menu->CloseCascade();
}

void NozzleCard::build_header(wxSizer *v, int number)
{
    auto *row = new wxBoxSizer(wxHORIZONTAL);

    // The title pill keeps the size of its text. The stretch spacer after it pushes the edit icon to the right edge.
    m_title = new PillView(this, GetBackgroundColour());
    m_title->SetFont(Label::Body_12.Bold());
    m_title->set_shape(PillView::Shape::Capsule);
    m_title->set_text(wxString::Format(_L("%d"), number));
    m_title->set_colors(themed(ThemeColor::TextPrimary), themed(ThemeColor::Grey400), wxTransparentColour);
    row->Add(m_title, 0, wxALIGN_CENTER_VERTICAL);
    row->AddStretchSpacer();

    m_edit_icon = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("edit", this, 14), wxDefaultPosition, FromDIP(wxSize(14, 14)), 0);
    m_edit_icon->SetBackgroundColour(GetBackgroundColour());
    m_edit_icon->SetCursor(wxCursor(wxCURSOR_HAND));
    m_edit_icon->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { show_edit_menu(); });
    row->Add(m_edit_icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(4));

    m_badge_bmp = create_scaled_bitmap("badge", this, 16); // drawn at the card's top-right corner in OnPaint

    // No left margin here: the title's own inset is the header's whole left gap.
    v->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
}

// Two read-only rows, caption left and value right. Editing goes through show_edit_menu(), which
// sets both at once -- a diameter and a flow type are only ever valid as a pair.
void NozzleCard::build_value_rows(wxSizer *v)
{
    auto add_row = [this, v](const wxString &caption) {
        auto *row  = new wxBoxSizer(wxHORIZONTAL);
        auto *name = new Label(this, caption);
        name->SetFont(Label::Body_12);
        name->SetForegroundColour(themed(ThemeColor::TextMuted));
        name->SetBackgroundColour(GetBackgroundColour());
        row->Add(name, 0, wxALIGN_CENTER_VERTICAL);

        row->AddSpacer(FromDIP(ROW_GAP));
        row->AddStretchSpacer();

        auto *value = new Label(this, wxEmptyString);
        value->SetFont(Label::Body_12);
        value->SetForegroundColour(themed(ThemeColor::TextPrimary));
        value->SetBackgroundColour(GetBackgroundColour());
        row->Add(value, 0, wxALIGN_CENTER_VERTICAL);

        v->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(ROW_PAD));
        return value;
    };

    m_diameter_value = add_row(_L("Diameter"));
    m_flow_value     = add_row(_L("Type"));
    v->AddSpacer(FromDIP(8));

    refresh_values();
}

void NozzleCard::refresh_values()
{
    // An empty slot, a 2D tool or an unknown tool has no flow type to report, so the row reads as a dash rather than
    // claiming the Standard the config happens to default to.
    wxString diameter = m_diameter; // no unit, as in the design: "0.4 mm" does not fit beside the caption at 12px
    wxString flow     = flow_label(m_volume);
    switch (face()) {
    case SlotFace::Empty: diameter = _L("Empty"); flow = "-"; break;
    case SlotFace::Cutter:
    case SlotFace::Pen:
    case SlotFace::Unknown: diameter = "-"; flow = "-"; break;
    default: break;
    }
    // A long translation such as "Empty" would be cut off at 12px in the narrow card.
    if (m_diameter_value) set_fitting_value(m_diameter_value, _L("Diameter"), diameter);
    if (m_flow_value) set_fitting_value(m_flow_value, _L("Type"), flow);
    Layout();
    GetParent()->Refresh(); // the hotend under the card follows the face
}

void NozzleCard::apply_pick(const wxString &diameter, NozzleVolumeType volume)
{
    if (m_diameter == diameter && m_volume == volume) return;
    // Ask before the value moves, not when the dialog is confirmed: by then the user has made
    // several picks and would be told, at the end, that some of them displace project filaments --
    // with no way to tell which pick caused what. Declining here just abandons this one pick.
    const NozzleConfig from = GetConfig();
    if (m_confirm_change && !m_confirm_change(from)) return;
    m_diameter = diameter;
    m_volume   = volume;
    refresh_values();
    if (m_on_changed) m_on_changed(from, GetConfig());
}

// Diameters at the first level, each opening the flow types that diameter is made in. The pair is
// picked in one gesture, so the card can never sit on a combination no nozzle exists in -- which
// the two linked dropdowns this replaced could only avoid by rebuilding each other on every change.
void NozzleCard::show_edit_menu()
{
    // Normally the previous pair closed and destroyed itself; this covers a path that left one up.
    if (m_menu) m_menu->CloseCascade();
    m_menu     = nullptr;
    m_sub_menu = nullptr;

    auto *menu  = new CascadeMenuPopup(this);
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    m_menu      = menu;

    // A fresh pair is built for every gesture, so the outgoing one is of no further use: let it go
    // with the cascade rather than linger as a hidden child of the card until the next open -- a
    // dismissed popup keeps whatever wx parked on it, and is destroyed by DestroyChildren() at the
    // worst possible moment if the dialog closes first.
    menu->SetDestroyOnClose(true);
    menu->SetOnClosed([this] {
        m_menu     = nullptr;
        m_sub_menu = nullptr;
    });

    // One flow menu for the whole gesture, refilled as the pointer moves between diameters, exactly
    // as DropDown reuses its own submenu. Destroying a popup and building a new one on every hover
    // does not work: Destroy() on a top-level window only queues the deletion for idle time, so the
    // outgoing window is usually still alive -- and still holding wx's popup handlers -- when the
    // incoming one pops, which is what made the submenu appear only some of the time.
    auto *sub  = new CascadeMenuPopup(this);
    m_sub_menu = sub;
    sub->Hide(); // the guard on the diameter menu keys off this, so start it in a known state
    sub->SetMainMenu(menu);
    menu->SetSubMenu(sub);

    bool first_row = true;
    for (const auto &d : m_diameters) {
        const wxString diameter = wxString::FromUTF8(d);
        auto          *item     = new CascadeMenuItem(menu, diameter + " mm", true, diameter == m_diameter, true);

        // Hover opens the submenu, click does too: the chevron reads as hover-to-open, but a user
        // who clicks the row instead should not be met with nothing happening.
        auto open_sub = [this, item, diameter] {
            auto *sub = m_sub_menu.get();
            if (!sub) return;

            // Refill in place. The sizer goes before its items do, so it never holds dead rows.
            sub->SetSizer(nullptr);
            sub->DestroyChildren();
            auto *sub_box = new wxBoxSizer(wxVERTICAL);

            bool first = true;
            for (const auto &flow : flow_options_for_diameter(m_extruder_index, diameter)) {
                const bool checked  = diameter == m_diameter && flow.volume == m_volume;
                auto      *sub_item = new CascadeMenuItem(sub, flow.label, true, checked, false);
                sub_item->Bind(wxEVT_BUTTON, [this, diameter, flow](wxCommandEvent &) {
                    // Tear the menus down first and let this handler return before applying: the pick
                    // can raise a modal confirmation, and a modal loop entered from inside a menu
                    // item's own handler lets the popup lose focus and destroy itself (it is created
                    // with SetDestroyOnClose) while that handler is still on the stack.
                    close_menus();
                    const NozzleVolumeType volume = flow.volume;
                    CallAfter([this, diameter, volume]() { apply_pick(diameter, volume); });
                });
                CascadeMenu::AddItem(sub_box, sub_item, first);
                first = false;
            }
            CascadeMenu::Finish(sub, sub_box);

            CascadeMenu::PositionSubMenu(sub, m_menu, item);
            if (!sub->IsShown())
                sub->Popup();
            else
                sub->Refresh();
        };
        item->Bind(wxEVT_ENTER_WINDOW, [open_sub](wxMouseEvent &e) {
            open_sub();
            e.Skip();
        });
        item->Bind(wxEVT_LEFT_DOWN, [open_sub](wxMouseEvent &) {
            open_sub();
            // Consume the click here. Letting it bubble into wxPopupTransientWindow makes the
            // diameter popup treat the completed click as a reason to dismiss itself.
        });

        CascadeMenu::AddItem(sizer, item, first_row);
        first_row = false;
    }

    // Clearing the slot needs no flow type, so it stays a leaf rather than a diameter row.
    auto *empty_item = new CascadeMenuItem(menu, _L("Empty"), true, m_diameter.IsEmpty(), false);
    empty_item->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        // Same ordering as the flow items above -- see the comment there.
        close_menus();
        const NozzleVolumeType volume = m_volume;
        CallAfter([this, volume]() { apply_pick(wxEmptyString, volume); });
    });
    CascadeMenu::AddItem(sizer, empty_item, first_row);

    CascadeMenu::Finish(menu, sizer);

    // Anchored on the edit icon rather than on the card: the design drops the panel from the icon,
    // and a card is tall enough that dropping from its bottom edge lands the menu over the row of
    // cards below it. Position() is given the icon's size so it can flip the menu above it near the
    // bottom of the screen; the x offset lines the rows up with the icon, past the panel's inset.
    menu->Position(m_edit_icon->GetScreenPosition() - wxPoint(menu->FromDIP(CascadeMenu::Pad), 0), m_edit_icon->GetSize());
    menu->Popup();
}

// Both levels, in the order the guard requires and with wx's mouse capture handed back before the
// windows go. CloseCascade() owns all of that; this is the pick path, while a click outside the
// cascade arrives through CascadeMenuPopup::OnDismiss() and ends in the same place.
void NozzleCard::close_menus()
{
    if (m_menu) m_menu->CloseCascade();
}

wxString NozzleCard::GetDiameter() const { return m_diameter; }

NozzleConfig NozzleCard::GetConfig() const
{
    NozzleConfig cfg;
    cfg.diameter  = m_diameter;
    cfg.volume    = m_volume;
    cfg.status    = m_status;
    cfg.ams_slots = m_ams_slots;
    return cfg;
}

// Synced = a printer is connected and this nozzle's diameter/volume matches what that printer reports
// for this slot. No connected printer, or an empty nozzle, is never synced.
bool NozzleCard::is_synced() const
{
    if (!m_machine_cfg) return false;
    const NozzleConfig cur = GetConfig();
    if (cur.diameter.IsEmpty()) return false;
    return cur.diameter == m_machine_cfg->diameter && cur.volume == m_machine_cfg->volume;
}

void NozzleCard::RefreshState()
{
    if (!m_diameter.IsEmpty() && m_status != NozzleStatus::nsNormal) m_status = NozzleStatus::nsNormal;

    const bool synced = is_synced();
    if (m_synced != synced) { // corner badge changed -> repaint
        m_synced = synced;
        Refresh();
    }
}

void NozzleCard::Rescale()
{
    SetMinSize(FromDIP(wxSize(CARD_W, CARD_H)));
    refresh_values(); // the fitting font depends on the DPI
}

// Rounded card body in the card's background colour with a grey border. The "Nozzle N" pill draws its own background.
void NozzleCard::OnPaint(wxPaintEvent &)
{
    wxPaintDC          pdc(this);
    wxGCDC             dc(pdc);
    wxGraphicsContext *gc = dc.GetGraphicsContext();
    if (gc == nullptr) return;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    const double w  = GetClientSize().GetWidth();
    const double h  = GetClientSize().GetHeight();
    const double r  = FromDIP(6);
    const double bw = FromDIP(1); // border thickness

    const wxColour outside = themed(wxColour("#F7F7F7"));
    const wxColour card_bg = GetBackgroundColour();
    const wxColour border  = themed(ThemeColor::Grey400);

    // Fill the corners with the container color so the rounded card sits cleanly on it.
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(outside));
    gc->DrawRectangle(0, 0, w, h);

    // Even border: paint the whole card in the border color, then lay the body on top inset by the
    // border width. The outline is the exposed ring, so its thickness is geometric and identical on
    // every edge — a centered 1px pen splits the half-pixel unevenly under fractional DPI scaling.
    gc->SetBrush(wxBrush(border));
    gc->DrawRoundedRectangle(0, 0, w, h, r);

    gc->SetBrush(wxBrush(card_bg));
    gc->DrawRoundedRectangle(bw, bw, w - 2 * bw, h - 2 * bw, std::max(0.0, r - bw));

    // Synced badge at the card's top-right corner.
    if (m_synced && m_badge_bmp.IsOk()) {
        const double bw = FromDIP(16);
        gc->DrawBitmap(m_badge_bmp, w - bw, 0, bw, bw);
    }
}

// ---------------------------------------------------------------------------
// NozzleRackView — the nozzle cards in two staggered rows, each wired by a dashed filament tube
// down to its hotend. Every hotend sits on one shared baseline below the lower row.
//   even numbers:      [2]     [4]
//   odd numbers:   [1]  :  [3]  :  [5]
//                   :   :   :   :   :
//                   H   H   H   H   H
// The upper row is offset by half a card pitch, so its tubes run down the gaps between lower cards.
// ---------------------------------------------------------------------------
namespace {

class NozzleRackView : public wxPanel
{
public:
    explicit NozzleRackView(wxWindow *parent);

    /**
     * \brief Place \p card in the rack. Cards are numbered 1..n in the order they are added.
     *
     * \param card  a card whose parent is this view
     */
    void add_card(wxWindow *card);

private:
    wxSizer *row(bool even) const;
    void     draw_tube(wxDC &dc, int cx, int top, int bottom) const;
    void     OnPaint(wxPaintEvent &evt);
};

constexpr int    TUBE_MIN_H   = 24; // tube length under a lower-row card
constexpr int    HOTEND_W     = 24;
constexpr int    HOTEND_H     = 48;
constexpr double TOOL_OPACITY = 0.5; // a 2D tool is drawn faded: the project cannot print with it
constexpr int    UNKNOWN_D    = 20;  // the "?" badge centred on an unknown tool

/**
 * \brief \p bitmap blended onto \p background at \p opacity
 */
wxBitmap faded_bitmap(const wxBitmap &bitmap, const wxColour &background, double opacity)
{
    auto blend = [opacity](unsigned char foreground, unsigned char backdrop) {
        return static_cast<unsigned char>(foreground * opacity + backdrop * (1.0 - opacity) + 0.5);
    };
    wxImage image = bitmap.ConvertToImage();
    for (int y = 0; y < image.GetHeight(); ++y)
        for (int x = 0; x < image.GetWidth(); ++x)
            image.SetRGB(x, y, blend(image.GetRed(x, y), background.Red()), blend(image.GetGreen(x, y), background.Green()),
                         blend(image.GetBlue(x, y), background.Blue()));
    return wxBitmap(image, -1, bitmap.GetScaleFactor());
}

/**
 * \brief The hotend art under a card with face \p face
 *
 * \param window      the rack view, for DPI and theme
 * \param background  the colour a faded tool blends onto
 */
wxBitmap hotend_bitmap(wxWindow *window, SlotFace face, const wxColour &background)
{
    // The hotend assets run a little past their 48px slot; the overhang is clipped as in the design.
    // The empty box outlines the slot itself, so it loads at the slot height or its bottom edge is clipped.
    switch (face) {
    case SlotFace::Empty: return create_scaled_bitmap("prepare_nozzle_empty", window, HOTEND_H);
    case SlotFace::Cutter: return faded_bitmap(create_scaled_bitmap("prepare_nozzle_cutter", window, 52), background, TOOL_OPACITY);
    case SlotFace::Pen: return faded_bitmap(create_scaled_bitmap("prepare_nozzle_pen", window, 52), background, TOOL_OPACITY);
    default: return create_scaled_bitmap("prepare_nozzle_normal", window, 52);
    }
}

NozzleRackView::NozzleRackView(wxWindow *parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(*wxWHITE);

    auto *v = new wxBoxSizer(wxVERTICAL);
    v->Add(new wxBoxSizer(wxHORIZONTAL), 0, wxLEFT, FromDIP((CARD_W + CARD_GAP) / 2)); // even numbers
    v->Add(new wxBoxSizer(wxHORIZONTAL), 0, wxTOP, FromDIP(CARD_GAP));                 // odd numbers
    v->AddSpacer(FromDIP(TUBE_MIN_H + HOTEND_H));
    SetSizer(v);

    Bind(wxEVT_PAINT, &NozzleRackView::OnPaint, this);
}

wxSizer *NozzleRackView::row(bool even) const { return GetSizer()->GetItem(size_t(even ? 0 : 1))->GetSizer(); }

void NozzleRackView::add_card(wxWindow *card)
{
    wxASSERT(card->GetParent() == this);
    const int number = int(row(true)->GetItemCount() + row(false)->GetItemCount()) + 1;
    wxSizer  *r      = row(number % 2 == 0);
    r->Add(card, 0, wxLEFT, r->IsEmpty() ? 0 : FromDIP(CARD_GAP));
}

// 2px dashes, 4 on / 4 off, phased from the bottom so the last dash runs on into the hotend's own.
void NozzleRackView::draw_tube(wxDC &dc, int cx, int top, int bottom) const
{
    const int w = FromDIP(2), dash = FromDIP(4), period = FromDIP(8);
    for (int y = bottom + FromDIP(2); y > top; y -= period) {
        const int y0 = std::max(top, y - dash);
        const int y1 = std::min(bottom, y);
        if (y1 > y0) dc.DrawRectangle(cx - w / 2, y0, w, y1 - y0);
    }
}

void NozzleRackView::OnPaint(wxPaintEvent &)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();

    // The baseline follows the cards actually laid out, not a cached height.
    int lowest = 0;
    for (wxWindow *card : GetChildren())
        if (card->IsShown()) lowest = std::max(lowest, card->GetRect().GetBottom() + 1);
    const int hotend_top = lowest + FromDIP(TUBE_MIN_H);

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(themed(ThemeColor::Grey400)));
    for (wxWindow *child : GetChildren()) {
        auto *card = dynamic_cast<NozzleCard *>(child);
        if (!card || !card->IsShown()) continue;
        const wxRect rc = card->GetRect();
        const int    cx = rc.GetLeft() + rc.GetWidth() / 2;
        draw_tube(dc, cx, rc.GetBottom() + 1, hotend_top);

        const wxBitmap hotend = hotend_bitmap(this, card->face(), GetBackgroundColour());
        wxDCClipper    clip(dc, wxRect(cx - FromDIP(HOTEND_W) / 2, hotend_top, FromDIP(HOTEND_W), FromDIP(HOTEND_H)));
        // Logical width, not GetWidth(): on macOS the bitmap carries a 2x scale factor, so its pixel
        // width is twice what DrawBitmap lays down and centering on it pushes the hotend a full icon left.
        dc.DrawBitmap(hotend, cx - int(hotend.GetScaledWidth()) / 2, hotend_top, true);
        if (card->face() == SlotFace::Unknown) {
            const wxBitmap mark = create_scaled_bitmap("hotend_unknown_badge", this, UNKNOWN_D);
            dc.DrawBitmap(mark, cx - int(mark.GetScaledWidth()) / 2, hotend_top + (FromDIP(HOTEND_H) - int(mark.GetScaledHeight())) / 2, true);
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// NozzleSettingsCardDialog
// ---------------------------------------------------------------------------
NozzleSettingsCardDialog::NozzleSettingsCardDialog(wxWindow *parent, const wxString &selected_diameter, const std::vector<NozzleConfig> &configs)
    : DPIDialog(parent, wxID_ANY, _L("Nozzle Settings"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_selected_diameter(selected_diameter)
{
    std::vector<NozzleConfig> seeds = configs;
    if (seeds.empty()) seeds.emplace_back();

    SetBackgroundColour(*wxWHITE);
    create_layout(seeds);
    CentreOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void NozzleSettingsCardDialog::create_layout(const std::vector<NozzleConfig> &seeds)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % Slic3r::resources_dir()).str();
    SetIcon(wxIcon(Slic3r::encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    auto *main_sizer = new wxBoxSizer(wxVERTICAL);
    build_card_grid(main_sizer, seeds);
    build_ams_row(main_sizer);
    build_button_bar(main_sizer);

    SetSizer(main_sizer);
    Layout();
    Fit();

    update_state();
}

void NozzleSettingsCardDialog::build_card_grid(wxBoxSizer *main_sizer, const std::vector<NozzleConfig> &seeds)
{
    auto *label = new Label(this, _L("Set your printer's nozzle installation status"));
    label->SetFont(Label::Body_14.Bold());
    main_sizer->Add(label, 0, wxALIGN_LEFT | wxLEFT | wxTOP, FromDIP(20));

    auto *rack = new NozzleRackView(this);

    auto on_changed = [this](const NozzleConfig &from, const NozzleConfig &to) {
        m_edit_tally.record_nozzle_pick(from, to);
        update_state();
    };
    auto diameters  = wxGetApp().preset_bundle->printers.diameters_of_selected_printer();

    // What the connected printer reports right now; the per-card synced badge compares against this,
    // never against the seed. A disconnected printer leaves `synced` false, so no card is ever synced.
    // What each slot shows comes from a second read with its own health rule; one machine-slot collector
    // would remove this pairing (see the TODO on NozzleConfig).
    const MachineNozzleBaseline baseline     = collect_machine_nozzle_baseline(false); // card dialog is single-extruder
    const auto                 &machine_cfgs = baseline.configs[0];
    DeviceManager              *dev          = wxGetApp().getDeviceManager();
    const auto shown = baseline.synced ? machine_slots(dev ? dev->get_selected_machine() : nullptr, LOGIC_UNIQUE_EXTRUDER_ID)
                                       : std::vector<MachineSlot>{};

    // Single extruder: nozzles number 1..n in the order the rack receives them.
    for (int i = 0; i < (int) seeds.size(); ++i) {
        const NozzleConfig *machine_cfg = (baseline.synced && i < (int) machine_cfgs.size()) ? &machine_cfgs[i] : nullptr;
        const auto          kind        = i < (int) shown.size() ? shown[i] : MachineSlot::Blank;
        auto               *card        = new NozzleCard(rack, i + 1, 0, seeds[i], machine_cfg, kind, diameters, on_changed);
        card->SetConfirmChange([this](const NozzleConfig &from) { return confirm_nozzle_change(from); });
        m_cards.push_back(card);
        rack->add_card(card);
    }

    main_sizer->Add(rack, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));
}

void NozzleSettingsCardDialog::build_button_bar(wxBoxSizer *main_sizer)
{
    StateColor ok_bg(std::pair<wxColour, int>(ThemeColor::BrandGreenPressed, StateColor::Pressed), std::pair<wxColour, int>(ThemeColor::BrandGreenHovered, StateColor::Hovered),
                     std::pair<wxColour, int>(ThemeColor::BrandGreen, StateColor::Normal));
    StateColor ok_text(std::pair<wxColour, int>(ThemeColor::WhiteFixed, StateColor::Normal));
    StateColor cancel_bg(std::pair<wxColour, int>(ThemeColor::Grey400, StateColor::Pressed), std::pair<wxColour, int>(ThemeColor::Grey300, StateColor::Hovered),
                         std::pair<wxColour, int>(ThemeColor::White, StateColor::Normal));
    StateColor cancel_bd(std::pair<wxColour, int>(ThemeColor::TextPrimary, StateColor::Normal));

    m_confirm_btn = new Button(this, _L("Confirm"));
    m_confirm_btn->SetMinSize(wxSize(FromDIP(62), FromDIP(24)));
    m_confirm_btn->SetCornerRadius(FromDIP(12));
    m_confirm_btn->SetBackgroundColor(ok_bg);
    m_confirm_btn->SetFont(Label::Body_12);
    m_confirm_btn->SetBorderColor(ThemeColor::BrandGreen);
    m_confirm_btn->SetTextColor(ok_text);
    m_confirm_btn->SetId(wxID_OK);

    m_cancel_btn = new Button(this, _L("Cancel"));
    m_cancel_btn->SetMinSize(wxSize(FromDIP(62), FromDIP(24)));
    m_cancel_btn->SetCornerRadius(FromDIP(12));
    m_cancel_btn->SetBackgroundColor(cancel_bg);
    m_cancel_btn->SetFont(Label::Body_12);
    m_cancel_btn->SetBorderColor(cancel_bd);
    m_cancel_btn->SetTextColor(cancel_bd);
    m_cancel_btn->SetId(wxID_CANCEL);
    // Skip lets wxDialog end the modal with wxID_OK as before.
    m_confirm_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &e) {
        m_edit_tally.commit();
        e.Skip();
    });
    m_cancel_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Close(); });

    // AMS edits persist to the live registry the moment they are made, so every dismissal other than
    // Confirm must revert them. Funnel the Cancel button through the same close path as the window's
    // X button; the system close reaches EndModal without this handler otherwise, keeping the edits.
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) {
        if (m_ams_row) m_ams_row->restore();
        EndModal(wxID_CANCEL);
    });

    auto *btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->AddStretchSpacer();
    btn_sizer->Add(m_confirm_btn, 0, wxRIGHT, FromDIP(12));
    btn_sizer->Add(m_cancel_btn, 0);
    main_sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, FromDIP(20));
}

// The nozzle a project filament slot is pointing at, in multi-diameter mode. Outside that mode the
// per-slot map is not what drives printing, so editing the rack cannot strand an individual slot.
static bool project_filament_uses_nozzle(const NozzleConfig &nozzle)
{
    PresetBundle *preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle == nullptr || !multi_nozzle_diameter_mode_enabled(preset_bundle->project_config))
        return false;
    const auto *diameters = preset_bundle->project_config.option<ConfigOptionStrings>("filament_diameter_map");
    const auto *volumes   = preset_bundle->project_config.option<ConfigOptionInts>("filament_volume_map");
    if (diameters == nullptr)
        return false;
    const std::string wanted = into_u8(nozzle.diameter);
    for (size_t i = 0; i < diameters->values.size(); ++i) {
        if (preset_bundle->is_mixed_filament(i))
            continue;
        if (diameters->values[i] != wanted)
            continue;
        const NozzleVolumeType volume = (volumes != nullptr && i < volumes->values.size())
            ? static_cast<NozzleVolumeType>(volumes->values[i]) : nvtStandard;
        if (volume == nozzle.volume)
            return true;
    }
    return false;
}

bool NozzleSettingsCardDialog::confirm_nozzle_change(const NozzleConfig &from)
{
    // An empty slot carries nothing for a filament to be pointing at.
    if (from.diameter.IsEmpty())
        return true;

    // Still another nozzle of the same kind after this edit, so the filaments keep a home and
    // nothing has to move.
    int same_kind = 0;
    for (const NozzleCard *card : m_cards) {
        const NozzleConfig cfg = card->GetConfig();
        if (cfg.diameter == from.diameter && cfg.volume == from.volume)
            ++same_kind;
    }
    if (same_kind > 1)
        return true;

    if (!project_filament_uses_nozzle(from))
        return true;

    // flow_label() is what the cards and the picker already show, so the question names the nozzle
    // the same way the user just read it ("0.4 mm / 标准"), not an abbreviation that appears nowhere
    // else in this dialog.
    const wxString nozzle_name = from.diameter + " " + flow_label(from.volume);
    MessageDialog confirm(this,
        wxString::Format(_L("The %s nozzle is in use by project filaments. Changing it will move them to 0.4 Standard. Change it anyway?"), nozzle_name),
        _L("Nozzle Settings"), wxYES | wxNO);
    confirm.SetButtonLabel(wxID_YES, _L("Confirm"));
    confirm.SetButtonLabel(wxID_NO, _L("Cancel"));
    return confirm.ShowModal() == wxID_YES;
}

void NozzleSettingsCardDialog::update_state()
{
    for (auto *c : m_cards) c->RefreshState();

    Layout();
    Fit();
    wxGetApp().UpdateDarkUIWin(this);
}

// The dialog records which nozzle is installed where; which diameter the plate slices at stays with
// the sidebar selector. Handing the caller back what it passed in keeps its write-back a no-op,
// except where it overrides this itself after reading the configs.
wxString NozzleSettingsCardDialog::GetSelectedDiameter() const { return m_selected_diameter; }

std::vector<NozzleConfig> NozzleSettingsCardDialog::GetExtruderConfigs(int index) const
{
    std::vector<NozzleConfig> configs;
    if (index != 0) return configs; // single extruder
    configs.reserve(m_cards.size());
    for (auto *c : m_cards) configs.push_back(c->GetConfig());
    return configs;
}

// External-AMS row: a standalone Grey200 band carrying the shared NozzleAmsRow widget. wxEXPAND
// stretches the band full width; the widget self-hides when the printer has no AMS attach point.
void NozzleSettingsCardDialog::build_ams_row(wxBoxSizer *main_sizer)
{
    m_ams_row = new NozzleAmsRow(this, 0, _L("External AMS"), NozzleAmsRow::Style::Band, [this] {
        m_edit_tally.observe_ams_counts();
        Layout();
        Fit();
    });
    main_sizer->Add(m_ams_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(20));
}

void NozzleSettingsCardDialog::on_dpi_changed(const wxRect & /*suggested_rect*/)
{
    for (auto *c : m_cards) c->Rescale();
    if (m_ams_row) m_ams_row->Rescale();

    auto rescale_btn = [this](Button *btn) {
        if (!btn) return;
        btn->SetMinSize(wxSize(FromDIP(62), FromDIP(24)));
        btn->SetCornerRadius(FromDIP(12));
        btn->Rescale();
    };
    rescale_btn(m_confirm_btn);
    rescale_btn(m_cancel_btn);

    Layout();
    Fit();
    Refresh();
}

} // namespace Slic3r::GUI
