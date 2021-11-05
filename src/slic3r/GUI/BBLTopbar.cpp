#include "BBLTopbar.hpp"
#include "wx/artprov.h"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"
#include "wxExtensions.hpp"
#include "AccountManager.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "WebViewDialog.hpp"


using namespace Slic3r;

enum CUSTOM_ID
{
    ID_TOP_MENU_TOOL = 3100,
    ID_TOP_FILE_MENU,
    ID_TOP_DROPDOWN_MENU,
    ID_PRINTER,
    ID_ACCOUNT,
    ID_MODEL_STORE,
    ID_TOOL_BAR = 3200,
    ID_AMS_NOTEBOOK,
};

class BBLTopbarArt : public wxAuiDefaultToolBarArt
{
public:
    virtual void DrawLabel(wxDC& dc, wxWindow* wnd, const wxAuiToolBarItem& item, const wxRect& rect) wxOVERRIDE;
};

void BBLTopbarArt::DrawLabel(wxDC& dc, wxWindow* wnd, const wxAuiToolBarItem& item, const wxRect& rect)
{
    dc.SetFont(m_font);
    dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT));

    int textWidth = 0, textHeight = 0;
    dc.GetTextExtent(item.GetLabel(), &textWidth, &textHeight);

    wxRect clipRect = rect;
    clipRect.width -= 1;
    dc.SetClippingRegion(clipRect);

    int textX, textY;
    if (textWidth < rect.GetWidth()) {
        textX = rect.x + 1 + (rect.width - textWidth) / 2;
    }
    else {
        textX = rect.x + 1;
    }
    textY = rect.y + (rect.height - textHeight) / 2;
    dc.DrawText(item.GetLabel(), textX, textY);
    dc.DestroyClippingRegion();
}

