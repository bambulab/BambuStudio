#pragma once

#include "wx/wxprec.h"
#include "wx/aui/auibar.h"

#include "DebugToolDialog.hpp"
#include "SelectMachine.hpp"
#include "DeviceManager.hpp"

using namespace Slic3r::GUI;

class BBLTopbar : public wxAuiToolBar
{
public:
    BBLTopbar(wxFrame* parent);
    void UpdateToolbarWidth(int width);
    void OnIconize(wxCommandEvent& event);
    void OnFullScreen(wxCommandEvent& event);
    void OnCloseFrame(wxCommandEvent& event);
    void OnFileToolItem(wxAuiToolBarEvent& evt);
    void OnMouseLeftDClock(wxMouseEvent& mouse);
    void OnMouseLeftDown(wxMouseEvent& event);
    void OnMouseLeftUp(wxMouseEvent& event);
    void OnMouseMotion(wxMouseEvent& event);
    void OnMouseCaptureLost(wxMouseCaptureLostEvent& event);
    void OnMenuClose(wxMenuEvent& event);
    void OnOpenProject(wxCommandEvent& event);
    void OnSaveProject(wxCommandEvent& event);
    void OnUndo(wxCommandEvent& event);
    void OnRedo(wxCommandEvent& event);
    void OnAccountClicked(wxCommandEvent& event);
    void OnPrinterClicked(wxCommandEvent& event);
    wxAuiToolBarItem* FindToolByCurrentPosition();

    void SetFileMenu(wxMenu* file_menu);
    void AddDropDownSubMenu(wxMenu* sub_menu, const wxString& title);
    wxMenu *GetTopMenu();

private:
    wxFrame* m_frame;
    wxAuiToolBarItem* m_file_menu_item;
    wxPoint m_delta;
    wxMenu m_top_menu;
    wxMenu* m_file_menu;
    wxButton* m_account_btn;
    wxButton* m_printer_btn;
    int m_toolbar_h;
    bool m_skip_popup_file_menu;
    DebugToolDialog *m_debug_tool;
    std::shared_ptr<SelectMachinePopup> m_select_machine;
};
