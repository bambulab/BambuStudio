#include "DualNozzleView.hpp"

#include <algorithm>
#include <vector>

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/sizer.h>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h> // WS_CLIPSIBLINGS
#endif

#include "ExtruderHelpers.hpp" // printer_offers_flow, show_filament_switcher_tip
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "wxExtensions.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CascadeMenu.hpp"
#include "Widgets/IconView.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/PillView.hpp"
#include "Widgets/StateColor.hpp"
#include "libslic3r/NozzleInventory.hpp"
#include "libslic3r/PresetBundle.hpp"

namespace Slic3r::GUI {

namespace {
constexpr int CARD_RADIUS   = 4;
constexpr int CARD_GAP      = 8;  // between the picker, the cards and the rows
constexpr int PICK_W        = 56; // minimum width of a picker button; a longer label widens it
constexpr int PICK_H        = 24;
constexpr int PICK_GAP      = 4;
constexpr int BADGE_D       = 16; // the sync check in a card's top-right corner
constexpr int EDIT_ICON_D   = 16;
constexpr int CARD_INSET    = 8;  // between the card edge and its title and rows
constexpr int HEADER_PAD_R  = 12; // keeps the "in use" chip clear of the sync check
constexpr int SWITCHER_H    = 12; // the filament switcher icon; wider than the gap, so it overlaps both cards

/**
 * \brief A design colour as the current light or dark theme shows it.
 *
 * Map only where the colour is used, never store the result: UpdateDarkUIWin() also maps the colours
 * a window holds, so a stored mapped colour would be mapped twice, and stale after a theme switch.
 */
wxColour themed(const wxColour &color) { return StateColor::darkModeColorFor(color); }

/// \brief Display name of a volume type, as the printer settings list it.
wxString volume_type_label(NozzleVolumeType type)
{
    const ConfigOptionDef *def = wxGetApp().preset_bundle->project_config.def()->get("nozzle_volume_type");
    if (def == nullptr) return {};
    for (size_t i = 0; i < def->enum_values.size(); ++i)
        if (NozzleVolumeType(def->enum_keys_map->at(def->enum_values[i])) == type)
            return _L(def->enum_labels[i]);
    return {};
}

/// \brief The nozzle of one extruder; an empty slot when it has none.
NozzleSlot nozzle_of(int extruder)
{
    const ExtruderSlots &slots = wxGetApp().preset_bundle->nozzle_inventory.slots(extruder);
    return slots.empty() ? NozzleSlot() : slots.front();
}

/// \brief Text, fill and border of a picker button, selected or not.
void style_pick(Button *button, bool selected)
{
    const wxColour fill   = selected ? ThemeColor::BrandGreen : ThemeColor::White;
    const wxColour border = selected ? ThemeColor::BrandGreen : ThemeColor::Grey400;
    const wxColour text   = selected ? ThemeColor::WhiteFixed : ThemeColor::TextPrimary;
    button->SetBackgroundColor(StateColor(std::pair{fill, int(StateColor::Normal)}));
    button->SetBorderColor(StateColor(std::pair{border, int(StateColor::Normal)}));
    button->SetTextColor(StateColor(std::pair{text, int(StateColor::Normal)}));
    button->Refresh();
}
} // namespace

/**
 * \brief One nozzle as a small grey card: a title chip, an edit icon, an "in use" chip, then the
 *        diameter and type rows.
 *
 * A nozzle that does not print shows its rows greyed, with a tip that says how to change it.
 */
class NozzleInfoCardView : public wxPanel
{
public:
    NozzleInfoCardView(wxWindow *parent, std::function<void(wxWindow *)> on_edit);

    void set_title(const wxString &title);

    /// \brief Whether the nozzle prints, and if not, what the user does to change it.
    enum class Usage {
        Unmarked,   ///< no chip and no grey look: the multi-diameter mode, where every nozzle prints
        InUse,      ///< the "in use" chip
        NotUsable,  ///< greyed; no nozzle, or not the printing diameter
    };

    /**
     * \param nozzle  the nozzle to show; an empty slot shows dashes
     * \param usage   the chip, the grey look and its tooltip
     */
    void update(const NozzleSlot &nozzle, Usage usage);

    void set_synced(bool synced);

private:
#ifdef __WXMSW__
    // The switcher icon overlaps the card. wxMSW leaves siblings unclipped, so without this a card
    // repaint covers the icon. Keep the icon above the cards in Z-order.
    WXDWORD MSWGetStyle(long flags, WXDWORD *exstyle) const override { return wxPanel::MSWGetStyle(flags, exstyle) | WS_CLIPSIBLINGS; }
#endif
    void   add_row(wxBoxSizer *rows, Label *&key, Label *&value, const wxString &key_text);
    Label *make_label(const wxString &text);
    void   apply_theme();
    void   on_paint(wxPaintEvent &evt);

