#pragma once

#include <wx/panel.h>
#include <wx/string.h>

class Label;

namespace Slic3r::GUI
{

class wgtDeviceAmsDryIcon : public wxPanel
{
public:
    explicit wgtDeviceAmsDryIcon(wxWindow* parent);
    ~wgtDeviceAmsDryIcon() override = default;

    void ClearSummary();
    void SetSummary(const wxString& main_label, const wxString& main_percent, const wxString& other_label, const wxString& other_percent);

private:
    void CreateGui();

private:
    Label* m_label_main{ nullptr };
    Label* m_label_main_percent{ nullptr };
    Label* m_label_other{ nullptr };
    Label* m_label_other_percent{ nullptr };
    wxPanel* m_panel_main_dot{ nullptr };
    wxPanel* m_panel_other_dot{ nullptr };
};

} // namespace Slic3r::GUI
