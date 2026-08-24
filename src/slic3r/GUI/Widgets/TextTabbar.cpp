#include "TextTabbar.hpp"

#include <wx/panel.h>

#include "Label.hpp"
#include "StateColor.hpp"

namespace Slic3r { namespace GUI {

TextTabbar::TextTabbar(wxWindow *parent, Align align) : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), m_align(align)
{
    SetBackgroundColour(*wxWHITE);
    auto *outer = new wxBoxSizer(wxVERTICAL);
    m_row       = new wxBoxSizer(wxHORIZONTAL);

    const int side_border = m_align == Align::Center ? FromDIP(48) : FromDIP(8);
    outer->Add(m_row, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, side_border));
    auto *line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    line->SetBackgroundColour(ThemeColor::Grey300);
    outer->Add(line, wxSizerFlags().Expand());

    SetSizer(outer);
}

void TextTabbar::ClearTabs()
{
    // Destroy the label/underline windows we own, then drop the (now dangling) sizer items.
    for (auto *w : m_labels) w->Destroy();
    for (auto *w : m_underlines) w->Destroy();
    m_labels.clear();
    m_underlines.clear();
    m_row->Clear(/*delete_windows*/ false);
    m_selection = -1;
    Layout();
}

void TextTabbar::AddTab(const wxString &label)
{
    const int index = (int) m_labels.size();

    auto *col  = new wxBoxSizer(wxVERTICAL);
    auto *text = new wxStaticText(this, wxID_ANY, label);
    text->SetFont(::Label::Body_14);

    auto *underline = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(2)));
    underline->SetBackgroundColour(this->GetBackgroundColour());

    auto on_click = [this, index](wxMouseEvent &) {
        SetSelection(index);
        wxCommandEvent evt(wxEVT_CHOICE, GetId());
        evt.SetEventObject(this);
        evt.SetInt(index);
        wxPostEvent(this, evt);
    };

    text->Bind(wxEVT_LEFT_DOWN, on_click);
    underline->Bind(wxEVT_LEFT_DOWN, on_click);
    text->Bind(wxEVT_ENTER_WINDOW, [text](wxMouseEvent &e) {
        text->SetCursor(wxCURSOR_HAND);
        e.Skip();
    });

    col->AddStretchSpacer();
    col->Add(text);
    col->AddStretchSpacer();
    col->Add(underline, 0, wxEXPAND);

    m_labels.push_back(text);
    m_underlines.push_back(underline);

    if (m_align == Align::Center) {
        // Even spread: a stretch spacer between every pair of tabs.
        if (m_row->GetItemCount() != 0) m_row->AddStretchSpacer();
        m_row->Add(col, wxSizerFlags().CenterHorizontal());
    } else {
        // Left-packed: fixed gap between tabs, trailing stretch pushes them all left.
        if (m_row->GetItemCount() != 0) m_row->AddSpacer(FromDIP(24));
        m_row->Add(col);
    }

    if (m_selection < 0) SetSelection(0);
}

void TextTabbar::SetSelection(int sel)
{
    if (sel < 0 || sel >= (int) m_labels.size()) return;

    m_selection = sel;
    render();
}

void TextTabbar::render()
{
    for (int i = 0; i < (int) m_labels.size(); ++i) {
        const bool active = (i == m_selection);
        m_labels[i]->SetFont(active ? Label::Head_14 : Label::Body_14);
        // render() re-runs on every SetSelection()/Rescale(), long after the one-shot UpdateDlgDarkUI
        // pass — so resolve the accent through darkModeColorFor here or the active underline reverts
        // to the light-mode green in dark mode.
        m_underlines[i]->SetBackgroundColour(active ? StateColor::darkModeColorFor(ThemeColor::BrandGreen) : GetBackgroundColour());
        m_underlines[i]->Refresh();
    }
    Layout();
    Refresh();
}

void TextTabbar::Rescale() { render(); }

}} // namespace Slic3r::GUI