    PillView      *m_title{nullptr};
    PillView      *m_in_use{nullptr};
    Label         *m_diameter_key{nullptr};
    Label         *m_diameter{nullptr};
    Label         *m_type_key{nullptr};
    Label         *m_type{nullptr};
    bool           m_synced{false};
    bool           m_grayed{false}; // the rows show greyed: the nozzle does not print
};

NozzleInfoCardView::NozzleInfoCardView(wxWindow *parent, std::function<void(wxWindow *)> on_edit)
{
    // Two-step creation: the base constructor would create the window before MSWGetStyle() is overridden.
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE);
    Bind(wxEVT_PAINT, &NozzleInfoCardView::on_paint, this);

    auto *header = new wxBoxSizer(wxHORIZONTAL);
    m_title      = new PillView(this, ThemeColor::Grey200);
    m_title->SetFont(Label::Body_12);
    m_title->set_shape(PillView::Shape::Capsule);
    header->Add(m_title, 0, wxALIGN_CENTER_VERTICAL);

    auto *edit = new IconView(this, "edit", EDIT_ICON_D);
    edit->SetCursor(wxCursor(wxCURSOR_HAND));
    edit->SetToolTip(_L("Edit nozzle"));
    edit->Bind(wxEVT_LEFT_UP, [edit, on_edit](wxMouseEvent &) { on_edit(edit); });
    header->Add(edit, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    header->AddStretchSpacer();

    m_in_use = new PillView(this, ThemeColor::Grey200);
    m_in_use->SetFont(Label::Body_10);
    m_in_use->set_shape(PillView::Shape::Capsule);
    m_in_use->set_text(_L("In use"));
    header->Add(m_in_use, 0, wxALIGN_CENTER_VERTICAL);

    auto *rows = new wxBoxSizer(wxVERTICAL);
    add_row(rows, m_diameter_key, m_diameter, _L("Diameter"));
    rows->AddSpacer(FromDIP(CARD_GAP));
    add_row(rows, m_type_key, m_type, _L("Type"));

    // Asymmetric side insets: the header keeps clear of the sync check, the rows line up with the title.
    auto *outer      = new wxBoxSizer(wxVERTICAL);
    auto *header_box = new wxBoxSizer(wxHORIZONTAL);
    header_box->Add(header, 1, wxEXPAND | wxLEFT, FromDIP(CARD_INSET));
    header_box->AddSpacer(FromDIP(HEADER_PAD_R));
    outer->Add(header_box, 0, wxEXPAND | wxTOP, FromDIP(4));
    outer->AddSpacer(FromDIP(12));
    outer->Add(rows, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(CARD_INSET - 4));
    outer->AddSpacer(FromDIP(12));
    SetSizer(outer);
    apply_theme();
}

Label *NozzleInfoCardView::make_label(const wxString &text)
{
    auto *label = new Label(this, text);
    label->SetFont(Label::Body_11);
    return label;
}

// Every colour the card and its children hold, mapped for the current theme from the raw tokens. The
// one place that sets them, so a later call fixes whatever a theme switch left behind.
void NozzleInfoCardView::apply_theme()
{
    const wxColour surface = themed(ThemeColor::Grey200);
    const wxColour text    = themed(m_grayed ? ThemeColor::TextDisabled : ThemeColor::TextPrimary);
    SetBackgroundColour(surface);
    for (Label *label : {m_diameter_key, m_diameter, m_type_key, m_type}) {
        label->SetBackgroundColour(surface);
        label->SetForegroundColour(text);
    }
    m_title->SetBackgroundColour(surface);
    m_in_use->SetBackgroundColour(surface);
    m_title->set_colors(themed(ThemeColor::TextPrimary), themed(ThemeColor::Grey300), wxTransparentColour);
    m_in_use->set_colors(themed(ThemeColor::BrandGreen), themed(ThemeColor::LightGreen), wxTransparentColour);
    Refresh();
}

void NozzleInfoCardView::add_row(wxBoxSizer *rows, Label *&key, Label *&value, const wxString &key_text)
{
    key   = make_label(key_text);
    value = make_label(wxEmptyString);
    auto *row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(key, 0, wxALIGN_CENTER_VERTICAL);
    row->AddStretchSpacer();
    row->Add(value, 0, wxALIGN_CENTER_VERTICAL);
    rows->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(4));
}

