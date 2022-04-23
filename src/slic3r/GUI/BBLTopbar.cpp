#include "BBLTopbar.hpp"
#include "wx/artprov.h"
#include "wx/aui/framemanager.h"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"
#include "wxExtensions.hpp"
#include "AccountManager.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "WebViewDialog.hpp"
#include "PartPlate.hpp"

#define TOPBAR_ICON_SIZE 18

using namespace Slic3r;

enum CUSTOM_ID
{
    ID_TOP_MENU_TOOL = 3100,
    ID_LOGO,
    ID_TOP_FILE_MENU,
    ID_TOP_DROPDOWN_MENU,
    ID_TITLE,
    ID_ACCOUNT,
    ID_MODEL_STORE,
    ID_PUBLISH,
    ID_TOOL_BAR = 3200,
    ID_AMS_NOTEBOOK,
};

class BBLTopbarArt : public wxAuiDefaultToolBarArt
{
public:
    virtual void DrawLabel(wxDC& dc, wxWindow* wnd, const wxAuiToolBarItem& item, const wxRect& rect) wxOVERRIDE;
    virtual void DrawBackground(wxDC& dc, wxWindow* wnd, const wxRect& rect) wxOVERRIDE;
    virtual void DrawButton(wxDC& dc, wxWindow* wnd, const wxAuiToolBarItem& item, const wxRect& rect) wxOVERRIDE;
};

void BBLTopbarArt::DrawLabel(wxDC& dc, wxWindow* wnd, const wxAuiToolBarItem& item, const wxRect& rect)
{
    dc.SetFont(m_font);
    dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));

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

void BBLTopbarArt::DrawBackground(wxDC& dc, wxWindow* wnd, const wxRect& rect)
{
    dc.SetBrush(wxBrush(wxColour(38, 46, 48)));
    wxRect clipRect = rect;
    clipRect.y -= 8;
    clipRect.height += 8;
    dc.SetClippingRegion(clipRect);
    dc.DrawRectangle(rect);
    dc.DestroyClippingRegion();
}

void BBLTopbarArt::DrawButton(wxDC& dc, wxWindow* wnd, const wxAuiToolBarItem& item, const wxRect& rect)
{
    int textWidth = 0, textHeight = 0;

    if (m_flags & wxAUI_TB_TEXT)
    {
        dc.SetFont(m_font);
        int tx, ty;

        dc.GetTextExtent(wxT("ABCDHgj"), &tx, &textHeight);
        textWidth = 0;
        dc.GetTextExtent(item.GetLabel(), &textWidth, &ty);
    }

    int bmpX = 0, bmpY = 0;
    int textX = 0, textY = 0;

    const wxBitmap& bmp = item.GetState() & wxAUI_BUTTON_STATE_DISABLED
        ? item.GetDisabledBitmap()
        : item.GetBitmap();

    const wxSize bmpSize = bmp.IsOk() ? bmp.GetScaledSize() : wxSize(0, 0);

    if (m_textOrientation == wxAUI_TBTOOL_TEXT_BOTTOM)
    {
        bmpX = rect.x +
            (rect.width / 2) -
            (bmpSize.x / 2);

        bmpY = rect.y +
            ((rect.height - textHeight) / 2) -
            (bmpSize.y / 2);

        textX = rect.x + (rect.width / 2) - (textWidth / 2) + 1;
        textY = rect.y + rect.height - textHeight - 1;
    }
    else if (m_textOrientation == wxAUI_TBTOOL_TEXT_RIGHT)
    {
        bmpX = rect.x + wnd->FromDIP(3);

        bmpY = rect.y +
            (rect.height / 2) -
            (bmpSize.y / 2);

        textX = bmpX + wnd->FromDIP(3) + bmpSize.x;
        textY = rect.y +
            (rect.height / 2) -
            (textHeight / 2);
    }


    if (!(item.GetState() & wxAUI_BUTTON_STATE_DISABLED))
    {
        if (item.GetState() & wxAUI_BUTTON_STATE_PRESSED)
        {
            dc.SetPen(wxPen(m_highlightColour));
            dc.SetBrush(wxBrush(m_highlightColour.ChangeLightness(20)));
            dc.DrawRectangle(rect);
        }
        else if ((item.GetState() & wxAUI_BUTTON_STATE_HOVER) || item.IsSticky())
        {
            dc.SetPen(wxPen(m_highlightColour));
            dc.SetBrush(wxBrush(m_highlightColour.ChangeLightness(40)));

            // draw an even lighter background for checked item hovers (since
            // the hover background is the same color as the check background)
            if (item.GetState() & wxAUI_BUTTON_STATE_CHECKED)
                dc.SetBrush(wxBrush(m_highlightColour.ChangeLightness(50)));

            dc.DrawRectangle(rect);
        }
        else if (item.GetState() & wxAUI_BUTTON_STATE_CHECKED)
        {
            // it's important to put this code in an else statement after the
            // hover, otherwise hovers won't draw properly for checked items
            dc.SetPen(wxPen(m_highlightColour));
            dc.SetBrush(wxBrush(m_highlightColour.ChangeLightness(40)));
            dc.DrawRectangle(rect);
        }
    }

    if (bmp.IsOk())
        dc.DrawBitmap(bmp, bmpX, bmpY, true);

    // set the item's text color based on if it is disabled
    dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));
    if (item.GetState() & wxAUI_BUTTON_STATE_DISABLED)
    {
        dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    }

    if ((m_flags & wxAUI_TB_TEXT) && !item.GetLabel().empty())
    {
        dc.DrawText(item.GetLabel(), textX, textY);
    }
}

