#include "MappingRow.hpp"

#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/graphics.h>
#include <wx/sizer.h>

#include "FilamentView.hpp"
#include "IconView.hpp"
#include "PillView.hpp"
#include "slic3r/GUI/ExtruderHelpers.hpp"
#include "slic3r/GUI/GUI.hpp"     // from_u8
#include "slic3r/GUI/GUI_App.hpp" // preset bundle, for the filament name
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/DeviceCore/DevFilaSystem.h" // DevAmsTray::decode_color
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/ProjectTask.hpp" // FilamentInfo

namespace Slic3r::GUI {

namespace {
// Theme-adaptive palette (light values flip to their dark-map counterparts automatically).
wxColour c_box_fill() { return StateColor::darkModeColorFor(ThemeColor::White); }     // cell surface
wxColour c_box_border() { return StateColor::darkModeColorFor(ThemeColor::Grey400); } // cell / chip edge
wxColour c_muted() { return StateColor::darkModeColorFor(ThemeColor::TextMuted); }
wxColour c_ink() { return StateColor::darkModeColorFor(ThemeColor::TextPrimary); }
wxColour c_accent() { return StateColor::darkModeColorFor(ThemeColor::BrandGreen); }
wxColour c_warning() { return StateColor::darkModeColorFor(ThemeColor::Warning); } // mismatch flag

bool is_gradient(const FilamentInfo &f) { return f.ctype == 0 && f.colors.size() > 1; }

std::vector<wxColour> to_colours(const FilamentInfo &f)
{
    std::vector<wxColour> cols;
    if (f.colors.size() > 1)
        for (const auto &c : f.colors) cols.push_back(DevAmsTray::decode_color(c));
    else
        cols.push_back(DevAmsTray::decode_color(f.color));
    return cols;
}

/**
 * \brief Diameter + flow-type chip text, e.g. "0.4  Standard".
 *
 * \param f  the filament
 */
wxString flow_text(const FilamentInfo &f)
{
    const wxString dia = f.nozzle_diameter > 0 ? wxString::FromUTF8(get_diameter_string((float) f.nozzle_diameter).c_str())
                                               : wxString();
    const wxString vol = f.nozzle_volume_type.empty() ? wxString() : _L(f.nozzle_volume_type);
    if (dia.IsEmpty()) return vol;
    return vol.IsEmpty() ? dia : (dia + "  " + vol);
}

/**
 * \brief A filament's brand and name, shown side by side.
 */
struct FilamentLabel
{
    wxString brand;           ///< the vendor, e.g. "Generic"; empty when unknown
    bool     is_bambu{false}; ///< show the Bambu mark in place of \ref brand
    wxString name;            ///< e.g. "PLA Basic"; never repeats the brand
};

bool brand_is_bambu(const std::string &brand)
{
    return brand.find("Bambu") != std::string::npos || brand.find("bambu") != std::string::npos;
}

/**
 * \brief A preset name without its " @printer" suffix or leading brand, e.g. "Bambu PLA Basic @printer" -> "PLA Basic".
 *
 * \param preset_name  the full preset name
 * \param brand        the filament's vendor; shown on its own, so the name must not repeat it
 */
std::string clean_filament_name(const std::string &preset_name, const std::string &brand)
{
    std::string s  = preset_name;
    const auto  at = s.find(" @");
    if (at != std::string::npos) s = s.substr(0, at);
    // A name may lead with the whole vendor ("Generic PLA") or its first word ("Bambu Lab" -> "Bambu PLA").
    for (const std::string &prefix : {brand, brand.substr(0, brand.find(' '))}) {
        if (!prefix.empty() && s.size() > prefix.size() + 1 && s.rfind(prefix + " ", 0) == 0) return s.substr(prefix.size() + 1);
    }
    return s;
}

/**
 * \brief The compatible base preset carrying a filament id, or null. It is the same preset the sync writes.
 *
 * \param filament_id  the filament preset id
 */
const Preset *resolve_filament_preset(const std::string &filament_id)
{
    if (filament_id.empty()) return nullptr;
    auto &filaments = wxGetApp().preset_bundle->filaments;
    for (auto it = filaments.begin(); it != filaments.end(); ++it) {
        Preset &p = *it;
        if (p.is_compatible && filaments.get_preset_base(p) == &p && p.filament_id == filament_id) return &p;
    }
    return nullptr;
}

/**
 * \brief Split a filament into brand and name for display.
 *
 * The preset carrying the filament id supplies both, so they always agree. Without a preset the brand
 * comes from the filament and the name falls back to its translated material type.
 *
 * \param f  the filament
 */
FilamentLabel label_of(const FilamentInfo &f)
{
    std::string   vendor = f.brand;
    FilamentLabel label;
    label.name = f.type.empty() ? wxString() : _L(f.type);
    if (const Preset *p = resolve_filament_preset(f.filament_id)) {
        auto *v = dynamic_cast<const ConfigOptionStrings *>(p->config.option("filament_vendor"));
        if (v && !v->values.empty() && !v->values[0].empty()) vendor = v->values[0];
        label.name = from_u8(clean_filament_name(p->name, vendor));
    }
    // Bambu system filament ids start with "GF" when no vendor is present on the preset.
    label.is_bambu = brand_is_bambu(vendor) || f.filament_id.rfind("GF", 0) == 0;
    label.brand    = from_u8(vendor);
    return label;
}

/**
 * \brief Show a diameter / flow chip, flagged as a warning on a mismatch. Hidden when there is no text.
 *
 * \param pill  the chip
 * \param text  diameter / flow text
 * \param warn  true to flag a mismatch
 */
void show_flow_pill(PillView *pill, const wxString &text, bool warn)
{
    pill->set_text(text);
    pill->set_colors(warn ? c_warning() : c_muted(), wxTransparentColour, warn ? c_warning() : c_box_border());
    pill->Show(!text.empty());
}
} // namespace

// -------------------------------------------------------------------------------------------------
// A rounded, bordered cell that hosts its content and can show an accent selection outline. Children
// take the cell's fill as their background, so they blend onto it with no per-child theming.
// -------------------------------------------------------------------------------------------------
class CellBox : public wxWindow
{
public:
    explicit CellBox(wxWindow *parent) : wxWindow(parent, wxID_ANY)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(c_box_fill());
        SetMinSize(wxSize(-1, FromDIP(40)));
        m_content = new wxBoxSizer(wxHORIZONTAL);
        auto *pad = new wxBoxSizer(wxHORIZONTAL);
        pad->Add(m_content, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(8));
        SetSizer(pad);
        Bind(wxEVT_PAINT, &CellBox::on_paint, this);
        // The cell paints its own background and strokes the border at its edges. On a grow the
        // platform only repaints the newly exposed strip, so the border drawn at the old width
        // lingers; invalidate the whole surface on resize to clear it.
        Bind(wxEVT_SIZE, [this](wxSizeEvent &e) { Refresh(); e.Skip(); });
    }