BBLTopbar::BBLTopbar(wxFrame* parent)
    : wxAuiToolBar(parent, ID_TOOL_BAR, wxDefaultPosition, wxDefaultSize, wxAUI_TB_TEXT | wxAUI_TB_HORZ_TEXT)
{
    SetArtProvider(new BBLTopbarArt());

    m_frame = parent;
    m_skip_popup_file_menu = false;
    m_skip_popup_dropdown_menu = false;

    wxInitAllImageHandlers();
    wxBitmap logo_bitmap = create_scaled_bitmap("logo", nullptr, FromDIP(21));
    wxAuiToolBarItem* logo_item = this->AddTool(wxID_ANY, "", logo_bitmap);
    logo_item->SetActive(false);

    m_file_menu_item = this->AddTool(ID_TOP_FILE_MENU, "File",
        wxBitmap(), wxEmptyString);

    m_dropdown_menu_item = this->AddTool(ID_TOP_DROPDOWN_MENU, "",
        create_scaled_bitmap("expand", nullptr, FromDIP(18)), wxEmptyString);

    this->AddSeparator();

    wxBitmap open_bitmap = wxArtProvider::GetBitmap(wxART_FILE_OPEN, wxART_OTHER, FromDIP(wxSize(18, 18)));
    wxAuiToolBarItem* tool_item = this->AddTool(wxID_OPEN, "", open_bitmap);

    wxBitmap save_bitmap = wxArtProvider::GetBitmap(wxART_FILE_SAVE, wxART_OTHER, FromDIP(wxSize(18, 18)));
    wxAuiToolBarItem* save_btn = this->AddTool(wxID_SAVE, "", save_bitmap);

    wxBitmap undo_bitmap = wxArtProvider::GetBitmap(wxART_UNDO, wxART_OTHER, FromDIP(wxSize(18, 18)));
    m_undo_item = this->AddTool(wxID_UNDO, "", undo_bitmap);

    wxBitmap redo_bitmap = wxArtProvider::GetBitmap(wxART_REDO, wxART_OTHER, FromDIP(wxSize(18, 18)));
    m_redo_item = this->AddTool(wxID_REDO, "", redo_bitmap);

    this->AddSpacer(10);
    this->AddStretchSpacer(1);

    m_title_item = this->AddLabel(wxID_ANY, "", 300);
    m_title_item->SetAlignment(wxALIGN_CENTER);

    this->AddSpacer(10);
    this->AddStretchSpacer(1);

    wxBitmap model_store_bitmap = create_scaled_bitmap("model_store", nullptr, FromDIP(21));
    m_model_store_item = this->AddTool(ID_MODEL_STORE, "", model_store_bitmap);
    this->AddSpacer(10);

    wxBitmap account_bitmap = create_scaled_bitmap("account", nullptr, FromDIP(21));
    m_account_item = this->AddTool(ID_ACCOUNT, "", account_bitmap);

    this->AddSpacer(10);

    wxBitmap printer_bitmap = create_scaled_bitmap("fdm_printer", nullptr, FromDIP(21));
    m_printer_item = this->AddTool(ID_PRINTER, "", printer_bitmap);

    this->AddSpacer(15);

    wxBitmap iconize_bitmap = wxArtProvider::GetBitmap(wxART_MINUS, wxART_OTHER, FromDIP(wxSize(18, 18)));
    wxAuiToolBarItem* iconize_btn = this->AddTool(wxID_ICONIZE_FRAME, "", iconize_bitmap);

    wxBitmap maximize_bitmap = wxArtProvider::GetBitmap(wxART_FULL_SCREEN, wxART_OTHER, FromDIP(wxSize(18, 18)));
    wxAuiToolBarItem* maximize_btn = this->AddTool(wxID_MAXIMIZE_FRAME, "", maximize_bitmap);

    wxBitmap close_bitmap = wxArtProvider::GetBitmap(wxART_CLOSE, wxART_OTHER, FromDIP(wxSize(18, 18)));
    wxAuiToolBarItem* close_btn = this->AddTool(wxID_CLOSE_FRAME, "", close_bitmap);

    Realize();
    m_toolbar_h = this->GetSize().GetHeight();

    int client_w = parent->GetClientSize().GetWidth();
    this->SetSize(client_w, m_toolbar_h);

    this->Bind(wxEVT_MOTION, &BBLTopbar::OnMouseMotion, this);
    this->Bind(wxEVT_MOUSE_CAPTURE_LOST, &BBLTopbar::OnMouseCaptureLost, this);
    this->Bind(wxEVT_MENU_CLOSE, &BBLTopbar::OnMenuClose, this);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnFileToolItem, this, ID_TOP_FILE_MENU);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnDropdownToolItem, this, ID_TOP_DROPDOWN_MENU);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnIconize, this, wxID_ICONIZE_FRAME);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnFullScreen, this, wxID_MAXIMIZE_FRAME);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnCloseFrame, this, wxID_CLOSE_FRAME);
    this->Bind(wxEVT_LEFT_DCLICK, &BBLTopbar::OnMouseLeftDClock, this);
    this->Bind(wxEVT_LEFT_DOWN, &BBLTopbar::OnMouseLeftDown, this);
    this->Bind(wxEVT_LEFT_UP, &BBLTopbar::OnMouseLeftUp, this);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnOpenProject, this, wxID_OPEN);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnSaveProject, this, wxID_SAVE);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnRedo, this, wxID_REDO);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnUndo, this, wxID_UNDO);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnPrinterClicked, this, ID_PRINTER);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnAccountClicked, this, ID_ACCOUNT);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnModelStoreClicked, this, ID_MODEL_STORE);
}

void BBLTopbar::OnOpenProject(wxAuiToolBarEvent& event)
{
    MainFrame* main_frame = dynamic_cast<MainFrame*>(m_frame);
    Plater* plater = main_frame->plater();
    plater->load_project();
}

void BBLTopbar::OnSaveProject(wxAuiToolBarEvent& event)
{
    MainFrame* main_frame = dynamic_cast<MainFrame*>(m_frame);
    Plater* plater = main_frame->plater();
    plater->export_3mf(into_path(plater->get_project_filename(".3mf")));
}

void BBLTopbar::OnUndo(wxAuiToolBarEvent& event)
{
    MainFrame* main_frame = dynamic_cast<MainFrame*>(m_frame);
    Plater* plater = main_frame->plater();
    plater->undo();
}

void BBLTopbar::OnRedo(wxAuiToolBarEvent& event)
{
    MainFrame* main_frame = dynamic_cast<MainFrame*>(m_frame);
    Plater* plater = main_frame->plater();
    plater->redo();
}

void BBLTopbar::EnableUndoRedoItems()
{
    this->EnableTool(m_undo_item->GetId(), true);
    this->EnableTool(m_redo_item->GetId(), true);
    Refresh();
}

void BBLTopbar::DisableUndoRedoItems()
{
    this->EnableTool(m_undo_item->GetId(), false);
    this->EnableTool(m_redo_item->GetId(), false);
    Refresh();
}