void NozzleInfoCardView::set_title(const wxString &title) { m_title->set_text(title); }

void NozzleInfoCardView::update(const NozzleSlot &nozzle, Usage usage)
{
    m_diameter->SetLabel(nozzle.empty() ? wxString("--") : wxString::FromUTF8(nozzle.diameter) + "mm");
    m_type->SetLabel(nozzle.empty() ? wxString("--") : volume_type_label(nozzle.volume_type));

    m_grayed     = usage == Usage::NotUsable;
    wxString tip;
    if (usage == Usage::NotUsable)
        tip = nozzle.empty() ? _L("This side has no nozzle. Click Edit to add one.")
                             : _L("This nozzle has another diameter. Select it above to use it.");
    for (Label *label : {m_diameter_key, m_diameter, m_type_key, m_type}) label->SetToolTip(tip);
    SetToolTip(tip);
    // On each update, so a theme switch shows on the next refresh.
    apply_theme();
    m_in_use->Show(usage == Usage::InUse);
    Layout();
    Refresh();
}

void NozzleInfoCardView::set_synced(bool synced)
{
    if (m_synced == synced) return;
    m_synced = synced;
    Refresh();
}

// The parent's colour at the rounded corners, the card fill, then the sync check in the top-right corner.
void NozzleInfoCardView::on_paint(wxPaintEvent &)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
    dc.Clear();

    const wxSize size = GetClientSize();
    if (std::unique_ptr<wxGraphicsContext> gc{wxGraphicsContext::Create(dc)}) {
        gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->SetBrush(wxBrush(themed(ThemeColor::Grey200)));
        gc->DrawRoundedRectangle(0, 0, size.x, size.y, FromDIP(CARD_RADIUS));
    }
    if (m_synced) {
        const wxBitmap badge = create_scaled_bitmap("badge", this, BADGE_D);
        dc.DrawBitmap(badge, size.x - ScalableBitmap::GetBmpSize(badge).x, 0, true);
    }
}

/**
 * \brief The two-level menu that edits one nozzle: diameters first, then the types made at that diameter.
 *
 * Kept for the life of its owner and refilled on each open, as FilamentNozzlePopup does.
 */
class NozzleEditMenu : public CascadeMenuPopup
{
public:
    using OnPick = std::function<void(const std::string &, NozzleVolumeType)>;

    explicit NozzleEditMenu(wxWindow *parent);

    /**
     * \brief Open under \p anchor for one extruder, with its current nozzle ticked.
     *
     * \param on_pick  called after the menu closed, with the picked diameter and type
     */
    void open(int extruder, const NozzleSlot &current, wxWindow *anchor, OnPick on_pick);

private:
    std::vector<NozzleVolumeType> types_for(const std::string &diameter) const;
    void                          build_menu();
    void                          open_type_menu(const std::string &diameter, const wxWindow *anchor);
    void                          close_menus();

    CascadeMenuPopup *m_sub{nullptr};
    int               m_extruder{0};
    NozzleSlot        m_current;
    OnPick            m_on_pick;
};

NozzleEditMenu::NozzleEditMenu(wxWindow *parent) : CascadeMenuPopup(parent)
{
    // Parented on the owner rather than on this popup: a transient popup parented on another one goes
    // down with it.
    m_sub = new CascadeMenuPopup(parent);
    m_sub->Hide();
    m_sub->SetMainMenu(this);
    SetSubMenu(m_sub);
}

// A type the printer is not sold with at this diameter, or that no nozzle is made in, is left out
// rather than greyed: the user can do nothing about it.
std::vector<NozzleVolumeType> NozzleEditMenu::types_for(const std::string &diameter) const
{
    std::vector<NozzleVolumeType> types;
    for (NozzleVolumeType type : get_valid_nozzle_volume_type()) {
        if (type == nvtHybrid || !nozzle_variant_exists(diameter, type) || !printer_offers_flow(diameter, type, m_extruder))
            continue;
        types.push_back(type);
    }
    return types;
}