BBLTopbar::BBLTopbar(wxFrame* parent)
    : wxAuiToolBar(parent, ID_TOOL_BAR, wxDefaultPosition, wxDefaultSize, wxAUI_TB_TEXT | wxAUI_TB_HORZ_TEXT)
{
    SetArtProvider(new BBLTopbarArt());

    m_frame = parent;
    m_skip_popup_file_menu = false;
    m_skip_popup_dropdown_menu = false;

    wxInitAllImageHandlers();

    this->AddSpacer(5);

    wxBitmap logo_bitmap = create_scaled_bitmap("topbar_logo", nullptr, TOPBAR_ICON_SIZE);
    wxAuiToolBarItem* logo_item = this->AddTool(ID_LOGO, "", logo_bitmap);
    logo_item->SetHoverBitmap(logo_bitmap);
    logo_item->SetActive(false);

    wxBitmap file_bitmap = create_scaled_bitmap("topbar_file", nullptr, TOPBAR_ICON_SIZE);
    m_file_menu_item = this->AddTool(ID_TOP_FILE_MENU, "File", file_bitmap, wxEmptyString);

    this->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));

    this->AddSpacer(5);

    wxBitmap dropdown_bitmap = create_scaled_bitmap("topbar_dropdown", nullptr, TOPBAR_ICON_SIZE);
    m_dropdown_menu_item = this->AddTool(ID_TOP_DROPDOWN_MENU, "",
        dropdown_bitmap, wxEmptyString);

    this->AddSpacer(5);
    this->AddSeparator();
    this->AddSpacer(5);

    wxBitmap open_bitmap = create_scaled_bitmap("topbar_open", nullptr, TOPBAR_ICON_SIZE);
    wxAuiToolBarItem* tool_item = this->AddTool(wxID_OPEN, "", open_bitmap);

    this->AddSpacer(10);

    wxBitmap save_bitmap = create_scaled_bitmap("topbar_save", nullptr, TOPBAR_ICON_SIZE);
    wxAuiToolBarItem* save_btn = this->AddTool(wxID_SAVE, "", save_bitmap);

    this->AddSpacer(10);

    wxBitmap undo_bitmap = create_scaled_bitmap("topbar_undo", nullptr, TOPBAR_ICON_SIZE);
    m_undo_item = this->AddTool(wxID_UNDO, "", undo_bitmap);
    wxBitmap undo_inactive_bitmap = create_scaled_bitmap("topbar_undo_inactive", nullptr, TOPBAR_ICON_SIZE);
    m_undo_item->SetDisabledBitmap(undo_inactive_bitmap);

    this->AddSpacer(10);

    wxBitmap redo_bitmap = create_scaled_bitmap("topbar_redo", nullptr, TOPBAR_ICON_SIZE);
    m_redo_item = this->AddTool(wxID_REDO, "", redo_bitmap);
    wxBitmap redo_inactive_bitmap = create_scaled_bitmap("topbar_redo_inactive", nullptr, TOPBAR_ICON_SIZE);
    m_redo_item->SetDisabledBitmap(redo_inactive_bitmap);

    this->AddSpacer(10);
    this->AddStretchSpacer(1);

    m_title_item = this->AddLabel(ID_TITLE, "", 300);
    m_title_item->SetAlignment(wxALIGN_CENTER);

    this->AddSpacer(10);
    this->AddStretchSpacer(1);


    wxBitmap m_publish_bitmap = create_scaled_bitmap("topbar_publish", nullptr, TOPBAR_ICON_SIZE);
    m_publish_item            = this->AddTool(ID_PUBLISH, "", m_publish_bitmap);
    wxBitmap m_publish_disable_bitmap = create_scaled_bitmap("topbar_publish_disable", nullptr, TOPBAR_ICON_SIZE);
    m_publish_item->SetDisabledBitmap(m_publish_disable_bitmap);
    this->AddSpacer(12);

    /*wxBitmap model_store_bitmap = create_scaled_bitmap("topbar_store", nullptr, TOPBAR_ICON_SIZE);
    m_model_store_item = this->AddTool(ID_MODEL_STORE, "", model_store_bitmap);
    this->AddSpacer(12);

    wxBitmap account_bitmap = create_scaled_bitmap("topbar_account", nullptr, TOPBAR_ICON_SIZE);
    m_account_item = this->AddTool(ID_ACCOUNT, "", account_bitmap);

    this->AddSpacer(12);
    */

    this->AddSeparator();
    this->AddSpacer(6);

    wxBitmap iconize_bitmap = create_scaled_bitmap("topbar_min", nullptr, TOPBAR_ICON_SIZE);
    wxAuiToolBarItem* iconize_btn = this->AddTool(wxID_ICONIZE_FRAME, "", iconize_bitmap);

    this->AddSpacer(6);

    maximize_bitmap = create_scaled_bitmap("topbar_max", nullptr, TOPBAR_ICON_SIZE);
    window_bitmap = create_scaled_bitmap("topbar_win", nullptr, TOPBAR_ICON_SIZE);
    if (m_frame->IsMaximized()) {
        maximize_btn = this->AddTool(wxID_MAXIMIZE_FRAME, "", window_bitmap);
    }
    else {
        maximize_btn = this->AddTool(wxID_MAXIMIZE_FRAME, "", maximize_bitmap);
    }

    this->AddSpacer(6);

    wxBitmap close_bitmap = create_scaled_bitmap("topbar_close", nullptr, TOPBAR_ICON_SIZE);
    wxAuiToolBarItem* close_btn = this->AddTool(wxID_CLOSE_FRAME, "", close_bitmap);

    this->AddSpacer(6);

    Realize();
    // m_toolbar_h = this->GetSize().GetHeight();
    m_toolbar_h = 30;

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
    //this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnAccountClicked, this, ID_ACCOUNT);
    //this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnModelStoreClicked, this, ID_MODEL_STORE);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnPublishClicked, this, ID_PUBLISH);
}

