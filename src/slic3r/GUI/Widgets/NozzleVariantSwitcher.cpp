#include "NozzleVariantSwitcher.hpp"
#include "Label.hpp"
#include "StateColor.hpp"
#include "slic3r/GUI/I18N.hpp"

#include <wx/colour.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>
#include <wx/gdicmn.h>
#include <wx/sizer.h>
#include <wx/wrapsizer.h>

#include <algorithm>

namespace {
constexpr int StateNormal   = 0;
constexpr int StateChecked  = 1 << 0;
constexpr int StateModified = 1 << 1;

// Colours resolve per call, so they follow the current light/dark theme.
wxColour border_color(int state)
{
    return StateColor::darkModeColorFor((state & StateChecked) ? ThemeColor::BrandGreen : ThemeColor::Grey400);
}

wxColour text_color(int state)
{
    if (state & StateModified) return StateColor::darkModeColorFor(ThemeColor::Warning);
    return StateColor::darkModeColorFor((state & StateChecked) ? ThemeColor::BrandGreen : ThemeColor::TextPrimary);
}

wxColour bg_color(int state)
{
    return StateColor::darkModeColorFor((state & StateChecked) ? ThemeColor::LightGreen : ThemeColor::Grey300);
}

int make_state(bool selected, bool modified = false)
{
    int state = selected ? StateChecked : StateNormal;
    if (modified) state |= StateModified;
    return state;
}

struct FreezeGuard
{
    FreezeGuard(wxWindow *w) : w_(w) { w->Freeze(); }
    ~FreezeGuard() { w_->Thaw(); }

private:
    wxWindow *w_;
};

} // namespace

namespace Slic3r::GUI {

class NozzleLabel : public wxPanel
{
public:
    NozzleLabel(wxWindow *parent, std::string nozzle) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);

        m_label = new Label(this, ::Label::Body_11, wxString::Format(wxS("%s"), nozzle));

        const int pad_x = FromDIP(6);
        const int pad_y = FromDIP(2);
        auto     *sz    = new wxBoxSizer(wxHORIZONTAL);
        sz->Add(m_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, pad_x);
        sz->SetMinSize(m_label->GetBestSize().GetWidth() + pad_x * 2, m_label->GetBestSize().GetHeight() + pad_y * 2);
        SetSizer(sz);
        SetMinSize(sz->GetMinSize());

        Bind(wxEVT_PAINT, &NozzleLabel::on_paint, this);

        apply_state();
    }

    void SetState(int state)
    {
        if (m_state == state) return;

        m_state = state;
        apply_state();
    }

private:
    void on_paint(wxPaintEvent &)
    {
        wxPaintDC pdc(this);
        wxGCDC    dc(pdc);

        wxSize   size = GetSize();
        wxColour bg   = bg_color(m_state);

        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(bg));
        dc.DrawRoundedRectangle(0, 0, size.GetWidth(), size.GetHeight(), FromDIP(4));
    }

    void apply_state()
    {
        wxColour bg = bg_color(m_state);
        wxColour fg = text_color(m_state);
        m_label->SetBackgroundColour(bg);
        m_label->SetForegroundColour(fg);
        m_label->Refresh();
        Refresh();
    }

    Label *m_label    = nullptr;
    int    m_state    = StateNormal;
};

class NozzleVariantButton : public wxPanel
{
public:
    NozzleVariantButton(wxWindow *parent, const NozzleVariantSwitcher::Option &opt, bool show_diameter)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(parent->GetBackgroundColour());

        const wxString volume_label = _L(get_nozzle_volume_type_string(opt.volume_type));
        wxString       label_text   = show_diameter ? wxString::Format(wxS("%g %s"), opt.diameter_mm, volume_label) : volume_label;
        m_label             = new ::Label(this, ::Label::Head_13, label_text);

        const int pad_x   = FromDIP(12);
        const int pad_y   = FromDIP(8);
        const int seg_gap = FromDIP(12);
        const int idx_gap = FromDIP(4);
        auto     *row     = new wxBoxSizer(wxHORIZONTAL);
        row->AddSpacer(pad_x);
        row->Add(m_label, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, pad_y);

        if (!opt.nozzles.empty()) {
            row->AddSpacer(seg_gap);
            for (size_t i = 0; i < opt.nozzles.size(); ++i) {
                auto *chip = new NozzleLabel(this, opt.nozzles[i]);
                m_indices.push_back(chip);
                row->Add(chip, 0, wxALIGN_CENTER_VERTICAL);
                if (i + 1 < opt.nozzles.size()) row->AddSpacer(idx_gap);
            }
        }
        row->AddSpacer(pad_x);

        // SetSizer (not SetSizerAndFit). Use the sizer's computed minimum
        // as our min size — Fit() during construction can re-enter the
        // parent sizer machinery in fragile states.
        SetSizer(row);
        SetMinSize(row->GetMinSize());

        ApplyState();

        Bind(wxEVT_PAINT, &NozzleVariantButton::on_paint, this);
        // Walk the whole subtree (button, bold label, every IndexLabel and
        // its inner Label) so a click anywhere routes to OnClick and the
        // hand cursor shows across the whole button.
        std::vector<wxWindow *> queue{this};
        for (size_t i = 0; i < queue.size(); ++i) {
            wxWindow *w = queue[i];
            w->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { OnClick(); });
            w->SetCursor(wxCURSOR_HAND);
            for (auto *child : w->GetChildren()) queue.push_back(child);
        }
    }

    void OnClick()
    {
        wxCommandEvent ce(wxEVT_COMMAND_BUTTON_CLICKED, GetId());
        ce.SetEventObject(this);
        GetEventHandler()->ProcessEvent(ce);
    }

    void SetSelected(bool sel)
    {
        if (m_selected == sel) return;

        m_selected = sel;
        ApplyState();
    }

    bool IsSelected() const { return m_selected; }

    void SetModified(bool modified)
    {
        if (m_modified == modified) return;

        m_modified = modified;
        ApplyState();
    }

    void Rescale()
    {
        // Children's fonts come from Label::Body_NN (statics), nothing to
        // re-size at the font level. Just re-layout in case DPI changed.
        if (auto *sz = GetSizer()) {
            sz->Layout();
            SetMinSize(sz->GetMinSize());
        }
        Refresh();
    }

