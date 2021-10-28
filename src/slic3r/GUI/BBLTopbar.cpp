#include "BBLTopbar.hpp"
#include "wx/artprov.h"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"
#include "wxExtensions.hpp"
#include "AccountManager.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"

using namespace Slic3r;

enum CUSTOM_ID
{
    ID_TOP_MENU_TOOL = 3100,
    ID_PRINTER,
    ID_ACCOUNT,
    ID_TOOL_BAR = 3200,
    ID_AMS_NOTEBOOK,
};

BBLTopbar::BBLTopbar(wxFrame* parent)
    : wxAuiToolBar(parent, ID_TOOL_BAR, wxDefaultPosition, wxDefaultSize, wxAUI_TB_TEXT | wxAUI_TB_HORZ_TEXT)
{
    m_frame = parent;
    m_skip_popup_file_menu = false;
    m_debug_tool = new DebugToolDialog(parent);

    wxInitAllImageHandlers();
    wxBitmap logo_bitmap = create_scaled_bitmap("logo", nullptr, FromDIP(21));
    wxStaticBitmap* logo_bitmap_ctrl = new wxStaticBitmap(this, wxID_ANY, logo_bitmap);
    this->AddControl(logo_bitmap_ctrl);

    wxAuiToolBarItem* file_tool = this->AddTool(ID_TOP_MENU_TOOL, "File",
        create_scaled_bitmap("top", nullptr, FromDIP(18)), wxEmptyString);
    this->SetToolDropDown(ID_TOP_MENU_TOOL, true);
    m_file_menu_item = file_tool;

    this->AddSeparator();

    wxBitmap open_bitmap = wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_OTHER, FromDIP(wxSize(18, 18)));
    wxBitmapButton* open_btn = new wxBitmapButton(this, wxID_OPEN, open_bitmap, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    this->AddControl(open_btn);

    wxBitmap save_bitmap = wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_OTHER, FromDIP(wxSize(18, 18)));
    wxBitmapButton* save_btn = new wxBitmapButton(this, wxID_SAVE, save_bitmap, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    this->AddControl(save_btn);

    wxBitmap undo_bitmap = wxArtProvider::GetBitmap(wxART_UNDO, wxART_OTHER, FromDIP(wxSize(18, 18)));
    wxBitmapButton* undo_btn = new wxBitmapButton(this, wxID_UNDO, undo_bitmap, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    this->AddControl(undo_btn);

    wxBitmap redo_bitmap = wxArtProvider::GetBitmap(wxART_REDO, wxART_OTHER, FromDIP(wxSize(18, 18)));
    wxBitmapButton* redo_btn = new wxBitmapButton(this, wxID_REDO, redo_bitmap, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    this->AddControl(redo_btn);

    this->AddSpacer(10);
    this->AddStretchSpacer(1);

    m_title_item = this->AddLabel(wxID_ANY, "", 300);
    m_title_item->SetAlignment(wxALIGN_CENTER);

    this->AddSpacer(10);
    this->AddStretchSpacer(1);

    wxBitmap account_bitmap = create_scaled_bitmap("account", nullptr, FromDIP(21));
    m_account_btn = new wxBitmapButton(this, ID_ACCOUNT, account_bitmap, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    this->AddControl(m_account_btn);

    this->AddSpacer(10);

    wxBitmap printer_bitmap = create_scaled_bitmap("fdm_printer", nullptr, FromDIP(21));
    m_printer_btn = new wxBitmapButton(this, ID_PRINTER, printer_bitmap, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    this->AddControl(m_printer_btn);

    this->AddSpacer(15);

    wxBitmap iconize_bitmap = wxArtProvider::GetBitmap(wxART_MINUS, wxART_OTHER, FromDIP(wxSize(18, 18)));
    wxButton* iconize_btn = new wxBitmapButton(this, wxID_ICONIZE_FRAME, iconize_bitmap);
    this->AddControl(iconize_btn);

    wxBitmap maximize_bitmap = wxArtProvider::GetBitmap(wxART_FULL_SCREEN, wxART_OTHER, FromDIP(wxSize(18, 18)));
    wxButton* maximize_btn = new wxBitmapButton(this, wxID_MAXIMIZE_FRAME, maximize_bitmap);
    this->AddControl(maximize_btn);

    wxBitmap close_bitmap = wxArtProvider::GetBitmap(wxART_CLOSE, wxART_OTHER, FromDIP(wxSize(18, 18)));
    wxButton* close_btn = new wxBitmapButton(this, wxID_CLOSE_FRAME, close_bitmap);
    this->AddControl(close_btn);

    Realize();
    m_toolbar_h = this->GetSize().GetHeight();

    int client_w = parent->GetClientSize().GetWidth();
    this->SetSize(client_w, m_toolbar_h);

    this->Bind(wxEVT_MOTION, &BBLTopbar::OnMouseMotion, this);
    this->Bind(wxEVT_MOUSE_CAPTURE_LOST, &BBLTopbar::OnMouseCaptureLost, this);
    this->Bind(wxEVT_MENU_CLOSE, &BBLTopbar::OnMenuClose, this);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnFileToolItem, this, ID_TOP_MENU_TOOL);
    this->Bind(wxEVT_BUTTON, &BBLTopbar::OnIconize, this, wxID_ICONIZE_FRAME);
    this->Bind(wxEVT_BUTTON, &BBLTopbar::OnFullScreen, this, wxID_MAXIMIZE_FRAME);
    this->Bind(wxEVT_BUTTON, &BBLTopbar::OnCloseFrame, this, wxID_CLOSE_FRAME);
    this->Bind(wxEVT_LEFT_DCLICK, &BBLTopbar::OnMouseLeftDClock, this);
    this->Bind(wxEVT_LEFT_DOWN, &BBLTopbar::OnMouseLeftDown, this);
    this->Bind(wxEVT_LEFT_UP, &BBLTopbar::OnMouseLeftUp, this);
    this->Bind(wxEVT_BUTTON, &BBLTopbar::OnOpenProject, this, wxID_OPEN);
    this->Bind(wxEVT_BUTTON, &BBLTopbar::OnSaveProject, this, wxID_SAVE);
    this->Bind(wxEVT_BUTTON, &BBLTopbar::OnRedo, this, wxID_REDO);
    this->Bind(wxEVT_BUTTON, &BBLTopbar::OnUndo, this, wxID_UNDO);
    this->Bind(wxEVT_BUTTON, &BBLTopbar::OnPrinterClicked, this, ID_PRINTER);
    this->Bind(wxEVT_BUTTON, &BBLTopbar::OnAccountClicked, this, ID_ACCOUNT);
}

void BBLTopbar::OnOpenProject(wxCommandEvent& event)
{
    MainFrame* main_frame = dynamic_cast<MainFrame*>(m_frame);
    Plater* plater = main_frame->plater();
    plater->load_project();
}

void BBLTopbar::OnSaveProject(wxCommandEvent& event)
{
    MainFrame* main_frame = dynamic_cast<MainFrame*>(m_frame);
    Plater* plater = main_frame->plater();
    plater->export_3mf(into_path(plater->get_project_filename(".3mf")));
}

void BBLTopbar::OnUndo(wxCommandEvent& event)
{
    MainFrame* main_frame = dynamic_cast<MainFrame*>(m_frame);
    Plater* plater = main_frame->plater();
    plater->undo();
}

void BBLTopbar::OnRedo(wxCommandEvent& event)
{
    MainFrame* main_frame = dynamic_cast<MainFrame*>(m_frame);
    Plater* plater = main_frame->plater();
    plater->redo();
}

void BBLTopbar::OnAccountClicked(wxCommandEvent& event)
{
    auto accountMenu = new wxMenu();

    append_menu_item(accountMenu, wxID_ANY, _L("Login"), _L("Login with your Account"),
        [](wxCommandEvent&) { Slic3r::GUI::login(); }, "upload_queue", nullptr, [this]() {
            Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
            return !account_manager->is_user_login();
        }, this);
    append_menu_item(accountMenu, wxID_ANY, _L("Logout"), _L(""),
        [](wxCommandEvent&) {
            Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
            account_manager->user_logout();
        }, "upload_queue", nullptr, [this]() {
            Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
            return account_manager->is_user_login();
        }, this);
    wxPoint btn_pos = m_account_btn->GetPosition();
    this->PopupMenu(accountMenu, btn_pos.x, btn_pos.y + this->GetSize().GetHeight() - 5);
}

void BBLTopbar::OnPrinterClicked(wxCommandEvent& event)
{
    m_select_machine = std::make_shared<SelectMachinePopup>(this, true);
    Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
    std::vector<MachineObject*> show_list;
    for (std::map<std::string, MachineObject*>::iterator it = account_manager->myBindMachineList.begin();
        it != account_manager->myBindMachineList.end(); it++) {
        show_list.push_back(it->second);
    }
    m_select_machine->update_machine_list(show_list);
    wxWindow* ctrl = (wxWindow*)event.GetEventObject();
    wxPoint pos = ctrl->ClientToScreen(wxPoint(0, 0));
    wxSize sz = ctrl->GetSize();
    m_select_machine->Position(pos, sz);
    m_select_machine->Popup();
}

void BBLTopbar::SetFileMenu(wxMenu* file_menu)
{
    m_file_menu = file_menu;
}

void BBLTopbar::AddDropDownSubMenu(wxMenu* sub_menu, const wxString& title)
{
    m_top_menu.AppendSubMenu(sub_menu, title);
}

wxMenu *BBLTopbar::GetTopMenu()
{
    return &m_top_menu;
}

void BBLTopbar::SetProjectName(wxString project_name)
{
    m_title_item->SetLabel(project_name);
}

void BBLTopbar::UpdateToolbarWidth(int width)
{
    this->SetSize(width, m_toolbar_h);
}

void BBLTopbar::OnIconize(wxCommandEvent& event)
{
    m_frame->Iconize();
}

void BBLTopbar::OnFullScreen(wxCommandEvent& event)
{
    if (m_frame->IsMaximized()) {
        m_frame->Restore();
    }
    else {
        m_frame->Maximize();
    }
}

void BBLTopbar::OnCloseFrame(wxCommandEvent& event)
{
    m_frame->Close();
}

void BBLTopbar::OnMouseLeftDClock(wxMouseEvent& mouse)
{
    // check whether mouse is not on any tool item
    if (this->FindToolByCurrentPosition() != NULL &&
        this->FindToolByCurrentPosition() != m_title_item)
        return;

    if (m_frame->IsMaximized()) {
        m_frame->Restore();
    }
    else {
        m_frame->Maximize();
    }
}

void BBLTopbar::OnFileToolItem(wxAuiToolBarEvent& evt)
{
    wxAuiToolBar* tb = static_cast<wxAuiToolBar*>(evt.GetEventObject());

    tb->SetToolSticky(evt.GetId(), true);

    if (evt.IsDropDownClicked()) {
        // line up our menu with the button
        wxRect rect = tb->GetToolRect(evt.GetId());
        wxPoint pt = tb->ClientToScreen(rect.GetBottomLeft());
        pt = ScreenToClient(pt);

        if (!m_skip_popup_file_menu) {
            PopupMenu(&m_top_menu, pt);
        }
        else {
            m_skip_popup_file_menu = false;
        }
    }
    else {
        if (!m_skip_popup_file_menu) {
            this->PopupMenu(m_file_menu, wxPoint(0, this->GetSize().GetHeight() - 2));
        }
        else {
            m_skip_popup_file_menu = false;
        }
    }

    // make sure the button is "un-stuck"
    tb->SetToolSticky(evt.GetId(), false);
}

void BBLTopbar::OnMouseLeftDown(wxMouseEvent& event)
{
    wxPoint mouse_pos = ::wxGetMousePosition();
    wxPoint frame_pos = m_frame->GetScreenPosition();
    m_delta = mouse_pos - frame_pos;

    if (FindToolByCurrentPosition() == NULL)
    {
        CaptureMouse();
    }

    event.Skip();
}

void BBLTopbar::OnMouseLeftUp(wxMouseEvent& event)
{
    if (HasCapture())
    {
        ReleaseMouse();
    }

    event.Skip();
}

void BBLTopbar::OnMouseMotion(wxMouseEvent& event)
{
    wxPoint mouse_pos = event.GetPosition();
    if (!HasCapture()) {
        //m_frame->OnMouseMotion(event);

        event.Skip();
        return;
    }

    if (event.Dragging() && event.LeftIsDown())
    {
        wxPoint mouse_pos = ::wxGetMousePosition();
        m_frame->Move(mouse_pos - m_delta);
    }
    event.Skip();
}

void BBLTopbar::OnMouseCaptureLost(wxMouseCaptureLostEvent& event)
{
}

void BBLTopbar::OnMenuClose(wxMenuEvent& event)
{
    wxAuiToolBarItem* item = this->FindToolByCurrentPosition();
    if (item == m_file_menu_item) {
        m_skip_popup_file_menu = true;
    }
}

wxAuiToolBarItem* BBLTopbar::FindToolByCurrentPosition()
{
    wxPoint mouse_pos = ::wxGetMousePosition();
    wxPoint client_pos = this->ScreenToClient(mouse_pos);
    return this->FindToolByPosition(client_pos.x, client_pos.y);
}