BBLTopbar::~BBLTopbar()
{
    m_file_menu_item = nullptr;
    m_dropdown_menu_item = nullptr;
    m_file_menu = nullptr;
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
    plater->save_project();
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

void BBLTopbar::SaveNormalRect()
{
    m_normalRect = m_frame->GetRect();
}

void BBLTopbar::OnAccountClicked(wxAuiToolBarEvent& event)
{
    auto accountMenu = new wxMenu();

    append_menu_item(accountMenu, wxID_ANY, _L("Login"), _L("Login with your Account"),
        [](wxCommandEvent&) { Slic3r::GUI::login(); }, "", nullptr, [this]() {
            Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
            return !account_manager->is_user_login();
        }, this);
    append_menu_item(accountMenu, wxID_ANY, _L("Logout"), _L("Logout"),
        [](wxCommandEvent&) {
            Slic3r::AccountManager* account_manager = GUI::wxGetApp().getAccountManager();
            account_manager->user_logout();
        }, "", nullptr, [this]() {
            Slic3r::AccountManager* account_manager = GUI::wxGetApp().getAccountManager();
            return account_manager->is_user_login();
        }, this);
    /*
    append_menu_item(accountMenu, wxID_ANY, _L("My Project List"), _L(""),
        [this](wxCommandEvent&) {
            GUI::wxGetApp().load_url(wxString(wxGetApp().app_config->get_web_host_url() + MY_PROJECT_LIST_URL));
        }, "", nullptr,
        [this]() {
            Slic3r::AccountManager* account_manager = GUI::wxGetApp().getAccountManager();
            return account_manager->is_user_login();
        },
            this);
    append_menu_item(accountMenu, wxID_ANY, _L("My Collections"), _L(""),
        [this](wxCommandEvent&) {
            GUI::wxGetApp().load_url(wxString(wxGetApp().app_config->get_web_host_url() + MY_COLLECTIONS_URL));
        }, "", nullptr,
        [this]() {
            Slic3r::AccountManager* account_manager = GUI::wxGetApp().getAccountManager();
            return account_manager->is_user_login();
        },
            this);
    */
    auto publish_model_and_profile = [this](wxCommandEvent&) {
        if (wxGetApp().check_login()) {
            wxGetApp().plater()->show_publish_dialog();
        }
    };

    auto cond_publish_model = [this]() {
        if (GUI::wxGetApp().plater()->model().objects.empty()) return false;

        //BBS check gcode validation
        /*GUI::PartPlateList& part_plate_list = GUI::wxGetApp().plater()->get_partplate_list();
        bool publish_enable = part_plate_list.is_all_slice_results_ready_for_print();
        if (!publish_enable) return false;*/

        Slic3r::AccountManager* account_manager = GUI::wxGetApp().getAccountManager();
        return account_manager->can_publish();
    };

    auto cond_publish_profile = [this]() {
        bool result = true;
        if (GUI::wxGetApp().plater()->model().objects.empty()) return false;

        //BBS check gcode validation
        /*GUI::PartPlateList& part_plate_list = GUI::wxGetApp().plater()->get_partplate_list();
        bool publish_enable = part_plate_list.is_all_slice_results_ready_for_print();
        if (!publish_enable) return false;*/

        Slic3r::AccountManager* account_manager = GUI::wxGetApp().getAccountManager();
        return account_manager->can_publish();
    };

    append_menu_item(accountMenu, wxID_ANY, _L("Publish Model/Profile"), _L("Please slice all plates before upload"),
        publish_model_and_profile, "", nullptr, cond_publish_model, this);

    wxRect rect = this->GetToolRect(m_account_item->GetId());
    this->PopupMenu(accountMenu, rect.x, rect.y + this->GetSize().GetHeight() - 5);
}

void BBLTopbar::OnModelStoreClicked(wxAuiToolBarEvent& event)
{
    GUI::wxGetApp().load_url(wxString(wxGetApp().app_config->get_web_host_url() + MODEL_STORE_URL));
}

void BBLTopbar::OnPublishClicked(wxAuiToolBarEvent& event)
{
    if (GUI::wxGetApp().plater()->model().objects.empty()) return;

    Slic3r::AccountManager *account_manager = GUI::wxGetApp().getAccountManager();
    if (!account_manager->can_publish()) return;

    wxGetApp().plater()->show_publish_dialog();
}

void BBLTopbar::SetFileMenu(wxMenu* file_menu)
{
    m_file_menu = file_menu;
}

void BBLTopbar::AddDropDownSubMenu(wxMenu* sub_menu, const wxString& title)
{
    m_top_menu.AppendSubMenu(sub_menu, title);
}

void BBLTopbar::AddDropDownMenuItem(wxMenuItem* menu_item)
{
    m_top_menu.Append(menu_item);
}

wxMenu* BBLTopbar::GetTopMenu()
{
    return &m_top_menu;
}

void BBLTopbar::SetTitle(wxString title)
{
    m_title_item->SetLabel(title);
    m_title_item->SetAlignment(wxALIGN_CENTRE_HORIZONTAL);
    this->Refresh();
}

void BBLTopbar::SetMaximizedSize()
{
    maximize_btn->SetBitmap(maximize_bitmap);
}

void BBLTopbar::SetWindowSize()
{
    maximize_btn->SetBitmap(window_bitmap);
}

void BBLTopbar::UpdateToolbarWidth(int width)
{
    this->SetSize(width, m_toolbar_h);
}

void BBLTopbar::Rescale() {
    int em = em_unit(this);
    wxAuiToolBarItem* item;

    item = this->FindTool(ID_LOGO);
    item->SetBitmap(create_scaled_bitmap("topbar_logo", nullptr, TOPBAR_ICON_SIZE));

    item = this->FindTool(ID_TOP_FILE_MENU);
    item->SetBitmap(create_scaled_bitmap("topbar_file", nullptr, TOPBAR_ICON_SIZE));

    item = this->FindTool(ID_TOP_DROPDOWN_MENU);
    item->SetBitmap(create_scaled_bitmap("topbar_dropdown", nullptr, TOPBAR_ICON_SIZE));

    item = this->FindTool(wxID_OPEN);
    item->SetBitmap(create_scaled_bitmap("topbar_open", nullptr, TOPBAR_ICON_SIZE));

    item = this->FindTool(wxID_SAVE);
    item->SetBitmap(create_scaled_bitmap("topbar_save", nullptr, TOPBAR_ICON_SIZE));

    item = this->FindTool(wxID_UNDO);
    item->SetBitmap(create_scaled_bitmap("topbar_undo", nullptr, TOPBAR_ICON_SIZE));
    item->SetDisabledBitmap(create_scaled_bitmap("topbar_undo_inactive", nullptr, TOPBAR_ICON_SIZE));

    item = this->FindTool(wxID_REDO);
    item->SetBitmap(create_scaled_bitmap("topbar_redo", nullptr, TOPBAR_ICON_SIZE));
    item->SetDisabledBitmap(create_scaled_bitmap("topbar_redo_inactive", nullptr, TOPBAR_ICON_SIZE));

    item = this->FindTool(ID_TITLE);
    m_title_item->SetMinSize(wxSize(300 * em / 10, -1));

    item = this->FindTool(ID_PUBLISH);
    item->SetBitmap(create_scaled_bitmap("topbar_publish", nullptr, TOPBAR_ICON_SIZE));
    item->SetDisabledBitmap(create_scaled_bitmap("topbar_publish_disable", nullptr, TOPBAR_ICON_SIZE));

    /*item = this->FindTool(ID_MODEL_STORE);
    item->SetBitmap(create_scaled_bitmap("topbar_store", nullptr, TOPBAR_ICON_SIZE));

    item = this->FindTool(ID_ACCOUNT);
    item->SetBitmap(create_scaled_bitmap("topbar_account", nullptr, TOPBAR_ICON_SIZE));
    */

    item = this->FindTool(wxID_ICONIZE_FRAME);
    item->SetBitmap(create_scaled_bitmap("topbar_min", nullptr, TOPBAR_ICON_SIZE));

    item = this->FindTool(wxID_MAXIMIZE_FRAME);
    maximize_bitmap = create_scaled_bitmap("topbar_max", nullptr, TOPBAR_ICON_SIZE);
    window_bitmap = create_scaled_bitmap("topbar_win", nullptr, TOPBAR_ICON_SIZE);
    if (m_frame->IsMaximized()) {
        item->SetBitmap(window_bitmap);
    }
    else {
        item->SetBitmap(maximize_bitmap);
    }

    item = this->FindTool(wxID_CLOSE_FRAME);
    item->SetBitmap(create_scaled_bitmap("topbar_close", nullptr, TOPBAR_ICON_SIZE));

    Realize();
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
        m_normalRect = m_frame->GetRect();
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
        this->FindToolByCurrentPosition() != m_title_item) {
        mouse.Skip();
        return;
    }

    if (m_frame->IsMaximized()) {
        m_frame->Restore();
    }
    else {
        m_normalRect = m_frame->GetRect();
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
        // leave max state and adjust position 
        if (m_frame->IsMaximized()) {
            wxRect rect = m_frame->GetRect();
            // Filter unexcept mouse move
            if (m_delta + rect.GetLeftTop() != mouse_pos) {
                m_delta = mouse_pos - rect.GetLeftTop();
                m_delta.x = m_delta.x * m_normalRect.width / rect.width;
                m_delta.y = m_delta.y * m_normalRect.height / rect.height;
                m_frame->Restore();
            }
        }
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