    wxSizer *content() { return m_content; }
    void     set_selected(bool s)
    {
        if (m_selected == s) return;
        m_selected = s;
        Refresh();
    }

private:
    void on_paint(wxPaintEvent &)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour())); // corners = card colour
        dc.Clear();
        wxGCDC       gdc(dc);
        const wxSize sz = GetClientSize();
        wxRect       r(0, 0, sz.x, sz.y);
        r.Deflate(FromDIP(1)); // keep the stroke inside the widget
        gdc.SetBrush(wxBrush(c_box_fill()));
        gdc.SetPen(m_selected ? wxPen(c_accent(), FromDIP(1)) : wxPen(c_box_border(), FromDIP(1)));
        gdc.DrawRoundedRectangle(r, FromDIP(4));
    }

    wxBoxSizer *m_content{nullptr};
    bool        m_selected{false};
};

// -------------------------------------------------------------------------------------------------
// The dashed accent connector between source and destination.
// -------------------------------------------------------------------------------------------------
class DashedArrow : public wxWindow
{
public:
    explicit DashedArrow(wxWindow *parent) : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    {
        SetInitialSize(FromDIP(wxSize(40, 16)));
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &DashedArrow::on_paint, this);
    }

private:
    void on_paint(wxPaintEvent &)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
        dc.Clear();
        wxGCDC             gdc(dc);
        wxGraphicsContext *gc = gdc.GetGraphicsContext();
        if (!gc) return;
        const wxSize sz  = GetClientSize();
        const double cy  = sz.y / 2.0;
        const double x0  = FromDIP(2);
        const double x1  = sz.x - FromDIP(8); // line end / arrowhead base
        const double tip = sz.x - FromDIP(2);
        const double h   = FromDIP(4);

        wxPen dash(c_accent(), FromDIP(1));
        dash.SetStyle(wxPENSTYLE_SHORT_DASH);
        dash.SetCap(wxCAP_ROUND);
        gc->SetPen(dash);
        wxGraphicsPath line = gc->CreatePath();
        line.MoveToPoint(x0, cy);
        line.AddLineToPoint(x1, cy);
        gc->StrokePath(line);

        wxPen head(c_accent(), FromDIP(1));
        head.SetCap(wxCAP_ROUND);
        head.SetJoin(wxJOIN_ROUND);
        gc->SetPen(head);
        wxGraphicsPath arrow = gc->CreatePath();
        arrow.MoveToPoint(x1, cy - h);
        arrow.AddLineToPoint(tip, cy);
        arrow.AddLineToPoint(x1, cy + h);
        gc->StrokePath(arrow);
    }
};

