#include "DeviceOperationsTooltip.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>

namespace Slic3r { namespace GUI {

static wxColour card_bg() { return StateColor::darkModeColorFor(ThemeColor::White); }
static wxColour title_fg() { return StateColor::darkModeColorFor(ThemeColor::TextPrimary); }
static wxColour row_fg() { return StateColor::darkModeColorFor(ThemeColor::TextSecondary); }

DeviceOperationsTooltip::DeviceOperationsTooltip(wxWindow *parent)
    : RoundedPopupWindow(parent, 8, true)
{
    m_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(m_sizer);
}

void DeviceOperationsTooltip::SetOperations(const std::vector<wxString> &operations)
{
    m_sizer->Clear(true);
    m_rows.clear();

    m_title = new wxStaticText(this, wxID_ANY, _L("Active operations"));
    m_title->SetFont(Label::Body_14);
    m_title->SetForegroundColour(title_fg());
    m_title->SetBackgroundColour(card_bg());
    m_title->Wrap(FromDIP(220));
    m_sizer->Add(m_title, 0, wxALL, FromDIP(12));

    for (const wxString &op : operations) {
        wxStaticText *row = new wxStaticText(this, wxID_ANY, wxString::FromUTF8("\xe2\x80\xa2 ") + op);
        row->SetFont(Label::Body_14);
        row->SetForegroundColour(row_fg());
        row->SetBackgroundColour(card_bg());
        row->Wrap(FromDIP(220));
        m_sizer->Add(row, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
        m_rows.push_back(row);
    }

    Finalize();
}

void DeviceOperationsTooltip::Dismiss()
{
}

}} // namespace Slic3r::GUI
