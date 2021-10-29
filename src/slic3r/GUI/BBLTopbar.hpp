#pragma once

#include "wx/wxprec.h"
#include "wx/aui/auibar.h"

#include "SelectMachine.hpp"
#include "DeviceManager.hpp"

using namespace Slic3r::GUI;

class BBLTopbar : public wxAuiToolBar
{
public:
    BBLTopbar(wxFrame* parent);
    void UpdateToolbarWidth(int width);
    void OnIconize(wxAuiToolBarEvent& event);
    void OnFullScreen(wxAuiToolBarEvent& event);
    void OnCloseFrame(wxAuiToolBarEvent& event);
    void OnFileToolItem(wxAuiToolBarEvent& evt);
    void OnMouseLeftDClock(wxMouseEvent& mouse);
    void OnMouseLeftDown(wxMouseEvent& event);
    void OnMouseLeftUp(wxMouseEvent& event);
    void OnMouseMotion(wxMouseEvent& event);
    void OnMouseCaptureLost(wxMouseCaptureLostEvent& event);
    void OnMenuClose(wxMenuEvent& event);
    void OnOpenProject(wxAuiToolBarEvent& event);
    void OnSaveProject(wxAuiToolBarEvent& event);
    void OnUndo(wxAuiToolBarEvent& event);
    void OnRedo(wxAuiToolBarEvent& event);
    void OnAccountClicked(wxAuiToolBarEvent& event);
    void OnPrinterClicked(wxAuiToolBarEvent& event);

    wxAuiToolBarItem* FindToolByCurrentPosition();

    void SetFileMenu(wxMenu* file_menu);
    void AddDropDownSubMenu(wxMenu* sub_menu, const wxString& title);
    wxMenu *GetTopMenu();
    void SetProjectName(wxString project_name);

private:
    wxFrame* m_frame;
    wxAuiToolBarItem* m_file_menu_item;
    wxPoint m_delta;
    wxMenu m_top_menu;
    wxMenu* m_file_menu;
    wxAuiToolBarItem* m_title_item;
    wxAuiToolBarItem* m_account_item;
    wxAuiToolBarItem* m_printer_item;
    int m_toolbar_h;
    bool m_skip_popup_file_menu;
    std::shared_ptr<SelectMachinePopup> m_select_machine;
};