// -------------------------------------------------------------------------------------------------
// A filament's brand: the Bambu mark, or the vendor name as text. Hides itself when there is neither.
// -------------------------------------------------------------------------------------------------
class BrandView : public wxWindow
{
public:
    explicit BrandView(wxWindow *parent)
        : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), m_mark(this, "bambu_mark", 12)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetFont(::Label::Body_13);
        Bind(wxEVT_PAINT, &BrandView::on_paint, this);
    }

    void set(const FilamentLabel &label)
    {
        m_bambu = label.is_bambu;
        m_brand = label.brand;
        Show(m_bambu || !m_brand.empty());
        InvalidateBestSize();
        Refresh();
    }

protected:
    wxSize DoGetBestClientSize() const override { return m_bambu ? m_mark.GetBmpSize() : GetTextExtent(m_brand); }

private:
    void on_paint(wxPaintEvent &)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
        dc.Clear();
        const wxSize sz = GetClientSize();
        if (m_bambu) {
            const wxSize bs = m_mark.GetBmpSize();
            if (m_mark.bmp().IsOk()) dc.DrawBitmap(m_mark.bmp(), (sz.x - bs.x) / 2, (sz.y - bs.y) / 2, true);
            return;
        }
        dc.SetFont(GetFont());
        dc.SetTextForeground(c_muted());
        const wxSize ts = dc.GetTextExtent(m_brand);
        dc.DrawText(m_brand, 0, (sz.y - ts.y) / 2);
    }

    ScalableBitmap m_mark;
    bool           m_bambu{false};
    wxString       m_brand;
};

