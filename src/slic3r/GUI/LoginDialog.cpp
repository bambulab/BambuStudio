#include "LoginDialog.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "format.hpp"

#include <wx/clipbrd.h>

namespace Slic3r { 
namespace GUI {
	wxDECLARE_EVENT(EVT_LOGIN_OK, wxCommandEvent);
	wxDECLARE_EVENT(EVT_LOGIN_FAILED, wxCommandEvent);

	wxDEFINE_EVENT(EVT_LOGIN_OK, wxCommandEvent);
	wxDEFINE_EVENT(EVT_LOGIN_FAILED, wxCommandEvent);


LoginDialog::LoginDialog()
	: DPIDialog(static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, _L("Login"), wxDefaultPosition,
	wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	bind_handlers();

	const int em = GUI::wxGetApp().em_unit();
	int min_width = WIN_WIDTH * em;
	int min_height = MIN_HEIGHT * em;
	int max_width = WIN_WIDTH * em;
	int max_height = MAX_HEIGHT * em;

	SetFont(wxGetApp().normal_font());
	wxColour bgr_clr = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
	SetBackgroundColour(bgr_clr);


	// Create GUI components and layout
	auto* panel = new wxPanel(this);
	wxBoxSizer* vsizer = new wxBoxSizer(wxVERTICAL);
	panel->SetSizer(vsizer);

	/* new line logo*/
	wxBoxSizer* hsizer = new wxBoxSizer(wxHORIZONTAL);
	/*main_sizer->Add(hsizer, 0, wxEXPAND | wxALL, SPACING);*/

	/* new line user info */
	/*hsizer = new wxBoxSizer(wxHORIZONTAL);*/
	auto user_info_sizer = new wxBoxSizer(wxVERTICAL);
	vsizer->Add(user_info_sizer, 0, wxEXPAND | wxALL, SPACING);
	m_label_user = new wxStaticText(panel, wxID_ANY, _L("User"), wxDefaultPosition, wxDefaultSize);
	m_txt_user = new wxTextCtrl(panel, wxID_ANY, _L(""), wxDefaultPosition, wxDefaultSize);
	m_txt_user->SetHint("username@email.com");
	user_info_sizer->Add(m_label_user, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0);
	user_info_sizer->Add(m_txt_user, 0, wxEXPAND | wxALIGN_CENTER_VERTICAL, 0);

	m_label_password = new wxStaticText(panel, wxID_ANY, _L("Password"), wxDefaultPosition, wxDefaultSize);
	m_txt_password = new wxTextCtrl(panel, wxID_ANY, _L(""), wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
	m_label_tips = new wxStaticText(panel, wxID_ANY, _L(""), wxDefaultPosition, wxDefaultSize);
	user_info_sizer->Add(m_label_password, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0);
	user_info_sizer->Add(m_txt_password, 0, wxEXPAND | wxALIGN_CENTER_VERTICAL, 0);
	user_info_sizer->Add(m_label_tips, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 0);

	/* new line login */
	hsizer = new wxBoxSizer(wxHORIZONTAL);
	vsizer->Add(hsizer, 0, wxEXPAND | wxALL, SPACING);
	m_btn_login = new wxButton(panel, wxID_ANY, _L("Login"), wxDefaultPosition, wxDefaultSize);
	m_btn_login->Bind(wxEVT_BUTTON, [this](wxCommandEvent& evt) {
		this->txt_stdout->Clear();
		Slic3r::AccountManager* account_manager = Slic3r::GUI::wxGetApp().getAccountManager();
		if (!account_manager->is_user_login()) {
			int result = account_manager->user_login(m_txt_user->GetValue().ToStdString(), m_txt_password->GetValue().ToStdString(),
				[this](int retcode, std::string info) {
					if (retcode == 0) {
						auto evt = new wxCommandEvent(EVT_LOGIN_OK, this->GetId());
						wxQueueEvent(this, evt);
					}
					else {
						auto evt = new wxCommandEvent(EVT_LOGIN_FAILED, this->GetId());
						evt->SetString(info);
						wxQueueEvent(this, evt);
					}
				});
			if (result < 0) {
				auto evt = new wxCommandEvent(EVT_LOGIN_FAILED, this->GetId());
				evt->SetString("Invalid User or Password!");
				wxQueueEvent(this, evt);
			}
		}
		else {
			this->Close();
		}
		});
	user_info_sizer->Add(m_btn_login, 0, wxEXPAND | wxALIGN_CENTER_VERTICAL, SPACING);

	spoiler = new wxCollapsiblePane(panel, wxID_ANY, _(L("Output log")), wxDefaultPosition, wxDefaultSize, wxCP_DEFAULT_STYLE | wxCP_NO_TLW_RESIZE);
	auto* spoiler_pane = spoiler->GetPane();
	auto* spoiler_sizer = new wxBoxSizer(wxVERTICAL);
	txt_stdout = new wxTextCtrl(spoiler_pane, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
	spoiler_sizer->Add(txt_stdout, 1, wxEXPAND);
	spoiler_pane->SetSizer(spoiler_sizer);
	// The doc says proportion need to be 0 for wxCollapsiblePane.
	// Experience says it needs to be 1, otherwise things won't get sized properly.
	vsizer->Add(spoiler, 1, wxEXPAND | wxALL, SPACING);

	m_label_info = new wxStaticText(this, wxID_ANY, _L(""), wxDefaultPosition, wxDefaultSize);
	vsizer->Add(m_label_info, 0, wxLEFT | wxALIGN_CENTER_VERTICAL);

	auto* topsizer = new wxBoxSizer(wxVERTICAL);
	topsizer->Add(panel, 1, wxEXPAND | wxALL, DIALOG_MARGIN);

	SetMinSize(wxSize(min_width, min_height));
	SetMaxSize(wxSize(min_width, max_height));
	SetSizerAndFit(topsizer);
	Refresh();

	const auto size = GetSize();
	SetSize(std::max(size.GetWidth(), static_cast<int>(min_width)), std::max(size.GetHeight(), static_cast<int>(min_height)));
	Layout();


	spoiler->Bind(wxEVT_COLLAPSIBLEPANE_CHANGED, [=](wxCollapsiblePaneEvent& evt) {
		if (evt.GetCollapsed()) {
			const int em = GUI::wxGetApp().em_unit();
			this->SetMinSize(wxSize(WIN_WIDTH * em, MIN_HEIGHT * em));
			const auto new_height = this->GetSize().GetHeight() - this->txt_stdout->GetSize().GetHeight();
			this->SetSize(this->GetSize().GetWidth(), new_height);
		}
		else {
			this->SetMinSize(wxSize(WIN_WIDTH * em, MIN_HEIGHT_EXPANDED * em));
		}

		this->Layout();
		this->fit_no_shrink();
		});
}

void LoginDialog::fit_no_shrink()
{
	// Ensure content fits into window and window is not shrinked
	const auto old_size = GetSize();
	Layout();
	Fit();
	const auto new_size = GetSize();
	const auto new_width = std::max(old_size.GetWidth(), new_size.GetWidth());
	const auto new_height = std::max(old_size.GetHeight(), new_size.GetHeight());
	SetSize(new_width, new_height);
}

void LoginDialog::bind_handlers()
{
	Bind(EVT_LOGIN_OK, [this](wxCommandEvent& evt) {
		this->m_label_tips->SetLabelText("Login OK!");
		this->m_btn_login->SetLabelText("Quit");
		this->txt_stdout->Clear();
		});

	Bind(EVT_LOGIN_FAILED, [this](wxCommandEvent& evt) {
		this->m_label_tips->SetLabelText("Login Failed!");
		this->txt_stdout->AppendText(evt.GetString());
		});
}

void LoginDialog::on_dpi_changed(const wxRect& suggested_rect)
{
	Fit();
	Refresh();
}

} // namespace GUI
} // namespace Slic3r