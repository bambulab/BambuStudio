#pragma once

#include <wx/panel.h>

class Label;
class Button;

wxDECLARE_EVENT(EVT_DEVICE_FILAMENT_HOTEND_TAB_CHANGED, wxCommandEvent);
wxDECLARE_EVENT(EVT_DEVICE_FILAMENT_HOTEND_BATCH_LOAD, wxCommandEvent);
wxDECLARE_EVENT(EVT_DEVICE_FILAMENT_HOTEND_BATCH_UNLOAD, wxCommandEvent);
wxDECLARE_EVENT(EVT_DEVICE_FILAMENT_HOTEND_BATCH_QUIT, wxCommandEvent);
wxDECLARE_EVENT(EVT_DEVICE_HOTEND_READ_ALL, wxCommandEvent);

namespace Slic3r::GUI
{

class wgtDeviceFilamentHotendTabHeader : public wxPanel
{
public:
    enum TabType
    {
        TabFilament = 0,
        TabHotend = 1
    };

    wgtDeviceFilamentHotendTabHeader(wxWindow* parent,
                                     wxWindowID id = wxID_ANY,
                                     const wxPoint& pos = wxDefaultPosition,
                                     const wxSize& size = wxDefaultSize,
                                     long style = wxTAB_TRAVERSAL);
    ~wgtDeviceFilamentHotendTabHeader() override = default;

    void SetSelectedTab(TabType tab);
    TabType GetSelectedTab() const { return m_selected_tab; }

    void ShowLoad(bool show, const wxString& tooltip = wxEmptyString);
    void ShowUnload(bool show, const wxString& tooltip = wxEmptyString);

    void EnterBatchMode();
    void QuitBatchMode();

private:
    void CreateGui();

    void NotifyTabChanged(TabType tab);
    void OnFilamentTabClick(wxMouseEvent& evt);
    void OnHotendTabClick(wxMouseEvent& evt);

    void OnBtnLoad(wxCommandEvent& evt);
    void OnBtnUnload(wxCommandEvent& evt);
    void OnBtnQuit(wxCommandEvent& evt);
    void OnBtnReadAll(wxCommandEvent& evt);

private:
    TabType m_selected_tab{ TabFilament };

    // filament
    wxPanel* m_tab_filament_panel{ nullptr };
    wxPanel* m_line_filament{ nullptr };
    Label* m_label_filament{ nullptr };

    Button* m_btn_fila_load{ nullptr };
    Button* m_btn_fila_unload{ nullptr };
    Button* m_btn_fila_quit{ nullptr };
    Button* m_btn_fila_dry{ nullptr };

    // hotend buttons
    Button* m_btn_hotend_read_all{ nullptr };

    // hotend tabs
    wxPanel* m_tab_hotend_panel{ nullptr };
    Label* m_label_hotend{ nullptr };
    wxPanel* m_line_hotend{ nullptr };
};

} // namespace Slic3r::GUI