void NozzleEditMenu::open_type_menu(const std::string &diameter, const wxWindow *anchor)
{
    m_sub->SetSizer(nullptr); // the sizer goes before its items, so it never holds dead rows
    m_sub->DestroyChildren();
    auto *box = new wxBoxSizer(wxVERTICAL);

    bool first = true;
    for (NozzleVolumeType type : types_for(diameter)) {
        const bool checked = !m_current.empty() && m_current.diameter == diameter && m_current.volume_type == type;
        auto      *item    = new CascadeMenuItem(m_sub, volume_type_label(type), true, checked, false);
        item->Bind(wxEVT_BUTTON, [this, diameter, type](wxCommandEvent &) {
            OnPick on_pick = m_on_pick;
            close_menus();
            if (on_pick) on_pick(diameter, type);
        });
        CascadeMenu::AddItem(box, item, first);
        first = false;
    }
    CascadeMenu::Finish(m_sub, box);
    CascadeMenu::PositionSubMenu(m_sub, this, anchor);
    if (!m_sub->IsShown())
        m_sub->Popup();
    else
        m_sub->Refresh();
}

void NozzleEditMenu::build_menu()
{
    SetSizer(nullptr);
    DestroyChildren();
    auto *box = new wxBoxSizer(wxVERTICAL);

    bool first = true;
    for (const std::string &diameter : wxGetApp().preset_bundle->printers.diameters_of_selected_printer()) {
        const bool checked = !m_current.empty() && m_current.diameter == diameter;
        auto      *item    = new CascadeMenuItem(this, wxString::FromUTF8(diameter) + " mm", true, checked, true);
        auto       open    = [this, diameter, item] { open_type_menu(diameter, item); };
        item->Bind(wxEVT_ENTER_WINDOW, [open](wxMouseEvent &e) {
            open();
            e.Skip();
        });
        item->Bind(wxEVT_LEFT_DOWN, [open](wxMouseEvent &) { open(); });
        CascadeMenu::AddItem(box, item, first);
        first = false;
    }
    CascadeMenu::Finish(this, box);
}

void NozzleEditMenu::open(int extruder, const NozzleSlot &current, wxWindow *anchor, OnPick on_pick)
{
    m_extruder = extruder;
    m_current  = current;
    m_on_pick  = std::move(on_pick);
    build_menu();
    Position(anchor->GetScreenPosition() - wxPoint(FromDIP(CascadeMenu::Pad), 0), anchor->GetSize());
    Popup();
}

// This level refuses to close while the type menu is up, so release the guard, close that one first,
// then put the guard back for the next open.
void NozzleEditMenu::close_menus()
{
    SetSubMenu(nullptr);
    m_sub->Dismiss();
    Dismiss();
    SetSubMenu(m_sub);
}

DualNozzleView::DualNozzleView(wxWindow *parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
{
    SetBackgroundColour(parent->GetBackgroundColour());

    auto *outer = new wxBoxSizer(wxVERTICAL);
    create_picker(outer);

    // A grid keeps the two cards equal in width, even below their minimum.
    auto *cards = new wxGridSizer(1, EXTRUDERS, 0, FromDIP(CARD_GAP));
    for (int extruder = 0; extruder < EXTRUDERS; ++extruder) {
        m_cards[extruder] = new NozzleInfoCardView(this, [this, extruder](wxWindow *anchor) { open_edit_menu(extruder, anchor); });
        cards->Add(m_cards[extruder], 0, wxEXPAND);
    }
    outer->Add(cards, 0, wxEXPAND);
    SetSizer(outer);

    // Outside the sizer: Layout() places it over the gap between the cards.
    m_switcher_icon = new IconView(this, "fila_switch", SWITCHER_H);
    m_switcher_icon->SetCursor(wxCursor(wxCURSOR_HAND));
    m_switcher_icon->Hide();
    m_switcher_icon->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &evt) {
        show_filament_switcher_tip(m_switcher == Switcher::Ready);
        evt.Skip();
    });

    set_titles(_L("Left Nozzle"), _L("Right Nozzle"));
    wxGetApp().UpdateDarkUIWin(this);
}

void DualNozzleView::create_picker(wxBoxSizer *outer)
{
    m_picker = new wxBoxSizer(wxHORIZONTAL);
    for (size_t i = 0; i < m_picks.size(); ++i) {
        auto *button = new Button(this, wxEmptyString);
        button->SetFont(Label::Body_12);
        button->SetCornerRadius(FromDIP(4));
        button->SetPaddingSize(FromDIP(wxSize(8, 0)));
        button->SetMinSize(FromDIP(wxSize(PICK_W, PICK_H)));
        button->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent &) {
            if (m_on_pick) m_on_pick(int(i));
        });
        m_picker->Add(button, 0, i > 0 ? wxLEFT : 0, FromDIP(PICK_GAP));
        m_picks[i] = button;
    }
    outer->Add(m_picker, 0, wxBOTTOM, FromDIP(CARD_GAP));
}