void BBLTopbar::OnAccountClicked(wxAuiToolBarEvent& event)
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
    append_menu_item(accountMenu, wxID_ANY, _L("My Project List"), _L(""),
        [this](wxCommandEvent&) {
            WebFrame* m_my_projects_webframe = new WebFrame(MY_PROJECT_LIST_URL);
            m_my_projects_webframe->Show();
        }, "upload_queue", nullptr,
        [this] (){
            return true;
            },
        this);
    append_menu_item(accountMenu, wxID_ANY, _L("My Collections"), _L(""),
        [this](wxCommandEvent&) {
            WebFrame* m_my_collections_webframe = new WebFrame(MY_COLLECTIONS_URL);
            m_my_collections_webframe->Show();
        }, "upload_queue", nullptr,
        [this] (){
            return true;
            },
        this);
    append_menu_item(accountMenu, wxID_ANY, _L("Upload/Publish Model"), _L(""),
        [this](wxCommandEvent&) {
            /* upload project first and publish */
            Slic3r::AccountManager* c = Slic3r::GUI::wxGetApp().getAccountManager();
            BBLProject* project = c->get_default_project();
            if (!project) {
                //TODO check uploaded?
                MainFrame* main_frame = dynamic_cast<MainFrame*>(m_frame);
                Plater* plater = main_frame->plater();
                plater->upload_3mf();
                project = c->get_default_project();
                if (!project) return;
            }
            if (project->project_model_id.empty()) return;

            wxString url = wxString::Format(MY_MODEL_PUBLISH_URL_FORMAT, project->project_model_id);
            WebFrame* m_publish_webframe = new WebFrame(url);
            m_publish_webframe->Show();
        }, "upload_queue", nullptr,
        [this] (){
            return true;
            },
        this);

    wxRect rect = this->GetToolRect(m_account_item->GetId());
    this->PopupMenu(accountMenu, rect.x, rect.y + this->GetSize().GetHeight() - 5);
}

void BBLTopbar::OnPrinterClicked(wxAuiToolBarEvent& event)
{
    m_select_machine = std::make_shared<SelectMachinePopup>(this, true);

    wxRect rect = this->GetToolRect(m_printer_item->GetId());
    wxPoint pos = this->ClientToScreen(wxPoint(rect.x, rect.y));
    pos.y += rect.height;

    m_select_machine->Position(pos, wxSize(0, 0));
    m_select_machine->Popup();
}

void BBLTopbar::OnModelStoreClicked(wxAuiToolBarEvent& event)
{
    WebFrame* m_model_store_webframe = new WebFrame(MODEL_STORE_URL);
    m_model_store_webframe->Show();
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
    m_title_item->SetAlignment(wxALIGN_CENTRE_HORIZONTAL);
    this->Refresh();
}

void BBLTopbar::UpdateToolbarWidth(int width)
{
    this->SetSize(width, m_toolbar_h);
}

void BBLTopbar::OnIconize(wxAuiToolBarEvent& event)
{
    m_frame->Iconize();
}

void BBLTopbar::OnFullScreen(wxAuiToolBarEvent& event)
{
    if (m_frame->IsMaximized()) {
        m_frame->Restore();
    }
    else {
        m_frame->Maximize();
    }
}

void BBLTopbar::OnCloseFrame(wxAuiToolBarEvent& event)
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

    if (!m_skip_popup_file_menu) {
        this->PopupMenu(m_file_menu, wxPoint(0, this->GetSize().GetHeight() - 2));
    }
    else {
        m_skip_popup_file_menu = false;
    }

    // make sure the button is "un-stuck"
    tb->SetToolSticky(evt.GetId(), false);
}

void BBLTopbar::OnDropdownToolItem(wxAuiToolBarEvent& evt)
{
    wxAuiToolBar* tb = static_cast<wxAuiToolBar*>(evt.GetEventObject());

    tb->SetToolSticky(evt.GetId(), true);

    if (!m_skip_popup_dropdown_menu) {
        PopupMenu(&m_top_menu, wxPoint(0, this->GetSize().GetHeight() - 2));
    }
    else {
        m_skip_popup_dropdown_menu = false;
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
    else if (item == m_dropdown_menu_item) {
        m_skip_popup_dropdown_menu = true;
    }
}

wxAuiToolBarItem* BBLTopbar::FindToolByCurrentPosition()
{
    wxPoint mouse_pos = ::wxGetMousePosition();
    wxPoint client_pos = this->ScreenToClient(mouse_pos);
    return this->FindToolByPosition(client_pos.x, client_pos.y);
}