// -------------------------------------------------------------------------------------------------
// MappingRow
// -------------------------------------------------------------------------------------------------
MappingRow::MappingRow(wxWindow *parent) : wxWindow(parent, wxID_ANY)
{
    SetBackgroundColour(parent->GetBackgroundColour()); // blend with the list/card

    auto make_name = [&](wxWindow *cell, const wxColour &fg) {
        auto *l = new ::Label(cell, wxEmptyString);
        l->SetFont(::Label::Body_13);
        l->SetForegroundColour(fg);
        l->SetBackgroundColour(c_box_fill());
        return l;
    };

    // --- source cell ---------------------------------------------------------------------------
    auto *src   = new CellBox(this);
    m_src_spool = new FilamentView(src, FilamentView::Style::Square);
    m_src_brand = new BrandView(src);
    m_src_name  = make_name(src, c_ink());
    m_src_size  = new PillView(src, c_box_fill());
    src->content()->Add(m_src_spool, 0, wxALIGN_CENTER_VERTICAL);
    src->content()->Add(m_src_brand, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    src->content()->Add(m_src_name, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    src->content()->AddStretchSpacer();
    src->content()->Add(m_src_size, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));

    // --- arrow ---------------------------------------------------------------------------------
    auto *arrow = new DashedArrow(this);

    // --- destination cell (the only interactive section) ---------------------------------------
    m_dst_box      = new CellBox(this);
    auto *chevron  = new IconView(m_dst_box, "drop_down", 16);
    m_dst_spool    = new FilamentView(m_dst_box, FilamentView::Style::Spool);
    m_dst_unmapped = make_name(m_dst_box, c_muted());
    m_dst_unmapped->SetLabel(_L("Unmapped"));
    m_dst_brand    = new BrandView(m_dst_box);
    m_dst_name     = make_name(m_dst_box, c_ink());
    m_dst_size     = new PillView(m_dst_box, c_box_fill());
    // spool and the "Unmapped" label share the slot right after the chevron (mutually exclusive), so
    // the destination content column starts at the same x whether mapped or not.
    m_dst_box->content()->Add(chevron, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    m_dst_box->content()->Add(m_dst_spool, 0, wxALIGN_CENTER_VERTICAL);
    m_dst_box->content()->Add(m_dst_unmapped, 0, wxALIGN_CENTER_VERTICAL);
    m_dst_box->content()->Add(m_dst_brand, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    m_dst_box->content()->Add(m_dst_name, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    m_dst_box->content()->AddStretchSpacer();
    m_dst_box->content()->Add(m_dst_size, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));

    auto *root = new wxBoxSizer(wxHORIZONTAL);
    root->Add(src, 1, wxALIGN_CENTER_VERTICAL);
    root->Add(arrow, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(8));
    root->Add(m_dst_box, 1, wxALIGN_CENTER_VERTICAL);
    SetSizer(root);

    for (wxWindow *w : {static_cast<wxWindow *>(chevron), static_cast<wxWindow *>(m_dst_spool),
                        static_cast<wxWindow *>(m_dst_unmapped), static_cast<wxWindow *>(m_dst_brand),
                        static_cast<wxWindow *>(m_dst_name), static_cast<wxWindow *>(m_dst_size),
                        static_cast<wxWindow *>(m_dst_box)})
        w->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) {
            if (on_dest_activate) on_dest_activate();
        });
}

void MappingRow::set_source(const FilamentInfo &f)
{
    m_src_spool->set_colors(to_colours(f), is_gradient(f));
    m_src_spool->set_label(wxString::Format("%d", f.id + 1)); // project filament index, shown 1-based
    const FilamentLabel label = label_of(f);
    m_src_brand->set(label);
    m_src_name->SetLabel(label.name);
    show_flow_pill(m_src_size, flow_text(f), false);
    m_src_diameter = f.nozzle_diameter;
    Layout();
}

void MappingRow::set_dest(const FilamentInfo &f, const wxString &slot_label)
{
    const bool mapped = f.tray_id >= 0;
    m_dst_spool->Show(mapped);
    m_dst_name->Show(mapped);
    m_dst_unmapped->Show(!mapped);
    if (mapped) {
        const FilamentLabel label = label_of(f);
        m_dst_spool->set_colors(to_colours(f), is_gradient(f));
        m_dst_spool->set_label(slot_label);
        m_dst_brand->set(label);
        m_dst_name->SetLabel(label.name);
        const bool mismatch = m_src_diameter > 0 && f.nozzle_diameter > 0 && !is_approx(m_src_diameter, f.nozzle_diameter);
        show_flow_pill(m_dst_size, flow_text(f), mismatch);
        m_dst_size->SetToolTip(mismatch ? _L("The current nozzle diameter is incompatible with the printer") : wxString());
    } else {
        m_dst_brand->Hide();
        m_dst_size->Hide();
    }
    Layout();
}

void MappingRow::set_selected(bool selected) { m_dst_box->set_selected(selected); }

wxWindow *MappingRow::dest_anchor() const { return m_dst_box; }

// -------------------------------------------------------------------------------------------------
MappingList::MappingList(wxWindow *parent)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL)
{
    SetScrollRate(0, FromDIP(12));
    SetSizer(new wxBoxSizer(wxVERTICAL));
}

MappingRow *MappingList::add_row()
{
    auto *row = new MappingRow(this);
    GetSizer()->Add(row, 0, wxEXPAND | wxTOP, FromDIP(8));
    return row;
}

void MappingList::clear() { GetSizer()->Clear(true); }

} // namespace Slic3r::GUI