private:
    void ApplyState()
    {
        int      state = make_state(m_selected, m_modified);
        wxColour fg    = text_color(state);
        // Transparent button fill — paint the label on top of the parent's
        // background so antialiased glyph edges blend correctly.
        wxColour parent_bg = GetParent() ? GetParent()->GetBackgroundColour() : *wxWHITE;
        m_label->SetBackgroundColour(parent_bg);
        m_label->SetForegroundColour(fg);
        m_label->Refresh();
        for (auto *index : m_indices) index->SetState(state);
        Refresh();
    }

    void on_paint(wxPaintEvent &)
    {
        wxPaintDC pdc(this);
        wxGCDC    dc(pdc);

        wxSize    size     = GetSize();
        wxColour  border   = border_color(make_state(m_selected));
        const int border_w = FromDIP(2);
        const int radius   = FromDIP(10);

        dc.SetPen(wxPen(border, border_w));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        const double inset = border_w / 2.0;
        dc.DrawRoundedRectangle(inset, inset, size.GetWidth() - border_w, size.GetHeight() - border_w, radius);
    }

    Label                     *m_label = nullptr;
    std::vector<NozzleLabel *> m_indices;
    bool                       m_selected = false;
    bool                       m_modified = false;
};

NozzleVariantSwitcher::NozzleVariantSwitcher(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style)
    : wxPanel(parent, id, pos, size, style | wxTRANSPARENT_WINDOW)
{
    SetBackgroundStyle(wxBG_STYLE_ERASE);
    inherit_parent_background();

    SetSizer(new wxWrapSizer(wxHORIZONTAL, wxWRAPSIZER_DEFAULT_FLAGS));

    Bind(wxEVT_COMMAND_BUTTON_CLICKED, &NozzleVariantSwitcher::button_clicked, this);

    Bind(wxEVT_SIZE, [this](wxSizeEvent &evt) {
        Layout();
        Refresh();
        evt.Skip();
    });
}

void NozzleVariantSwitcher::inherit_parent_background()
{
    if (auto *p = GetParent()) SetBackgroundColour(p->GetBackgroundColour());
}

NozzleVariantSwitcher::~NozzleVariantSwitcher() { clear(); }

void NozzleVariantSwitcher::SetOptions(const std::vector<Option> &options, bool show_diameter)
{
    if (options == m_options && show_diameter == m_show_diameter) return;

    int prev_sel   = sel;
    int prev_count = int(btns.size());
    m_show_diameter = show_diameter;

    FreezeGuard fg(this);

    clear();
    for (size_t i = 0; i < options.size(); ++i) {
        const auto &opt = options[i];
        auto *btn = new NozzleVariantButton(this, opt, m_show_diameter);
        btns.push_back(btn);
        m_options.push_back(opt);
        GetSizer()->Add(btn, 0, wxRIGHT, FromDIP(i < (options.size() - 1) ? 8 : 0));
    }

    if (prev_count == int(btns.size()) && prev_sel >= 0 && prev_sel < int(btns.size())) {
        sel = prev_sel;
    } else {
        sel = -1;
    }
    update_button_styles();
    Layout();
}

void NozzleVariantSwitcher::clear()
{
    sel = -1;
    if (auto *sz = GetSizer()) sz->Clear(false);
    for (auto *btn : btns)
        if (btn) btn->Destroy();
    btns.clear();
    m_options.clear();
}

void NozzleVariantSwitcher::SetSelection(int index)
{
    if (index < 0 || index >= int(btns.size()) || index == sel) return;
    sel = index;
    update_button_styles();
    send_selection_event();
    Refresh();
}

NozzleVariantSwitcher::Option NozzleVariantSwitcher::GetOption(unsigned int index) const
{
    if (index >= m_options.size()) return Option{};
    return m_options[index];
}

void NozzleVariantSwitcher::SetButtonModified(unsigned int index, bool modified)
{
    if (index >= btns.size()) return;
    btns[index]->SetModified(modified);
}

void NozzleVariantSwitcher::Rescale()
{
    inherit_parent_background();
    for (auto *btn : btns) btn->Rescale();
    Layout();
}

void NozzleVariantSwitcher::button_clicked(wxCommandEvent &event)
{
    SetFocus();
    auto *src  = event.GetEventObject();
    auto  iter = std::find_if(btns.begin(), btns.end(), [src](auto *b) { return b == src; });
    SetSelection(iter == btns.end() ? -1 : int(iter - btns.begin()));
}

void NozzleVariantSwitcher::update_button_styles()
{
    for (int i = 0; i < int(btns.size()); ++i) { btns[i]->SetSelected(i == sel); }
}

bool NozzleVariantSwitcher::send_selection_event()
{
    wxCommandEvent evt(wxEVT_CHOICE, GetId());
    evt.SetEventObject(this);
    evt.SetInt(sel);
    GetEventHandler()->ProcessEvent(evt);
    return true;
}

} // namespace Slic3r::GUI