void DualNozzleView::set_titles(const wxString &left, const wxString &right)
{
    const std::array<wxString, EXTRUDERS> titles{left, right};
    for (int extruder = 0; extruder < EXTRUDERS; ++extruder) {
        m_cards[extruder]->set_title(titles[extruder]);
        m_picks[extruder]->SetLabel(titles[extruder]);
    }
    Layout();
}

void DualNozzleView::refresh()
{
    const PresetBundle    &bundle    = *wxGetApp().preset_bundle;
    const NozzleInventory &inventory = bundle.nozzle_inventory;
    const bool             multi     = multi_nozzle_diameter_mode_enabled(bundle.project_config);

    const std::array<NozzleSlot, EXTRUDERS> nozzles{nozzle_of(0), nozzle_of(1)};
    // Nozzles of one diameter all print, so a side is picked only between two different diameters.
    const bool mixed = !nozzles[0].empty() && !nozzles[1].empty() && nozzles[0].diameter != nozzles[1].diameter;
    update_picker(!multi && mixed);

    using Usage = NozzleInfoCardView::Usage;
    for (int extruder = 0; extruder < EXTRUDERS; ++extruder) {
        const Usage usage = multi                      ? Usage::Unmarked :
                            inventory.in_use(extruder) ? Usage::InUse :
                                                         Usage::NotUsable;
        m_cards[extruder]->update(nozzles[extruder], usage);
    }

    Layout();
}

// Different nozzles cannot print one object together, so the picker selects the side that prints.
void DualNozzleView::update_picker(bool shown)
{
    GetSizer()->Show(m_picker, shown, true);
    if (!shown) return;

    const NozzleInventory &inventory = wxGetApp().preset_bundle->nozzle_inventory;
    for (int extruder = 0; extruder < EXTRUDERS; ++extruder)
        style_pick(m_picks[extruder], inventory.in_use(extruder));
}

void DualNozzleView::set_synced(int extruder, bool synced)
{
    if (extruder >= 0 && extruder < EXTRUDERS) m_cards[extruder]->set_synced(synced);
}

void DualNozzleView::set_switcher(Switcher switcher)
{
    if (m_switcher == switcher) return;
    m_switcher = switcher;
    if (switcher == Switcher::None) {
        m_switcher_icon->Hide();
        return;
    }
    m_switcher_icon->set_icon(switcher == Switcher::Ready ? "fila_switch" : "fila_switch_error");
    m_switcher_icon->SetToolTip(switcher == Switcher::Ready ? _L("Filament switcher") : _L("The filament switcher is not ready."));
    m_switcher_icon->Show();
    Layout();
}

bool DualNozzleView::Layout()
{
    const bool done = wxPanel::Layout();
    if (m_switcher_icon && m_switcher_icon->IsShown()) {
        // Centred on the gap between the cards, and on their height.
        const wxRect left  = m_cards[0]->GetRect();
        const wxRect right = m_cards[1]->GetRect();
        const wxSize icon  = m_switcher_icon->GetSize();
        const int    x     = (left.GetRight() + 1 + right.GetLeft()) / 2 - icon.x / 2;
        const int    y     = left.GetTop() + (left.GetHeight() - icon.y) / 2;
        m_switcher_icon->SetPosition(wxPoint(x, y));
        m_switcher_icon->Raise(); // over both cards; the cards clip siblings above them on MSW
    }
    return done;
}

void DualNozzleView::open_edit_menu(int extruder, wxWindow *anchor)
{
    if (!m_edit_menu) m_edit_menu = new NozzleEditMenu(this);
    // A pick can switch the printer preset, which rebuilds the sidebar, so it runs once the menu is gone.
    m_edit_menu->open(extruder, nozzle_of(extruder), anchor, [this, extruder](const std::string &diameter, NozzleVolumeType type) {
        CallAfter([this, extruder, diameter, type] {
            if (m_on_edit) m_on_edit(extruder, diameter, type);
        });
    });
}

void DualNozzleView::Rescale()
{
    for (Button *button : m_picks) {
        button->SetMinSize(FromDIP(wxSize(PICK_W, PICK_H)));
        button->Rescale();
    }
    // The icon reloads itself at the new scale; only its size is stale.
    m_switcher_icon->InvalidateBestSize();
    m_switcher_icon->SetSize(m_switcher_icon->GetBestSize());
    // A theme switch arrives here too. A hidden view redraws when the card shows it.
    if (IsShown()) refresh();
}

} // namespace Slic3r::GUI
