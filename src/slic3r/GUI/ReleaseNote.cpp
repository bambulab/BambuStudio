#include "ReleaseNote.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/Thread.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_Preview.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/StaticBox.hpp"
#include "Widgets/WebView.hpp"
#include "Widgets/SwitchButton.hpp"

#include <wx/regex.h>
#include <wx/progdlg.h>
#include <wx/clipbrd.h>
#include <wx/dcgraph.h>
#include <miniz.h>
#include <wx/valnum.h>
#include <algorithm>
#include "Plater.hpp"
#include "BitmapCache.hpp"

#include "DeviceCore/DevManager.h"
#include "DeviceCore/DevStorage.h"

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_SECONDARY_CHECK_CONFIRM, wxCommandEvent);
wxDEFINE_EVENT(EVT_SECONDARY_CHECK_CANCEL, wxCommandEvent);
wxDEFINE_EVENT(EVT_SECONDARY_CHECK_DONE, wxCommandEvent);
wxDEFINE_EVENT(EVT_SECONDARY_CHECK_RESUME, wxCommandEvent);
wxDEFINE_EVENT(EVT_CHECKBOX_CHANGE, wxCommandEvent);
wxDEFINE_EVENT(EVT_ENTER_IP_ADDRESS, wxCommandEvent);
wxDEFINE_EVENT(EVT_CLOSE_IPADDRESS_DLG, wxCommandEvent);
wxDEFINE_EVENT(EVT_CHECK_IP_ADDRESS_FAILED, wxCommandEvent);
wxDEFINE_EVENT(EVT_CHECK_IP_ADDRESS_LAYOUT, wxCommandEvent);
wxDEFINE_EVENT(EVT_SECONDARY_CHECK_RETRY, wxCommandEvent);
wxDEFINE_EVENT(EVT_UPDATE_NOZZLE, wxCommandEvent);
wxDEFINE_EVENT(EVT_UPDATE_TEXT_MSG, wxCommandEvent);
wxDEFINE_EVENT(EVT_ERROR_DIALOG_BTN_CLICKED, wxCommandEvent);

ReleaseNoteDialog::ReleaseNoteDialog(Plater *plater /*= nullptr*/)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Release Note"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top   = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(30));

    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_body->Add(0, 0, 0, wxLEFT, FromDIP(38));

    auto sm = create_scaled_bitmap("BambuStudio", nullptr,  70);
    auto brand = new wxStaticBitmap(this, wxID_ANY, sm, wxDefaultPosition, wxSize(FromDIP(70), FromDIP(70)));

    m_sizer_body->Add(brand, 0, wxALL, 0);

    m_sizer_body->Add(0, 0, 0, wxRIGHT, FromDIP(25));

    wxBoxSizer *m_sizer_right = new wxBoxSizer(wxVERTICAL);

    m_text_up_info = new Label(this, Label::Head_14, wxEmptyString, LB_AUTO_WRAP);
    m_text_up_info->SetForegroundColour(wxColour(0x26, 0x2E, 0x30));
    m_sizer_right->Add(m_text_up_info, 0, wxEXPAND, 0);

    m_sizer_right->Add(0, 0, 1, wxTOP, FromDIP(15));

    m_vebview_release_note = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(560), FromDIP(430)), wxVSCROLL);
    m_vebview_release_note->SetScrollRate(5, 5);
    m_vebview_release_note->SetBackgroundColour(wxColour(0xF8, 0xF8, 0xF8));
    m_vebview_release_note->SetMaxSize(wxSize(FromDIP(560), FromDIP(430)));

    m_sizer_right->Add(m_vebview_release_note, 0, wxEXPAND | wxRIGHT, FromDIP(20));
    m_sizer_body->Add(m_sizer_right, 1, wxBOTTOM | wxEXPAND, FromDIP(30));
    m_sizer_main->Add(m_sizer_body, 0, wxEXPAND, 0);

    SetSizer(m_sizer_main);
    Layout();
    m_sizer_main->Fit(this);

    Centre(wxBOTH);
    wxGetApp().UpdateDlgDarkUI(this);
}

ReleaseNoteDialog::~ReleaseNoteDialog() {}


void ReleaseNoteDialog::on_dpi_changed(const wxRect &suggested_rect)
{
}

void ReleaseNoteDialog::update_release_note(wxString release_note, std::string version)
{
    m_text_up_info->SetLabel(wxString::Format(_L("version %s update information :"), version));
    wxBoxSizer * sizer_text_release_note = new wxBoxSizer(wxVERTICAL);
    auto        m_staticText_release_note = new ::Label(m_vebview_release_note, release_note, LB_AUTO_WRAP);
    m_staticText_release_note->SetMinSize(wxSize(FromDIP(530), -1));
    m_staticText_release_note->SetMaxSize(wxSize(FromDIP(530), -1));
    sizer_text_release_note->Add(m_staticText_release_note, 0, wxALL, 5);
    m_vebview_release_note->SetSizer(sizer_text_release_note);
    m_vebview_release_note->Layout();
    m_vebview_release_note->Fit();
    wxGetApp().UpdateDlgDarkUI(this);
}

UpdatePluginDialog::UpdatePluginDialog(wxWindow* parent /*= nullptr*/)
    : DPIDialog(static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, _L("Network plug-in update"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    wxBoxSizer* m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(30));

    wxBoxSizer* m_sizer_body = new wxBoxSizer(wxHORIZONTAL);



    auto sm = create_scaled_bitmap("BambuStudio", nullptr, 55);
    auto brand = new wxStaticBitmap(this, wxID_ANY, sm, wxDefaultPosition, wxSize(FromDIP(55), FromDIP(55)));

    wxBoxSizer* m_sizer_right = new wxBoxSizer(wxVERTICAL);

    m_text_up_info = new Label(this, Label::Head_13, wxEmptyString, LB_AUTO_WRAP);
    m_text_up_info->SetMaxSize(wxSize(FromDIP(260), -1));
    m_text_up_info->SetForegroundColour(wxColour(0x26, 0x2E, 0x30));


    operation_tips = new ::Label(this, Label::Body_12, _L("Click OK to update the Network plug-in when Bambu Studio launches next time."), LB_AUTO_WRAP);
    operation_tips->SetMinSize(wxSize(FromDIP(260), -1));
    operation_tips->SetMaxSize(wxSize(FromDIP(260), -1));

    m_vebview_release_note = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_vebview_release_note->SetScrollRate(5, 5);
    m_vebview_release_note->SetBackgroundColour(wxColour(0xF8, 0xF8, 0xF8));
    m_vebview_release_note->SetMinSize(wxSize(FromDIP(260), FromDIP(150)));
    m_vebview_release_note->SetMaxSize(wxSize(FromDIP(260), FromDIP(150)));

    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);

    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

    auto m_button_ok = new Button(this, _L("OK"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(*wxWHITE);
    m_button_ok->SetTextColor(wxColour("#FFFFFE"));
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetCornerRadius(FromDIP(12));

    m_button_ok->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        EndModal(wxID_OK);
        });

    auto m_button_cancel = new Button(this, _L("Cancel"));
    m_button_cancel->SetBackgroundColor(btn_bg_white);
    m_button_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_cancel->SetFont(Label::Body_12);
    m_button_cancel->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetCornerRadius(FromDIP(12));

    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        EndModal(wxID_NO);
        });

    sizer_button->AddStretchSpacer();
    sizer_button->Add(m_button_ok, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_cancel, 0, wxALL, FromDIP(5));

    m_sizer_right->Add(m_text_up_info, 0, wxEXPAND, 0);
    m_sizer_right->Add(0, 0, 0, wxTOP, FromDIP(5));
    m_sizer_right->Add(m_vebview_release_note, 0, wxEXPAND | wxRIGHT, FromDIP(20));
    m_sizer_right->Add(0, 0, 0, wxTOP, FromDIP(5));
    m_sizer_right->Add(operation_tips, 1, wxEXPAND | wxRIGHT, FromDIP(20));
    m_sizer_right->Add(0, 0, 0, wxTOP, FromDIP(5));
    m_sizer_right->Add(sizer_button, 0, wxEXPAND | wxRIGHT, FromDIP(20));

    m_sizer_body->Add(0, 0, 0, wxLEFT, FromDIP(24));
    m_sizer_body->Add(brand, 0, wxALL, 0);
    m_sizer_body->Add(0, 0, 0, wxRIGHT, FromDIP(20));
    m_sizer_body->Add(m_sizer_right, 1, wxBOTTOM | wxEXPAND, FromDIP(18));
    m_sizer_main->Add(m_sizer_body, 0, wxEXPAND, 0);

    SetSizer(m_sizer_main);
    Layout();
    m_sizer_main->Fit(this);

    Centre(wxBOTH);
    wxGetApp().UpdateDlgDarkUI(this);
}

UpdatePluginDialog::~UpdatePluginDialog() {}


void UpdatePluginDialog::on_dpi_changed(const wxRect& suggested_rect)
{
}

void UpdatePluginDialog::update_info(std::string json_path)
{
    std::string version_str, description_str;
    wxString version;
    wxString description;

    try {
        boost::nowide::ifstream ifs(json_path);
        json j;
        ifs >> j;

        version_str = j["version"];
        description_str = j["description"];
    }
    catch (nlohmann::detail::parse_error& err) {
        //BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse " << json_path << " got a nlohmann::detail::parse_error, reason = " << err.what();
        return;
    }

    version = from_u8(version_str);
    description = from_u8(description_str);

    m_text_up_info->SetLabel(wxString::Format(_L("A new Network plug-in(%s) available, Do you want to install it?"), version));
    m_text_up_info->SetMinSize(wxSize(FromDIP(260), -1));
    m_text_up_info->SetMaxSize(wxSize(FromDIP(260), -1));
    wxBoxSizer* sizer_text_release_note = new wxBoxSizer(wxVERTICAL);
    auto        m_text_label            = new ::Label(m_vebview_release_note, Label::Body_13, description, LB_AUTO_WRAP);
    m_text_label->SetMinSize(wxSize(FromDIP(235), -1));
    m_text_label->SetMaxSize(wxSize(FromDIP(235), -1));

    sizer_text_release_note->Add(m_text_label, 0, wxALL, 5);
    m_vebview_release_note->SetSizer(sizer_text_release_note);
    m_vebview_release_note->Layout();
    m_vebview_release_note->Fit();
    wxGetApp().UpdateDlgDarkUI(this);
    Layout();
    Fit();
}

UpdateVersionDialog::UpdateVersionDialog(wxWindow *parent)
    : DPIDialog(parent, wxID_ANY, _L("New version of Bambu Studio"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);

    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top   = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));


    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxHORIZONTAL);



    auto sm    = create_scaled_bitmap("BambuStudio", nullptr, 70);
    m_brand = new wxStaticBitmap(this, wxID_ANY, sm, wxDefaultPosition, wxSize(FromDIP(70), FromDIP(70)));



    wxBoxSizer *m_sizer_right = new wxBoxSizer(wxVERTICAL);

    m_text_up_info = new Label(this, Label::Head_14, wxEmptyString, LB_AUTO_WRAP);
    m_text_up_info->SetForegroundColour(wxColour(0x26, 0x2E, 0x30));

    m_simplebook_release_note = new wxSimplebook(this);
    m_simplebook_release_note->SetSize(wxSize(FromDIP(560), FromDIP(430)));
    m_simplebook_release_note->SetMinSize(wxSize(FromDIP(560), FromDIP(430)));
    m_simplebook_release_note->SetBackgroundColour(wxColour(0xF8, 0xF8, 0xF8));

    m_scrollwindows_release_note = new wxScrolledWindow(m_simplebook_release_note, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(560), FromDIP(430)), wxVSCROLL);
    m_scrollwindows_release_note->SetScrollRate(5, 5);
    m_scrollwindows_release_note->SetBackgroundColour(wxColour(0xF8, 0xF8, 0xF8));

    //webview
    m_vebview_release_note = CreateTipView(m_simplebook_release_note);
    m_vebview_release_note->SetBackgroundColour(wxColour(0xF8, 0xF8, 0xF8));
    m_vebview_release_note->SetSize(wxSize(FromDIP(560), FromDIP(430)));
    m_vebview_release_note->SetMinSize(wxSize(FromDIP(560), FromDIP(430)));
    //m_vebview_release_note->SetMaxSize(wxSize(FromDIP(560), FromDIP(430)));

	fs::path ph(data_dir());
	ph /= "resources/tooltip/releasenote.html";
	if (!fs::exists(ph)) {
		ph = resources_dir();
		ph /= "tooltip/releasenote.html";
	}
	auto url = ph.string();
	std::replace(url.begin(), url.end(), '\\', '/');
	url = "file:///" + url;
    m_vebview_release_note->LoadURL(from_u8(url));

    m_simplebook_release_note->AddPage(m_scrollwindows_release_note, wxEmptyString, false);
    m_simplebook_release_note->AddPage(m_vebview_release_note, wxEmptyString, false);



    m_bitmap_open_in_browser = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("open_in_browser", this, 12), wxDefaultPosition, wxDefaultSize, 0 );
    m_link_open_in_browser   = new wxHyperlinkCtrl(this, wxID_ANY, "Open in browser", "");
    m_link_open_in_browser->SetFont(Label::Body_12);


    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);


    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                            std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

    m_button_download = new Button(this, _L("Download"));
    m_button_download->SetBackgroundColor(btn_bg_green);
    m_button_download->SetBorderColor(*wxWHITE);
    m_button_download->SetTextColor(wxColour("#FFFFFE"));
    m_button_download->SetFont(Label::Body_12);
    m_button_download->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_download->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_download->SetCornerRadius(FromDIP(12));

    m_button_download->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        EndModal(wxID_YES);
    });

    m_button_skip_version = new Button(this, _L("Skip this Version"));
    m_button_skip_version->SetBackgroundColor(btn_bg_white);
    m_button_skip_version->SetBorderColor(wxColour(38, 46, 48));
    m_button_skip_version->SetFont(Label::Body_12);
    m_button_skip_version->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_skip_version->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_skip_version->SetCornerRadius(FromDIP(12));

    m_button_skip_version->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        wxGetApp().set_skip_version(true);
        EndModal(wxID_NO);
    });


    m_button_cancel = new Button(this, _L("Cancel"));
    m_button_cancel->SetBackgroundColor(btn_bg_white);
    m_button_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_cancel->SetFont(Label::Body_12);
    m_button_cancel->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetCornerRadius(FromDIP(12));

    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        EndModal(wxID_NO);
    });

    m_sizer_main->Add(m_line_top, 0, wxEXPAND | wxBOTTOM, 0);

    sizer_button->Add(m_bitmap_open_in_browser, 0, wxALIGN_CENTER | wxLEFT, FromDIP(7));
    sizer_button->Add(m_link_open_in_browser, 0, wxALIGN_CENTER| wxLEFT, FromDIP(3));
    //sizer_button->Add(m_remind_choice, 0, wxALL | wxEXPAND, FromDIP(5));
    sizer_button->AddStretchSpacer();
    sizer_button->Add(m_button_download, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_skip_version, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_cancel, 0, wxALL, FromDIP(5));

    m_sizer_right->Add(m_text_up_info, 0, wxEXPAND | wxBOTTOM | wxTOP, FromDIP(15));
    m_sizer_right->Add(m_simplebook_release_note, 1, wxEXPAND | wxRIGHT, 0);
    m_sizer_right->Add(sizer_button, 0, wxEXPAND | wxRIGHT, FromDIP(20));

    m_sizer_body->Add(m_brand, 0, wxTOP|wxRIGHT|wxLEFT, FromDIP(15));
    m_sizer_body->Add(0, 0, 0, wxRIGHT, 0);
    m_sizer_body->Add(m_sizer_right, 1, wxBOTTOM | wxEXPAND, FromDIP(8));
    m_sizer_main->Add(m_sizer_body, 1, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxBOTTOM, 10);

    SetSizer(m_sizer_main);
    Layout();
    Fit();

    SetMinSize(GetSize());

    Centre(wxBOTH);
    wxGetApp().UpdateDlgDarkUI(this);
}

UpdateVersionDialog::~UpdateVersionDialog() {}


wxWebView* UpdateVersionDialog::CreateTipView(wxWindow* parent)
{
	wxWebView* tipView = WebView::CreateWebView(parent, "");
	tipView->Bind(wxEVT_WEBVIEW_LOADED, &UpdateVersionDialog::OnLoaded, this);
	tipView->Bind(wxEVT_WEBVIEW_NAVIGATED, &UpdateVersionDialog::OnTitleChanged, this);
	tipView->Bind(wxEVT_WEBVIEW_ERROR, &UpdateVersionDialog::OnError, this);
	return tipView;
}

void UpdateVersionDialog::OnLoaded(wxWebViewEvent& event)
{
    event.Skip();
}

void UpdateVersionDialog::OnTitleChanged(wxWebViewEvent& event)
{
    //ShowReleaseNote();
    event.Skip();
}
void UpdateVersionDialog::OnError(wxWebViewEvent& event)
{
    event.Skip();
}

static std::string url_encode(const std::string& value) {
	std::ostringstream escaped;
	escaped.fill('0');
	escaped << std::hex;
	for (std::string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
		std::string::value_type c = (*i);

		// Keep alphanumeric and other accepted characters intact
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			escaped << c;
			continue;
		}

		// Any other characters are percent-encoded
		escaped << std::uppercase;
		escaped << '%' << std::setw(2) << int((unsigned char)c);
		escaped << std::nouppercase;
	}
	return escaped.str();
}

bool UpdateVersionDialog::ShowReleaseNote(std::string content)
{
	auto script = "window.showMarkdown('" + url_encode(content) + "', true);";
    RunScript(script);
    return true;
}

void UpdateVersionDialog::RunScript(std::string script)
{
    WebView::RunScript(m_vebview_release_note, script);
    script.clear();
}

void UpdateVersionDialog::on_dpi_changed(const wxRect &suggested_rect) {
    m_button_download->Rescale();
    m_button_skip_version->Rescale();
    m_button_cancel->Rescale();
}

std::vector<std::string> UpdateVersionDialog::splitWithStl(std::string str,std::string pattern)
{
    std::string::size_type pos;
    std::vector<std::string> result;
    str += pattern;
    int size = str.size();
    for (int i = 0; i < size; i++)
    {
        pos = str.find(pattern, i);
        if (pos < size)
        {
            std::string s = str.substr(i, pos - i);
            result.push_back(s);
            i = pos + pattern.size() - 1;
        }
    }
    return result;
}

void UpdateVersionDialog::update_version_info(wxString release_note, wxString version)
{
    //bbs check whether the web display is used
    bool use_web_link       = false;
    url_line                = "";
    auto split_array        =  splitWithStl(release_note.ToStdString(), "###");

    if (split_array.size() >= 3) {
        for (auto i = 0; i < split_array.size(); i++) {
            std::string url = split_array[i];
            if (std::strstr(url.c_str(), "http://") != NULL || std::strstr(url.c_str(), "https://") != NULL) {
                use_web_link = true;
                url_line = url;
                break;
            }
        }
    }


    if (use_web_link) {
        m_brand->Hide();
        m_text_up_info->Hide();
        m_simplebook_release_note->SetSelection(1);
        m_vebview_release_note->LoadURL(from_u8(url_line));
        m_link_open_in_browser->SetURL(url_line);
    }
    else {
        m_simplebook_release_note->SetMaxSize(wxSize(FromDIP(560), FromDIP(430)));
        m_simplebook_release_note->SetSelection(0);
        m_text_up_info->SetLabel(wxString::Format(_L("Click to download new version in default browser: %s"), version));
        wxBoxSizer* sizer_text_release_note = new wxBoxSizer(wxVERTICAL);
        auto        m_staticText_release_note = new ::Label(m_scrollwindows_release_note, release_note, LB_AUTO_WRAP);
        m_staticText_release_note->SetMinSize(wxSize(FromDIP(560), -1));
        m_staticText_release_note->SetMaxSize(wxSize(FromDIP(560), -1));
        sizer_text_release_note->Add(m_staticText_release_note, 0, wxALL, 5);
        m_scrollwindows_release_note->SetSizer(sizer_text_release_note);
        m_scrollwindows_release_note->Layout();
        m_scrollwindows_release_note->Fit();
        SetMinSize(GetSize());
        SetMaxSize(GetSize());
    }

    wxGetApp().UpdateDlgDarkUI(this);
    Layout();
    Fit();
}

SecondaryCheckDialog::SecondaryCheckDialog(wxWindow* parent, wxWindowID id, const wxString& title, enum ButtonStyle btn_style, const wxPoint& pos, const wxSize& size, long style, bool not_show_again_check)
    :DPIFrame(parent, id, title, pos, size, style)
{
    m_button_style = btn_style;
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(400), 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(5));

    wxBoxSizer* m_sizer_right = new wxBoxSizer(wxVERTICAL);

    m_sizer_right->Add(0, 0, 1, wxTOP, FromDIP(15));

    m_vebview_release_note = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_vebview_release_note->SetScrollRate(0, 5);
    m_vebview_release_note->SetBackgroundColour(*wxWHITE);
    m_vebview_release_note->SetMinSize(wxSize(FromDIP(400), FromDIP(380)));
    m_sizer_right->Add(m_vebview_release_note, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(15));


    auto bottom_sizer = new wxBoxSizer(wxVERTICAL);
    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);
    btn_bg_green = StateColor(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    btn_bg_white = StateColor(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));


    if (not_show_again_check) {
        auto checkbox_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_show_again_checkbox = new wxCheckBox(this, wxID_ANY, _L("Don't show again"), wxDefaultPosition, wxDefaultSize, 0);
        m_show_again_checkbox->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, [this](wxCommandEvent& e) {
            not_show_again = !not_show_again;
            m_show_again_checkbox->SetValue(not_show_again);
        });
        checkbox_sizer->Add(FromDIP(15), 0, 0, 0);
        checkbox_sizer->Add(m_show_again_checkbox, 0, wxALL, FromDIP(5));
        bottom_sizer->Add(checkbox_sizer, 0, wxBOTTOM | wxEXPAND, 0);
    }
    m_button_ok = new Button(this, _L("Confirm"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(*wxWHITE);
    m_button_ok->SetTextColor(wxColour("#FFFFFE"));
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_ok->SetMaxSize(wxSize(-1, FromDIP(24)));
    m_button_ok->SetCornerRadius(FromDIP(12));

    m_button_ok->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent& e) {
        wxCommandEvent evt(EVT_SECONDARY_CHECK_CONFIRM, GetId());
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(evt);
        this->on_hide();
    });

    m_button_retry = new Button(this, _L("Retry"));
    m_button_retry->SetBackgroundColor(btn_bg_green);
    m_button_retry->SetBorderColor(*wxWHITE);
    m_button_retry->SetTextColor(wxColour("#FFFFFE"));
    m_button_retry->SetFont(Label::Body_12);
    m_button_retry->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_retry->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_retry->SetMaxSize(wxSize(-1, FromDIP(24)));
    m_button_retry->SetCornerRadius(FromDIP(12));

    m_button_retry->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent& e) {
        wxCommandEvent evt(EVT_SECONDARY_CHECK_RETRY, GetId());
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(evt);
        this->on_hide();
    });

    m_button_cancel = new Button(this, _L("Cancel"));
    m_button_cancel->SetBackgroundColor(btn_bg_white);
    m_button_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_cancel->SetFont(Label::Body_12);
    m_button_cancel->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_cancel->SetMaxSize(wxSize(-1, FromDIP(24)));
    m_button_cancel->SetCornerRadius(FromDIP(12));

    m_button_cancel->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent& e) {
            wxCommandEvent evt(EVT_SECONDARY_CHECK_CANCEL);
            e.SetEventObject(this);
            GetEventHandler()->ProcessEvent(evt);
            this->on_hide();
        });

    m_button_fn = new Button(this, _L("Done"));
    m_button_fn->SetBackgroundColor(btn_bg_white);
    m_button_fn->SetBorderColor(wxColour(38, 46, 48));
    m_button_fn->SetFont(Label::Body_12);
    m_button_fn->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_fn->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_fn->SetMaxSize(wxSize(-1, FromDIP(24)));
    m_button_fn->SetCornerRadius(FromDIP(12));

    m_button_fn->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent& e) {
            post_event(wxCommandEvent(EVT_SECONDARY_CHECK_DONE));
            e.Skip();
        });

    m_button_resume = new Button(this, _L("resume"));
    m_button_resume->SetBackgroundColor(btn_bg_white);
    m_button_resume->SetBorderColor(wxColour(38, 46, 48));
    m_button_resume->SetFont(Label::Body_12);
    m_button_resume->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_resume->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_resume->SetMaxSize(wxSize(-1, FromDIP(24)));
    m_button_resume->SetCornerRadius(FromDIP(12));

    m_button_resume->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent& e) {
        post_event(wxCommandEvent(EVT_SECONDARY_CHECK_RESUME));
        e.Skip();
        });
    m_button_resume->Hide();

    if (btn_style == CONFIRM_AND_CANCEL) {
        m_button_cancel->Show();
        m_button_fn->Hide();
        m_button_retry->Hide();
    } else if (btn_style == CONFIRM_AND_DONE) {
        m_button_cancel->Hide();
        m_button_fn->Show();
        m_button_retry->Hide();
    } else if (btn_style == CONFIRM_AND_RETRY) {
        m_button_retry->Show();
        m_button_cancel->Hide();
        m_button_fn->Hide();
    } else if (style == DONE_AND_RETRY) {
        m_button_retry->Show();
        m_button_fn->Show();
        m_button_cancel->Hide();
    }
    else {
        m_button_retry->Hide();
        m_button_cancel->Hide();
        m_button_fn->Hide();
    }

    sizer_button->AddStretchSpacer();
    sizer_button->Add(m_button_resume, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_retry, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_fn, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_ok, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_cancel, 0, wxALL, FromDIP(5));
    sizer_button->Add(FromDIP(5),0, 0, 0);
    bottom_sizer->Add(sizer_button, 0, wxEXPAND | wxRIGHT | wxLEFT, 0);


    m_sizer_right->Add(bottom_sizer, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(15));
    m_sizer_right->Add(0, 0, 0, wxTOP,FromDIP(10));

    m_sizer_main->Add(m_sizer_right, 0, wxBOTTOM | wxEXPAND, FromDIP(5));

    Bind(wxEVT_CLOSE_WINDOW, [this](auto& e) {this->on_hide();});
    Bind(wxEVT_ACTIVATE, [this](auto& e) { if (!e.GetActive()) this->RequestUserAttention(wxUSER_ATTENTION_ERROR); });

    SetSizer(m_sizer_main);
    Layout();
    m_sizer_main->Fit(this);

    CenterOnParent();
    wxGetApp().UpdateFrameDarkUI(this);
}

void SecondaryCheckDialog::post_event(wxCommandEvent&& event)
{
    if (event_parent) {
        event.SetString("");
        event.SetEventObject(event_parent);
        wxPostEvent(event_parent, event);
        event.Skip();
    }
}

void SecondaryCheckDialog::update_text(wxString text)
{
    wxBoxSizer* sizer_text_release_note = new wxBoxSizer(wxVERTICAL);

    if (!m_staticText_release_note) {
        m_staticText_release_note = new Label(m_vebview_release_note, text, LB_AUTO_WRAP);
        wxBoxSizer* top_blank_sizer = new wxBoxSizer(wxVERTICAL);
        wxBoxSizer* bottom_blank_sizer = new wxBoxSizer(wxVERTICAL);
        top_blank_sizer->Add(FromDIP(5), 0, wxALIGN_CENTER | wxALL, FromDIP(5));
        bottom_blank_sizer->Add(FromDIP(5), 0, wxALIGN_CENTER | wxALL, FromDIP(5));

        sizer_text_release_note->Add(top_blank_sizer, 0, wxALIGN_CENTER | wxALL, FromDIP(5));
        sizer_text_release_note->Add(m_staticText_release_note, 0, wxALIGN_CENTER, FromDIP(5));
        sizer_text_release_note->Add(bottom_blank_sizer, 0, wxALIGN_CENTER | wxALL, FromDIP(5));
        m_vebview_release_note->SetSizer(sizer_text_release_note);
    }
    m_staticText_release_note->SetMaxSize(wxSize(FromDIP(330), -1));
    m_staticText_release_note->SetMinSize(wxSize(FromDIP(330), -1));
    m_staticText_release_note->SetLabelText(text);
    m_vebview_release_note->Layout();

    auto text_size = m_staticText_release_note->GetBestSize();
    if (text_size.y < FromDIP(360))
        m_vebview_release_note->SetMinSize(wxSize(FromDIP(360), text_size.y + FromDIP(25)));
    else {
        m_vebview_release_note->SetMinSize(wxSize(FromDIP(360), FromDIP(360)));
    }

    Layout();
    Fit();
}

void SecondaryCheckDialog::on_show()
{
#ifdef __APPLE__
    if (wxGetApp().mainframe && wxGetApp().mainframe->get_mac_full_screen()) {
        SetWindowStyleFlag(GetWindowStyleFlag() | wxSTAY_ON_TOP);
    }
#endif
    wxGetApp().UpdateFrameDarkUI(this);
    // recover button color
    wxMouseEvent evt_ok(wxEVT_LEFT_UP);
    m_button_ok->GetEventHandler()->ProcessEvent(evt_ok);
    wxMouseEvent evt_cancel(wxEVT_LEFT_UP);
    m_button_cancel->GetEventHandler()->ProcessEvent(evt_cancel);

    this->Show();
    this->Raise();
}

void SecondaryCheckDialog::on_hide()
{
    if (m_show_again_checkbox != nullptr && not_show_again && show_again_config_text != "")
        wxGetApp().app_config->set(show_again_config_text, "1");

    this->Hide();
    if (wxGetApp().mainframe != nullptr) {
        wxGetApp().mainframe->Show();
        wxGetApp().mainframe->Raise();
    }
}

void SecondaryCheckDialog::update_title_style(wxString title, SecondaryCheckDialog::ButtonStyle style, wxWindow* parent)
{
    if (m_button_style == style && title == GetTitle() == title) return;

    SetTitle(title);

    event_parent = parent;

    if (style == CONFIRM_AND_CANCEL) {
        m_button_cancel->Show();
        m_button_fn->Hide();
        m_button_retry->Hide();
        m_button_resume->Hide();
    }
    else if (style == CONFIRM_AND_DONE) {
        m_button_cancel->Hide();
        m_button_fn->Show();
        m_button_retry->Hide();
        m_button_resume->Hide();
    }
    else if (style == CONFIRM_AND_RETRY) {
        m_button_retry->Show();
        m_button_cancel->Hide();
        m_button_fn->Hide();
        m_button_resume->Hide();
    }
    else if (style == DONE_AND_RETRY) {
        m_button_retry->Show();
        m_button_fn->Show();
        m_button_cancel->Hide();
        m_button_resume->Hide();
    }
    else if(style == CONFIRM_AND_RESUME)
    {
        m_button_retry->Hide();
        m_button_fn->Hide();
        m_button_cancel->Hide();
        m_button_resume->Show();
    }
    else {
        m_button_retry->Hide();
        m_button_cancel->Hide();
        m_button_fn->Hide();
        m_button_resume->Hide();

    }


    Layout();
}

void SecondaryCheckDialog::update_btn_label(wxString ok_btn_text, wxString cancel_btn_text)
{
    m_button_ok->SetLabel(ok_btn_text);
    m_button_cancel->SetLabel(cancel_btn_text);
    rescale();
}

SecondaryCheckDialog::~SecondaryCheckDialog()
{

}

void SecondaryCheckDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    rescale();
}

void SecondaryCheckDialog::msw_rescale() {
    wxGetApp().UpdateFrameDarkUI(this);
    Refresh();
}

void SecondaryCheckDialog::rescale()
{
    m_button_ok->Rescale();
    m_button_cancel->Rescale();
}

PrintErrorDialog::PrintErrorDialog(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style)
    :DPIFrame(parent, id, title, pos, size, style)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));
    SetBackgroundColour(*wxWHITE);

    btn_bg_white = StateColor(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

    m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(350), 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(5));

    wxBoxSizer* m_sizer_right = new wxBoxSizer(wxVERTICAL);

    m_sizer_right->Add(0, 0, 1, wxTOP, FromDIP(5));

    m_vebview_release_note = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_vebview_release_note->SetScrollRate(0, 5);
    m_vebview_release_note->SetBackgroundColour(*wxWHITE);
    m_vebview_release_note->SetMinSize(wxSize(FromDIP(320), FromDIP(250)));
    m_sizer_right->Add(m_vebview_release_note, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(15));

    m_error_prompt_pic_static = new wxStaticBitmap(m_vebview_release_note, wxID_ANY, wxBitmap(), wxDefaultPosition, wxSize(FromDIP(300), FromDIP(180)));

    auto bottom_sizer = new wxBoxSizer(wxVERTICAL);
    m_sizer_button = new wxBoxSizer(wxVERTICAL);

    bottom_sizer->Add(m_sizer_button, 0, wxEXPAND | wxRIGHT | wxLEFT, 0);

    m_sizer_right->Add(bottom_sizer, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(20));
    m_sizer_right->Add(0, 0, 0, wxTOP, FromDIP(10));

    m_sizer_main->Add(m_sizer_right, 0, wxBOTTOM | wxEXPAND, FromDIP(5));

    Bind(wxEVT_CLOSE_WINDOW, [this](auto& e) {this->on_hide(); });
    Bind(wxEVT_ACTIVATE, [this](auto& e) { if (!e.GetActive()) this->RequestUserAttention(wxUSER_ATTENTION_ERROR); });
    Bind(wxEVT_WEBREQUEST_STATE, &PrintErrorDialog::on_webrequest_state, this);


    SetSizer(m_sizer_main);
    Layout();
    m_sizer_main->Fit(this);

    init_button_list();

    CenterOnParent();
    wxGetApp().UpdateFrameDarkUI(this);
}

void PrintErrorDialog::post_event(wxCommandEvent& event)
{
    if (event_parent) {
        event.SetString("");
        event.SetEventObject(event_parent);
        wxPostEvent(event_parent, event);
        event.Skip();
    }
}

void PrintErrorDialog::post_event(wxCommandEvent &&event)
{
    if (event_parent) {
        event.SetString("");
        event.SetEventObject(event_parent);
        wxPostEvent(event_parent, event);
        event.Skip();
    }
}

void PrintErrorDialog::on_webrequest_state(wxWebRequestEvent& evt)
{
    BOOST_LOG_TRIVIAL(trace) << "monitor: monitor_panel web request state = " << evt.GetState();
    switch (evt.GetState()) {
    case wxWebRequest::State_Completed: {
            wxImage img(*evt.GetResponse().GetStream());
            wxImage resize_img = img.Scale(FromDIP(320), FromDIP(180), wxIMAGE_QUALITY_HIGH);
            wxBitmap error_prompt_pic = resize_img;
            m_error_prompt_pic_static->SetBitmap(error_prompt_pic);
            Layout();
            Fit();

        break;
    }
    case wxWebRequest::State_Failed:
    case wxWebRequest::State_Cancelled:
    case wxWebRequest::State_Unauthorized: {
        m_error_prompt_pic_static->SetBitmap(wxBitmap());
        break;
    }
    case wxWebRequest::State_Active:
    case wxWebRequest::State_Idle: break;
    default: break;
    }
}

void PrintErrorDialog::update_text_image(const wxString& text, const wxString& error_code, const wxString& image_url)
{
    //if (!m_sizer_text_release_note) {
    //    m_sizer_text_release_note = new wxBoxSizer(wxVERTICAL);
    //}
    wxBoxSizer* sizer_text_release_note = new wxBoxSizer(wxVERTICAL);

    wxString error_code_msg = error_code;
    if (!error_code.IsEmpty()) {
        wxDateTime now       = wxDateTime::Now();
        wxString  show_time = now.Format("%H%M%d");
        error_code_msg = wxString::Format("[%S %S]", error_code, show_time);
    }

    if (!m_staticText_release_note) {
        m_staticText_release_note = new Label(m_vebview_release_note, text, LB_AUTO_WRAP);
        sizer_text_release_note->Add(m_error_prompt_pic_static, 0, wxALIGN_CENTER, FromDIP(5));
        sizer_text_release_note->AddSpacer(10);
        sizer_text_release_note->Add(m_staticText_release_note, 0, wxALIGN_CENTER , FromDIP(5));
    }
    if (!m_staticText_error_code) {
        m_staticText_error_code = new Label(m_vebview_release_note, error_code_msg, LB_AUTO_WRAP);
        sizer_text_release_note->AddSpacer(5);
        sizer_text_release_note->Add(m_staticText_error_code, 0, wxALIGN_CENTER, FromDIP(5));
    }

    m_vebview_release_note->SetSizer(sizer_text_release_note);

    if (!image_url.empty()) {
        const wxImage& img = wxGetApp().get_hms_query()->query_image_from_local(image_url);
        if (!img.IsOk() && image_url.Contains("http"))
        {
            web_request = wxWebSession::GetDefault().CreateRequest(this, image_url);
            //BOOST_LOG_TRIVIAL(trace) << "monitor: create new webrequest, state = " << web_request.GetState() << ", url = " << image_url;
            if (web_request.GetState() == wxWebRequest::State_Idle) web_request.Start();
            //BOOST_LOG_TRIVIAL(trace) << "monitor: start new webrequest, state = " << web_request.GetState() << ", url = " << image_url;
        }
        else
        {
            const wxImage& resize_img = img.Scale(FromDIP(320), FromDIP(180), wxIMAGE_QUALITY_HIGH);
            m_error_prompt_pic_static->SetBitmap(wxBitmap(resize_img));

            Layout();
            Fit();
        }

        m_error_prompt_pic_static->Show();

    }
    else {
        m_error_prompt_pic_static->Hide();
    }
    sizer_text_release_note->Layout();
    m_staticText_release_note->SetMaxSize(wxSize(FromDIP(300), -1));
    m_staticText_release_note->SetMinSize(wxSize(FromDIP(300), -1));
    m_staticText_release_note->SetLabelText(text);
    m_staticText_error_code->SetMaxSize(wxSize(FromDIP(300), -1));
    m_staticText_error_code->SetMinSize(wxSize(FromDIP(300), -1));
    m_staticText_error_code->SetLabelText(error_code_msg);
    m_vebview_release_note->Layout();

    auto text_size = m_staticText_release_note->GetBestSize();
    if (text_size.y < FromDIP(360))
        if (!image_url.empty()) {
            m_vebview_release_note->SetMinSize(wxSize(FromDIP(320), text_size.y + FromDIP(220)));
        }
        else {
            m_vebview_release_note->SetMinSize(wxSize(FromDIP(320), text_size.y + FromDIP(50)));
        }
    else {
        m_vebview_release_note->SetMinSize(wxSize(FromDIP(320), FromDIP(340)));
    }

    Layout();
    Fit();
}

void PrintErrorDialog::on_show()
{
    wxGetApp().UpdateFrameDarkUI(this);

    this->Show();
    this->Raise();
}

void PrintErrorDialog::on_hide()
{
    //m_sizer_button->Clear();
    //m_sizer_button->Layout();
    //m_used_button.clear();
    this->Hide();
    if (web_request.IsOk() && web_request.GetState() == wxWebRequest::State_Active) {
        BOOST_LOG_TRIVIAL(info) << "web_request: cancelled";
        web_request.Cancel();
    }
    m_error_prompt_pic_static->SetBitmap(wxBitmap());

    if (wxGetApp().mainframe != nullptr) {
        wxGetApp().mainframe->Show();
        wxGetApp().mainframe->Raise();
    }
}

void PrintErrorDialog::update_title_style(wxString title, std::vector<int> button_style, wxWindow* parent)
{
    SetTitle(title);
    event_parent = parent;
    for (int used_button_id : m_used_button) {
        if (m_button_list.find(used_button_id) != m_button_list.end()) {
            m_button_list[used_button_id]->Hide();
        }
    }

    m_sizer_button->Clear();
    m_used_button = button_style;
    bool need_remove_close_btn = false;
    for (int button_id : button_style) {
        if (m_button_list.find(button_id) != m_button_list.end()) {
            m_sizer_button->Add(m_button_list[button_id], 0, wxALL, FromDIP(5));
            m_button_list[button_id]->Show();
        }

        need_remove_close_btn |= (button_id == REMOVE_CLOSE_BTN); // special case, do not show close button
    }

    // Special case, do not show close button
    if (need_remove_close_btn)
    {
        SetWindowStyle(GetWindowStyle() & ~wxCLOSE_BOX);
    }
    else
    {
        SetWindowStyle(GetWindowStyle() | wxCLOSE_BOX);
    }

    Layout();
    Fit();
}

void PrintErrorDialog::init_button(PrintErrorButton style,wxString buton_text)
{
    Button* print_error_button = new Button(this, buton_text);
    print_error_button->SetBackgroundColor(btn_bg_white);
    print_error_button->SetBorderColor(wxColour(38, 46, 48));
    print_error_button->SetFont(Label::Body_14);
    print_error_button->SetSize(wxSize(FromDIP(300), FromDIP(30)));
    print_error_button->SetMinSize(wxSize(FromDIP(300), FromDIP(30)));
    print_error_button->SetMaxSize(wxSize(-1, FromDIP(30)));
    print_error_button->SetCornerRadius(FromDIP(5));
    print_error_button->Hide();
    m_button_list[style] = print_error_button;
    m_button_list[style]->Bind(wxEVT_LEFT_DOWN, [this, style](wxMouseEvent& e)
    {
        wxCommandEvent evt(EVT_ERROR_DIALOG_BTN_CLICKED);
        evt.SetInt(style);
        post_event(evt);
        e.Skip();
    });
}

void PrintErrorDialog::init_button_list()
{
    init_button(RESUME_PRINTING, _L("Resume Printing"));
    init_button(RESUME_PRINTING_DEFECTS, _L("Resume (defects acceptable)"));
    init_button(RESUME_PRINTING_PROBELM_SOLVED, _L("Resume (problem solved)"));
    init_button(STOP_PRINTING, _L("Stop Printing"));// pop up recheck dialog?
    init_button(CHECK_ASSISTANT, _L("Check Assistant"));
    init_button(FILAMENT_EXTRUDED, _L("Filament Extruded, Continue"));
    init_button(RETRY_FILAMENT_EXTRUDED, _L("Not Extruded Yet, Retry"));
    init_button(CONTINUE, _L("Finished, Continue"));
    init_button(LOAD_VIRTUAL_TRAY, _L("Load Filament"));
    init_button(OK_BUTTON, _L("OK"));
    init_button(FILAMENT_LOAD_RESUME, _L("Filament Loaded, Resume"));
    init_button(JUMP_TO_LIVEVIEW, _L("View Liveview"));
    init_button(NO_REMINDER_NEXT_TIME, _L("No Reminder Next Time"));
    init_button(IGNORE_NO_REMINDER_NEXT_TIME, _L("Ignore. Don't Remind Next Time"));
    init_button(IGNORE_RESUME, _L("Ignore this and Resume"));
    init_button(PROBLEM_SOLVED_RESUME, _L("Problem Solved and Resume"));
    init_button(TURN_OFF_FIRE_ALARM, _L("Got it, Turn off the Fire Alarm."));
    init_button(RETRY_PROBLEM_SOLVED, _L("Retry (problem solved)"));
    init_button(STOP_DRYING, _L("Stop Drying"));
}

PrintErrorDialog::~PrintErrorDialog()
{

}

void PrintErrorDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    rescale();
}

void PrintErrorDialog::msw_rescale() {
    wxGetApp().UpdateFrameDarkUI(this);
    Refresh();
}

void PrintErrorDialog::rescale()
{
    for (auto used_button : m_used_button) {
        if (m_button_list.find(used_button) != m_button_list.end()) {
            m_button_list[used_button]->Rescale();
        }
     }
}

ConfirmBeforeSendDialog::ConfirmBeforeSendDialog(wxWindow* parent, wxWindowID id, const wxString& title, enum ButtonStyle btn_style, const wxPoint& pos, const wxSize& size, long style, bool not_show_again_check)
    :DPIDialog(parent, id, title, pos, size, style)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(400), 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(5));

    wxBoxSizer* m_sizer_right = new wxBoxSizer(wxVERTICAL);

    m_sizer_right->Add(0, 0, 1, wxTOP, FromDIP(15));

    m_vebview_release_note = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_vebview_release_note->SetScrollRate(0, 5);
    m_vebview_release_note->SetBackgroundColour(*wxWHITE);
    m_vebview_release_note->SetMinSize(wxSize(FromDIP(400), FromDIP(380)));
    m_sizer_right->Add(m_vebview_release_note, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(15));


    auto bottom_sizer = new wxBoxSizer(wxVERTICAL);
    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));


    if (not_show_again_check) {
        auto checkbox_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_show_again_checkbox = new wxCheckBox(this, wxID_ANY, _L("Don't show again"), wxDefaultPosition, wxDefaultSize, 0);
        m_show_again_checkbox->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, [this](wxCommandEvent& e) {
            not_show_again = !not_show_again;
            m_show_again_checkbox->SetValue(not_show_again);
        });
        checkbox_sizer->Add(FromDIP(15), 0, 0, 0);
        checkbox_sizer->Add(m_show_again_checkbox, 0, wxALL, FromDIP(5));
        bottom_sizer->Add(checkbox_sizer, 0, wxBOTTOM | wxEXPAND, 0);
    }
    m_button_ok = new Button(this, _L("Confirm"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(*wxWHITE);
    m_button_ok->SetTextColor(wxColour("#FFFFFE"));
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(-1, FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetCornerRadius(FromDIP(12));

    m_button_ok->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        wxCommandEvent evt(EVT_SECONDARY_CHECK_CONFIRM, GetId());
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(evt);
        this->on_hide();
    });

    m_button_cancel = new Button(this, _L("Cancel"));
    m_button_cancel->SetBackgroundColor(btn_bg_white);
    m_button_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_cancel->SetFont(Label::Body_12);
    m_button_cancel->SetSize(wxSize(-1, FromDIP(24)));
    m_button_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetCornerRadius(FromDIP(12));

    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        wxCommandEvent evt(EVT_SECONDARY_CHECK_CANCEL);
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(evt);
        this->on_hide();
        });

    if (btn_style != CONFIRM_AND_CANCEL)
        m_button_cancel->Hide();
    else
        m_button_cancel->Show();

    m_button_update_nozzle = new Button(this, _L("Confirm and Update Nozzle"));
    m_button_update_nozzle->SetBackgroundColor(btn_bg_white);
    m_button_update_nozzle->SetBorderColor(wxColour(38, 46, 48));
    m_button_update_nozzle->SetFont(Label::Body_12);
    m_button_update_nozzle->SetSize(wxSize(-1, FromDIP(24)));
    m_button_update_nozzle->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_update_nozzle->SetCornerRadius(FromDIP(12));

    m_button_update_nozzle->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        wxCommandEvent evt(EVT_UPDATE_NOZZLE);
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(evt);
        this->on_hide();
    });

    m_button_update_nozzle->Hide();

    sizer_button->AddStretchSpacer();
    sizer_button->Add(m_button_ok, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_update_nozzle, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_cancel, 0, wxALL, FromDIP(5));
    sizer_button->Add(FromDIP(5),0, 0, 0);
    bottom_sizer->Add(sizer_button, 0, wxEXPAND | wxRIGHT | wxLEFT, 0);


    m_sizer_right->Add(bottom_sizer, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(20));
    m_sizer_right->Add(0, 0, 0, wxTOP, FromDIP(10));

    m_sizer_main->Add(m_sizer_right, 0, wxBOTTOM | wxEXPAND, FromDIP(5));

    Bind(wxEVT_CLOSE_WINDOW, [this](auto& e) {this->on_hide(); });

    SetSizer(m_sizer_main);
    Layout();
    m_sizer_main->Fit(this);

    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void ConfirmBeforeSendDialog::update_text(wxString text)
{
    wxBoxSizer* sizer_text_release_note = new wxBoxSizer(wxVERTICAL);
    if (!m_staticText_release_note){
        m_staticText_release_note = new Label(m_vebview_release_note, text, LB_AUTO_WRAP);
        wxBoxSizer* top_blank_sizer = new wxBoxSizer(wxVERTICAL);
        wxBoxSizer* bottom_blank_sizer = new wxBoxSizer(wxVERTICAL);
        top_blank_sizer->Add(FromDIP(5), 0, wxALIGN_CENTER | wxALL, FromDIP(5));
        bottom_blank_sizer->Add(FromDIP(5), 0, wxALIGN_CENTER | wxALL, FromDIP(5));

        sizer_text_release_note->Add(top_blank_sizer, 0, wxALIGN_CENTER | wxALL, FromDIP(5));
        sizer_text_release_note->Add(m_staticText_release_note, 0, wxALIGN_CENTER, FromDIP(5));
        sizer_text_release_note->Add(bottom_blank_sizer, 0, wxALIGN_CENTER | wxALL, FromDIP(5));
        m_vebview_release_note->SetSizer(sizer_text_release_note);
    }
    m_staticText_release_note->SetMaxSize(wxSize(FromDIP(380), -1));
    m_staticText_release_note->SetMinSize(wxSize(FromDIP(380), -1));
    m_staticText_release_note->SetLabelText(text);
    m_vebview_release_note->Layout();

    auto text_size = m_staticText_release_note->GetBestSize();
    if (text_size.y < FromDIP(380))
        m_vebview_release_note->SetMinSize(wxSize(FromDIP(400), text_size.y + FromDIP(25)));
    else {
        m_vebview_release_note->SetMinSize(wxSize(FromDIP(400), FromDIP(380)));
    }

    Layout();
    Fit();
}

void ConfirmBeforeSendDialog::update_text(std::vector<ConfirmBeforeSendInfo> texts, bool enable_warning_clr /*= true*/)
{
    wxBoxSizer* sizer_text_release_note = new wxBoxSizer(wxVERTICAL);
    m_vebview_release_note->SetSizer(sizer_text_release_note);


    auto height = 0;
    for (auto text : texts) {

        Label* label_item = nullptr;
        if (text.wiki_url.empty())
        {
            label_item = new Label(m_vebview_release_note, text.text, LB_AUTO_WRAP);
        }
        else
        {
            label_item = new Label(m_vebview_release_note, text.text + " " + _L("Please refer to Wiki before use->"), LB_AUTO_WRAP);
            label_item->Bind(wxEVT_LEFT_DOWN, [this, text](wxMouseEvent& e) { wxLaunchDefaultBrowser(text.wiki_url);});
            label_item->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
            label_item->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });
        }

        if (enable_warning_clr && text.level == ConfirmBeforeSendInfo::InfoLevel::Warning)
        {
            label_item->SetForegroundColour(wxColour(0xFF, 0x6F, 0x00));
        }

        label_item->SetMaxSize(wxSize(FromDIP(494), -1));
        label_item->SetMinSize(wxSize(FromDIP(494), -1));
        label_item->Wrap(FromDIP(494));
        label_item->Layout();

        sizer_text_release_note->Add(label_item, 0, wxALIGN_CENTER | wxALL, FromDIP(3));
        height += label_item->GetSize().y;
    }

    m_vebview_release_note->Layout();
    if (height < FromDIP(500))
        m_vebview_release_note->SetMinSize(wxSize(-1, height + FromDIP(25)));
    else {
        m_vebview_release_note->SetMinSize(wxSize(-1, FromDIP(500)));
    }

    Layout();
    Fit();
}

void ConfirmBeforeSendDialog::on_show()
{
    wxGetApp().UpdateDlgDarkUI(this);
    // recover button color
    wxMouseEvent evt_ok(wxEVT_LEFT_UP);
    m_button_ok->GetEventHandler()->ProcessEvent(evt_ok);
    wxMouseEvent evt_cancel(wxEVT_LEFT_UP);
    m_button_cancel->GetEventHandler()->ProcessEvent(evt_cancel);
    CenterOnScreen();
    this->ShowModal();
}

void ConfirmBeforeSendDialog::on_hide()
{
    if (m_show_again_checkbox != nullptr && not_show_again && show_again_config_text != "")
        wxGetApp().app_config->set(show_again_config_text, "1");
    EndModal(wxID_OK);
}

void ConfirmBeforeSendDialog::update_btn_label(wxString ok_btn_text, wxString cancel_btn_text)
{
    m_button_ok->SetLabel(ok_btn_text);
    m_button_cancel->SetLabel(cancel_btn_text);
    rescale();
}

wxString ConfirmBeforeSendDialog::format_text(wxString str, int warp)
{
    Label st (this, str);
    wxString out_txt      = str;
    wxString count_txt    = "";
    int      new_line_pos = 0;

    for (int i = 0; i < str.length(); i++) {
        auto text_size = st.GetTextExtent(count_txt);
        if (text_size.x < warp) {
            count_txt += str[i];
        } else {
            out_txt.insert(i - 1, '\n');
            count_txt = "";
        }
    }
    return out_txt;
}

ConfirmBeforeSendDialog::~ConfirmBeforeSendDialog()
{

}

void ConfirmBeforeSendDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    rescale();
}

void ConfirmBeforeSendDialog::show_update_nozzle_button(bool show)
{
    m_button_update_nozzle->Show(show);
    Layout();
}

void ConfirmBeforeSendDialog::hide_button_ok()
{
    m_button_ok->Hide();
}

void ConfirmBeforeSendDialog::edit_cancel_button_txt(const wxString& txt, bool switch_green)
{
    m_button_cancel->SetLabel(txt);

    if (switch_green)
    {
        StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
                                std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                                std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
        m_button_cancel->SetBackgroundColor(btn_bg_green);
        m_button_cancel->SetBorderColor(*wxWHITE);
        m_button_cancel->SetTextColor(wxColour("#FFFFFE"));
    }
}

void ConfirmBeforeSendDialog::disable_button_ok()
{
    m_button_ok->Disable();
    m_button_ok->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
    m_button_ok->SetBorderColor(wxColour(0x90, 0x90, 0x90));
}

void ConfirmBeforeSendDialog::enable_button_ok()
{
    m_button_ok->Enable();
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(btn_bg_green);
}

void ConfirmBeforeSendDialog::rescale()
{
    m_button_ok->Rescale();
    m_button_cancel->Rescale();
}

static void nop_deleter(InputIpAddressDialog*) {}

InputIpAddressDialog::InputIpAddressDialog(wxWindow *parent)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe),
                wxID_ANY,
                _L("Connect the printer using IP and access code"),
                wxDefaultPosition,
                wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    m_result                       = -1;
    wxBoxSizer *m_sizer_border       = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *m_sizer_body       = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *m_sizer_msg        = new wxBoxSizer(wxHORIZONTAL);
    auto        m_line_top         = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));

    comfirm_before_check_text = _L("Try the following methods to update the connection parameters and reconnect to the printer.");
    comfirm_before_enter_text = _L("1. Please confirm Bambu Studio and your printer are in the same LAN.");
    comfirm_after_enter_text  = _L("2. If the IP and Access Code below are different from the actual values on your printer, please correct them.");
    comfirm_last_enter_text   = _L("3. Please obtain the device SN from the printer side; it is usually found in the device information on the printer screen.");

    Label *wiki = new Label(this, ::Label::Body_13, _L("View wiki"), LB_AUTO_WRAP);
    wiki->SetForegroundColour(wxColour(0, 174, 66));
    wiki->Bind(wxEVT_ENTER_WINDOW, [this](auto &e) {SetCursor(wxCURSOR_HAND);});
    wiki->Bind(wxEVT_LEAVE_WINDOW, [this](auto &e) {SetCursor(wxCURSOR_ARROW);});
    wiki->Bind(wxEVT_LEFT_DOWN, [this](auto &e) {
        wxString url;
        if (wxGetApp().app_config->get("region") =="China") {
            url = "https://wiki.bambulab.com/zh/software/bambu-studio/failed-to-send-print-files";
        }
        else {
            url = "https://wiki.bambulab.com/en/software/bambu-studio/failed-to-send-print-files";
        }
        wxLaunchDefaultBrowser(url);
    });

    m_tip0 = new Label(this, ::Label::Body_13, comfirm_before_check_text, LB_AUTO_WRAP);
    m_tip0->SetMinSize(wxSize(FromDIP(355), -1));
    m_tip0->SetMaxSize(wxSize(FromDIP(355), -1));
    m_tip0->Wrap(FromDIP(355));

    m_tip1 = new Label(this, ::Label::Body_13, comfirm_before_enter_text, LB_AUTO_WRAP);
    m_tip1->SetMinSize(wxSize(FromDIP(355), -1));
    m_tip1->SetMaxSize(wxSize(FromDIP(355), -1));
    m_tip1->Wrap(FromDIP(355));

    m_tip2 = new Label(this, ::Label::Body_13, comfirm_after_enter_text, LB_AUTO_WRAP);
    m_tip2->SetMinSize(wxSize(FromDIP(355), -1));
    m_tip2->SetMaxSize(wxSize(FromDIP(355), -1));

    m_tip3 = new Label(this, ::Label::Body_13, comfirm_last_enter_text, LB_AUTO_WRAP);
    m_tip3->SetMinSize(wxSize(FromDIP(355), -1));
    m_tip3->SetMaxSize(wxSize(FromDIP(355), -1));

    ip_input_top_panel = new wxPanel(this);
    ip_input_bot_panel = new wxPanel(this);

    ip_input_top_panel->SetBackgroundColour(*wxWHITE);
    ip_input_bot_panel->SetBackgroundColour(*wxWHITE);

    auto m_input_top_sizer = new wxBoxSizer(wxVERTICAL);
    auto m_input_bot_sizer = new wxBoxSizer(wxVERTICAL);

    /*top input*/
    auto m_input_tip_area = new wxBoxSizer(wxHORIZONTAL);
    auto m_input_area     = new wxBoxSizer(wxHORIZONTAL);

    m_tips_ip = new Label(ip_input_top_panel, "IP");
    m_tips_ip->SetMinSize(wxSize(FromDIP(168), -1));
    m_tips_ip->SetMaxSize(wxSize(FromDIP(168), -1));

    m_input_ip = new TextInput(ip_input_top_panel, wxEmptyString, wxEmptyString);
    m_input_ip->Bind(wxEVT_TEXT, &InputIpAddressDialog::on_text, this);
    m_input_ip->SetMinSize(wxSize(FromDIP(168), FromDIP(28)));
    m_input_ip->SetMaxSize(wxSize(FromDIP(168), FromDIP(28)));

    m_tips_access_code = new Label(ip_input_top_panel, _L("Access Code"));
    m_tips_access_code->SetMinSize(wxSize(FromDIP(168), -1));
    m_tips_access_code->SetMaxSize(wxSize(FromDIP(168), -1));

    m_input_access_code = new TextInput(ip_input_top_panel, wxEmptyString, wxEmptyString);
    m_input_access_code->Bind(wxEVT_TEXT, &InputIpAddressDialog::on_text, this);
    m_input_access_code->SetMinSize(wxSize(FromDIP(168), FromDIP(28)));
    m_input_access_code->SetMaxSize(wxSize(FromDIP(168), FromDIP(28)));

    m_input_tip_area->Add(m_tips_ip, 0, wxALIGN_CENTER, 0);
    m_input_tip_area->Add(0, 0, 0, wxLEFT, FromDIP(20));
    m_input_tip_area->Add(m_tips_access_code, 0, wxALIGN_CENTER, 0);

    m_input_area->Add(m_input_ip, 0, wxALIGN_CENTER, 0);
    m_input_area->Add(0, 0, 0, wxLEFT, FromDIP(20));
    m_input_area->Add(m_input_access_code, 0, wxALIGN_CENTER, 0);

    m_input_top_sizer->Add(m_input_tip_area, 0, wxRIGHT | wxEXPAND, FromDIP(18));
    m_input_top_sizer->Add(0, 0, 0, wxTOP, FromDIP(4));
    m_input_top_sizer->Add(m_input_area, 0, wxRIGHT | wxEXPAND, FromDIP(18));

    ip_input_top_panel->SetSizer(m_input_top_sizer);
    ip_input_top_panel->Layout();
    ip_input_top_panel->Fit();

    /*bom input*/
    auto m_input_sn_area      = new wxBoxSizer(wxHORIZONTAL);
    auto m_input_modelID_area = new wxBoxSizer(wxHORIZONTAL);

    m_tips_sn = new Label(ip_input_bot_panel, "SN");
    m_tips_sn->SetMinSize(wxSize(FromDIP(168), -1));
    m_tips_sn->SetMaxSize(wxSize(FromDIP(168), -1));

    m_input_sn = new TextInput(ip_input_bot_panel, wxEmptyString, wxEmptyString);
    m_input_sn->Bind(wxEVT_TEXT, &InputIpAddressDialog::on_text, this);
    m_input_sn->SetMinSize(wxSize(FromDIP(168), FromDIP(28)));
    m_input_sn->SetMaxSize(wxSize(FromDIP(168), FromDIP(28)));

    m_tips_modelID = new Label(ip_input_bot_panel, _L("Printer model"));
    m_tips_modelID->SetMinSize(wxSize(FromDIP(168), -1));
    m_tips_modelID->SetMaxSize(wxSize(FromDIP(168), -1));

    m_input_modelID = new ComboBox(ip_input_bot_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(168), FromDIP(28)), 0, nullptr, wxCB_READONLY);
    // m_input_modelID->Bind(wxEVT_TEXT, &InputIpAddressDialog::on_text, this);
    m_input_modelID->SetMinSize(wxSize(FromDIP(168), FromDIP(28)));
    m_input_modelID->SetMaxSize(wxSize(FromDIP(168), FromDIP(28)));

    m_models_map = DevPrinterConfigUtil::get_all_model_id_with_name();
    for (auto it = m_models_map.begin(); it != m_models_map.end(); ++it) {
        m_input_modelID->Append(it->first);
        m_input_modelID->SetSelection(0);
    }

    m_input_sn_area->Add(m_tips_sn, 0, wxALIGN_CENTER, 0);
    m_input_sn_area->Add(0, 0, 0, wxLEFT, FromDIP(20));
    m_input_sn_area->Add(m_tips_modelID, 0, wxALIGN_CENTER, 0);

    m_input_modelID_area->Add(m_input_sn, 0, wxALIGN_CENTER, 0);
    m_input_modelID_area->Add(0, 0, 0, wxLEFT, FromDIP(20));
    m_input_modelID_area->Add(m_input_modelID, 0, wxALIGN_CENTER, 0);

    m_input_bot_sizer->Add(m_input_sn_area, 0,  wxEXPAND, 0);
    m_input_bot_sizer->Add(0, 0, 0, wxTOP, FromDIP(4));
    m_input_bot_sizer->Add(m_input_modelID_area, 0, wxEXPAND, 0);

    ip_input_bot_panel->SetSizer(m_input_bot_sizer);
    ip_input_bot_panel->Layout();
    ip_input_bot_panel->Fit();

    /*other*/
    m_test_right_msg = new Label(this, Label::Body_13, wxEmptyString, LB_AUTO_WRAP);
    m_test_right_msg->SetForegroundColour(wxColour(61, 203, 115));
    m_test_right_msg->Hide();

    m_test_wrong_msg = new Label(this, Label::Body_13, wxEmptyString, LB_AUTO_WRAP);
    m_test_wrong_msg->SetForegroundColour(wxColour(208, 27, 27));
    m_test_wrong_msg->Hide();

    m_tip4 = new Label(this, Label::Body_12, _L("Where to find your printer's IP and Access Code?"), LB_AUTO_WRAP);
    m_tip4->SetMinSize(wxSize(FromDIP(355), -1));
    m_tip4->SetMaxSize(wxSize(FromDIP(355), -1));

    m_trouble_shoot = new wxHyperlinkCtrl(this, wxID_ANY, "How to trouble shooting", "");

    m_img_help = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("input_access_code_x1_en", this, 198), wxDefaultPosition, wxSize(FromDIP(355), -1), 0);

    auto m_sizer_button = new wxBoxSizer(wxHORIZONTAL);

    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                            std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

    m_button_ok = new Button(this, _L("Connect"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(*wxWHITE);
    m_button_ok->SetTextColor(wxColour("#FFFFFE"));
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetCornerRadius(FromDIP(12));
    m_button_ok->Bind(wxEVT_LEFT_DOWN, &InputIpAddressDialog::on_ok, this);
    m_button_ok->Enable(false);
    m_button_ok->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
    m_button_ok->SetBorderColor(wxColour(0x90, 0x90, 0x90));

    m_sizer_button->AddStretchSpacer();
    m_sizer_button->Add(m_button_ok, 0, wxALL, FromDIP(5));
    // m_sizer_button->Add(m_button_cancel, 0, wxALL, FromDIP(5));
    m_sizer_button->Layout();

    m_status_bar = std::make_shared<BBLStatusBarSend>(this);
    m_status_bar->get_panel()->Hide();

    auto m_step_icon_panel1 = new wxWindow(this, wxID_ANY);
    auto m_step_icon_panel2 = new wxWindow(this, wxID_ANY);
    m_step_icon_panel3      = new wxWindow(this, wxID_ANY);

    m_step_icon_panel1->SetBackgroundColour(*wxWHITE);
    m_step_icon_panel2->SetBackgroundColour(*wxWHITE);
    m_step_icon_panel3->SetBackgroundColour(*wxWHITE);

    auto m_sizer_step_icon_panel1 = new wxBoxSizer(wxVERTICAL);
    auto m_sizer_step_icon_panel2 = new wxBoxSizer(wxVERTICAL);
    auto m_sizer_step_icon_panel3 = new wxBoxSizer(wxVERTICAL);

    m_img_step1 = new wxStaticBitmap(m_step_icon_panel1, wxID_ANY, create_scaled_bitmap("ip_address_step", this, 6), wxDefaultPosition, wxSize(FromDIP(6), FromDIP(6)), 0);
    m_img_step2 = new wxStaticBitmap(m_step_icon_panel2, wxID_ANY, create_scaled_bitmap("ip_address_step", this, 6), wxDefaultPosition, wxSize(FromDIP(6), FromDIP(6)), 0);
    m_img_step3 = new wxStaticBitmap(m_step_icon_panel3, wxID_ANY, create_scaled_bitmap("ip_address_step", this, 6), wxDefaultPosition, wxSize(FromDIP(6), FromDIP(6)), 0);

    m_step_icon_panel1->SetSizer(m_sizer_step_icon_panel1);
    m_step_icon_panel1->Layout();
    m_step_icon_panel1->Fit();

    m_step_icon_panel2->SetSizer(m_sizer_step_icon_panel2);
    m_step_icon_panel2->Layout();
    m_step_icon_panel2->Fit();

    m_step_icon_panel3->SetSizer(m_sizer_step_icon_panel3);
    m_step_icon_panel3->Layout();
    m_step_icon_panel3->Fit();

    m_sizer_step_icon_panel1->Add(m_img_step1, 0, wxALIGN_CENTER | wxALL, FromDIP(5));
    m_sizer_step_icon_panel2->Add(m_img_step2, 0, wxALIGN_CENTER | wxALL, FromDIP(5));
    m_sizer_step_icon_panel3->Add(m_img_step3, 0, wxALIGN_CENTER | wxALL, FromDIP(5));

    m_step_icon_panel1->SetMinSize(wxSize(-1, m_tip1->GetBestSize().y));
    m_step_icon_panel1->SetMaxSize(wxSize(-1, m_tip1->GetBestSize().y));

    m_step_icon_panel2->SetMinSize(wxSize(-1, m_tip2->GetBestSize().y));
    m_step_icon_panel2->SetMaxSize(wxSize(-1, m_tip2->GetBestSize().y));


    m_sizer_msg->Layout();


    m_trouble_shoot->Hide();

    m_sizer_main->Add(wiki, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(4));
    m_sizer_main->Add(m_tip0, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(6));
    m_sizer_main->Add(m_tip1, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(20));
    m_sizer_main->Add(m_tip2, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(2));
    m_sizer_main->Add(m_tip3, 0, wxTOP|wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(12));
    m_sizer_main->Add(m_tip4, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(3));
    m_sizer_main->Add(m_img_help, 0, 0, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(12));
    m_sizer_main->Add(ip_input_top_panel, 0,wxEXPAND, 0);
    m_sizer_main->Add(ip_input_bot_panel, 0,wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(4));
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(4));
    m_sizer_main->Add(m_test_right_msg, 0, wxEXPAND, 0);
    m_sizer_main->Add(m_test_wrong_msg, 0, wxEXPAND, 0);

    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(4));
    m_sizer_main->Add(m_status_bar->get_panel(), 0, wxEXPAND, 0);
    m_sizer_main->Layout();

    m_sizer_body->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_body->Add(0, 0, 0, wxTOP, FromDIP(10));
    m_sizer_body->Add(m_sizer_main, 0, wxLEFT|wxRIGHT|wxEXPAND, FromDIP(20));
    m_sizer_body->Add(0, 0, 0, wxTOP, FromDIP(4));
    m_sizer_body->Add(m_sizer_msg, 0, wxLEFT|wxRIGHT|wxEXPAND, FromDIP(20));
    m_sizer_body->Add(0, 0, 0, wxTOP, FromDIP(4));
    m_sizer_body->Add(m_trouble_shoot, 0, wxLEFT|wxRIGHT|wxEXPAND, FromDIP(20));
    m_sizer_body->Add(0, 0, 0, wxTOP, FromDIP(8));
    m_sizer_body->Add(m_sizer_button, 0, wxLEFT|wxRIGHT|wxEXPAND, FromDIP(20));
    m_sizer_body->Add(0, 0, 0, wxTOP, FromDIP(10));
    m_sizer_body->Layout();

    m_sizer_border->Add(0,0,0,wxLEFT, FromDIP(20));
    m_sizer_border->Add(m_sizer_body, wxEXPAND, 0);

    switch_input_panel(0);

    SetSizer(m_sizer_border);
    Layout();
    Fit();

    CentreOnParent(wxBOTH);
    Move(wxPoint(GetScreenPosition().x, GetScreenPosition().y - FromDIP(50)));
    wxGetApp().UpdateDlgDarkUI(this);

    closeTimer = new wxTimer();
    closeTimer->SetOwner(this);
    Bind(wxEVT_TIMER, &InputIpAddressDialog::OnTimer, this);

    Bind(EVT_CHECK_IP_ADDRESS_FAILED, &InputIpAddressDialog::on_check_ip_address_failed, this);

    Bind(EVT_CLOSE_IPADDRESS_DLG, [this](auto& e) {
        m_status_bar->reset();
        EndModal(wxID_YES);
    });
    Bind(wxEVT_CLOSE_WINDOW, [this](auto& e) {
        on_cancel();
        closeTimer->Stop();
    });

    Bind(EVT_UPDATE_TEXT_MSG, &InputIpAddressDialog::update_test_msg_event, this);
    Bind(EVT_CHECK_IP_ADDRESS_LAYOUT, [this](auto& e) {
        int mode = e.GetInt();
        switch_input_panel(mode);
        Layout();
        Fit();
    });
}

void InputIpAddressDialog::switch_input_panel(int index)
{
    if (index == 0) {
        ip_input_top_panel->Show();
        ip_input_bot_panel->Hide();
        m_step_icon_panel3->Hide();
        m_tip3->Hide();
    } else {
        ip_input_top_panel->Hide();
        ip_input_bot_panel->Show();
        m_step_icon_panel3->Show();
        m_tip3->Show();

        m_button_ok->Enable(false);
        m_button_ok->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
        m_button_ok->SetBorderColor(wxColour(0x90, 0x90, 0x90));
    }
    current_input_index = index;
}

void InputIpAddressDialog::on_cancel()
{
    if (m_thread) {
        m_thread->interrupt();
        m_thread->detach();
        delete m_thread;
        m_thread = nullptr;
    }

    EndModal(wxID_CANCEL);
}


void InputIpAddressDialog::update_title(wxString title)
{
    SetTitle(title);
}

void InputIpAddressDialog::set_machine_obj(MachineObject* obj)
{
    m_obj = obj;
    m_input_ip->GetTextCtrl()->SetLabelText(m_obj->get_dev_ip());
    m_input_access_code->GetTextCtrl()->SetLabelText(m_obj->get_access_code());

    std::string img_str = DevPrinterConfigUtil::get_printer_connect_help_img(m_obj->printer_type);
    auto diagram_bmp = create_scaled_bitmap(img_str + "_en", this, 198);
    m_img_help->SetBitmap(diagram_bmp);


    auto str_ip = m_input_ip->GetTextCtrl()->GetValue();
    auto str_access_code = m_input_access_code->GetTextCtrl()->GetValue();
    if (isIp(str_ip.ToStdString()) && str_access_code.Length() == 8) {
        m_button_ok->Enable(true);
        StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
            std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
        m_button_ok->SetTextColor(StateColor::darkModeColorFor("#FFFFFE"));
        m_button_ok->SetBackgroundColor(btn_bg_green);
    }
    else {
        m_button_ok->Enable(false);
        m_button_ok->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
        m_button_ok->SetBorderColor(wxColour(0x90, 0x90, 0x90));
    }

    Layout();
    Fit();
}

void InputIpAddressDialog::update_test_msg(wxString msg,bool connected)
{
    if (msg.empty()) {
        m_test_right_msg->Hide();
        m_test_wrong_msg->Hide();
    }
    else {
         if(connected){
             m_test_right_msg->Show();
             m_test_right_msg->SetLabelText(msg);
             m_test_right_msg->SetMinSize(wxSize(FromDIP(355), -1));
             m_test_right_msg->SetMaxSize(wxSize(FromDIP(355), -1));
         }
         else{
             m_test_wrong_msg->Show();
             m_test_wrong_msg->SetLabelText(msg);
             m_test_wrong_msg->SetMinSize(wxSize(FromDIP(355), -1));
             m_test_wrong_msg->SetMaxSize(wxSize(FromDIP(355), -1));
         }
    }

    Layout();
    Fit();
}

bool InputIpAddressDialog::isIp(std::string ipstr)
{
    istringstream ipstream(ipstr);
    int num[4];
    char point[3];
    string end;
    ipstream >> num[0] >> point[0] >> num[1] >> point[1] >> num[2] >> point[2] >> num[3] >> end;
    for (int i = 0; i < 3; ++i) {
        if (num[i] < 0 || num[i]>255) return false;
        if (point[i] != '.') return false;
    }
    if (num[3] < 0 || num[3]>255) return false;
    if (!end.empty()) return false;
    return true;
}

void InputIpAddressDialog::on_ok(wxMouseEvent& evt)
{
    if (!m_need_input_sn) {
        on_send_retry();
        return;
    }

    m_test_right_msg->Hide();
    m_test_wrong_msg->Hide();
    m_trouble_shoot->Hide();
    std::string str_ip = m_input_ip->GetTextCtrl()->GetValue().ToStdString();
    std::string str_access_code = m_input_access_code->GetTextCtrl()->GetValue().ToStdString();
    std::string str_sn = m_input_sn->GetTextCtrl()->GetValue().ToStdString();
    std::string str_model_id = "";

    auto it = m_models_map.find(m_input_modelID->GetStringSelection().ToStdString());
    if (it != m_models_map.end()) {
        str_model_id = it->second;
    }

    m_button_ok->Enable(false);
    m_button_ok->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
    m_button_ok->SetBorderColor(wxColour(0x90, 0x90, 0x90));

    Refresh();
    Layout();
    Fit();

    token_.reset(this, nop_deleter);
    m_thread = new boost::thread(boost::bind(&InputIpAddressDialog::workerThreadFunc, this, str_ip, str_access_code, str_sn, str_model_id));
}

void InputIpAddressDialog::on_send_retry()
{
    m_test_right_msg->Hide();
    m_test_wrong_msg->Hide();
    m_img_step3->Hide();
    m_tip4->Hide();
    m_trouble_shoot->Hide();
    Layout();
    Fit();
    wxString ip              = m_input_ip->GetTextCtrl()->GetValue();
    wxString str_access_code = m_input_access_code->GetTextCtrl()->GetValue();

    // check support function
    if (!m_obj) return;
    if (!m_obj->is_support_send_to_sdcard) {
        wxString input_str = wxString::Format("%s|%s", ip, str_access_code);
        auto     event     = wxCommandEvent(EVT_ENTER_IP_ADDRESS);
        event.SetString(input_str);
        event.SetEventObject(this);
        wxPostEvent(this, event);

        auto event_close = wxCommandEvent(EVT_CLOSE_IPADDRESS_DLG);
        event_close.SetEventObject(this);
        wxPostEvent(this, event_close);
        return;
    }

    m_button_ok->Enable(false);
    m_button_ok->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
    m_button_ok->SetBorderColor(wxColour(0x90, 0x90, 0x90));

    if (m_send_job) { m_send_job->join(); }

    m_status_bar->reset();
    m_status_bar->set_prog_block();
    m_status_bar->set_cancel_callback_fina([this]() {
        BOOST_LOG_TRIVIAL(info) << "print_job: enter canceled";
        if (m_send_job) {
            if (m_send_job->is_running()) {
                BOOST_LOG_TRIVIAL(info) << "send_job: canceled";
                m_send_job->cancel();
            }
            m_send_job->join();
        }
    });

    m_send_job                = std::make_shared<SendJob>(m_status_bar, wxGetApp().plater(), m_obj->get_dev_id());
    m_send_job->m_dev_ip      = ip.ToStdString();
    m_send_job->m_access_code = str_access_code.ToStdString();

#if !BBL_RELEASE_TO_PUBLIC
    m_send_job->m_local_use_ssl_for_mqtt = wxGetApp().app_config->get("enable_ssl_for_mqtt") == "true" ? true : false;
    m_send_job->m_local_use_ssl_for_ftp  = wxGetApp().app_config->get("enable_ssl_for_ftp") == "true" ? true : false;
#else
    m_send_job->m_local_use_ssl_for_mqtt = m_obj->local_use_ssl_for_mqtt;
    m_send_job->m_local_use_ssl_for_ftp  = m_obj->local_use_ssl_for_ftp;
#endif

    m_send_job->connection_type  = m_obj->connection_type();
    m_send_job->cloud_print_only = true;
    m_send_job->has_sdcard       = m_obj->GetStorage()->get_sdcard_state() == DevStorage::SdcardState::HAS_SDCARD_NORMAL;
    m_send_job->set_check_mode();
    m_send_job->set_project_name("verify_job");

    m_send_job->on_check_ip_address_fail([this](int result) {
        this->check_ip_address_failed(result);
    });

    m_send_job->on_check_ip_address_success([this, ip, str_access_code]() {
        wxString input_str = wxString::Format("%s|%s", ip, str_access_code);
        auto     event     = wxCommandEvent(EVT_ENTER_IP_ADDRESS);
        event.SetString(input_str);
        event.SetEventObject(this);
        wxPostEvent(this, event);
        m_result = 0;

        update_test_msg(_L("IP and Access Code Verified! You may close the window"), true);
    });

    m_send_job->start();
}

void InputIpAddressDialog::update_test_msg_event(wxCommandEvent& evt)
{
    wxString text = evt.GetString();
    bool beconnect = evt.GetInt();
    update_test_msg(text, beconnect);
    Layout();
    Fit();
}

void InputIpAddressDialog::post_update_test_msg(std::weak_ptr<InputIpAddressDialog> w,wxString text, bool beconnect)
{
    if (w.expired()) return;

    wxCommandEvent event(EVT_UPDATE_TEXT_MSG);
    event.SetEventObject(this);
    event.SetString(text);
    event.SetInt(beconnect);
    wxPostEvent(this, event);
}

void InputIpAddressDialog::workerThreadFunc(std::string str_ip, std::string str_access_code, std::string sn, std::string model_id)
{
    std::weak_ptr<InputIpAddressDialog> w = std::weak_ptr<InputIpAddressDialog>(token_);

    post_update_test_msg(w, _L("connecting..."), true);

    detectResult detectData;
    auto result = -1;
    if (current_input_index == 0) {

#ifdef __APPLE__
        result = -3;
#else
        result = wxGetApp().getAgent()->bind_detect(str_ip, "secure", detectData);
#endif

    } else {
        result = 0;
        detectData.dev_name = sn;
        detectData.dev_id = sn;
        detectData.connect_type = "lan";
        detectData.connect_type = "free";
    }

    if (w.expired()) return;

    if (result < 0) {
        post_update_test_msg(w, wxEmptyString, true);
        if (result == -1) {
            post_update_test_msg(w, _L("Failed to connect to printer."), false);
        }
        else if (result == -2) {
            post_update_test_msg(w, _L("Failed to publish login request."), false);
        }
        else if (result == -3) {
            wxCommandEvent event(EVT_CHECK_IP_ADDRESS_LAYOUT);
            event.SetEventObject(this);
            event.SetInt(1);
            wxPostEvent(this, event);
        }
        return;
    }

    if (detectData.connect_type != "farm") {
        if (detectData.bind_state == "occupied") {
            post_update_test_msg(w, wxEmptyString, true);
            post_update_test_msg(w, _L("The printer has already been bound."), false);
            return;
        }

        if (detectData.connect_type == "cloud") {
            post_update_test_msg(w, wxEmptyString, true);
            post_update_test_msg(w, _L("The printer mode is incorrect, please switch to LAN Only."), false);
            return;
        }
    }
    if (w.expired()) return;


    DeviceManager* dev = wxGetApp().getDeviceManager();
    m_obj = dev->insert_local_device(detectData.dev_name, detectData.dev_id, str_ip,
        detectData.connect_type, detectData.bind_state, detectData.version,
        str_access_code, detectData.model_id);


    if (w.expired()) return;

    if (m_obj) {
        m_obj->set_user_access_code(str_access_code);
        wxGetApp().getDeviceManager()->set_selected_machine(m_obj->get_dev_id());
    }


    closeCount = 1;

    post_update_test_msg(w, wxEmptyString, true);
    post_update_test_msg(w, wxString::Format(_L("Connecting to printer... The dialog will close later"), closeCount), true);

    if (w.expired()) return;

#ifdef __APPLE__
    wxCommandEvent event(EVT_CLOSE_IPADDRESS_DLG);
    wxPostEvent(this, event);
#else
    closeTimer->Start(1000);
#endif
}

void InputIpAddressDialog::OnTimer(wxTimerEvent& event) {
    if (closeCount > 0) {
        closeCount--;
    }
    else {
        closeTimer->Stop();
        EndModal(wxID_CLOSE);
    }
}

void InputIpAddressDialog::check_ip_address_failed(int result)
{
    auto evt = new wxCommandEvent(EVT_CHECK_IP_ADDRESS_FAILED);
    evt->SetInt(result);
    wxQueueEvent(this, evt);
}

void InputIpAddressDialog::on_check_ip_address_failed(wxCommandEvent& evt)
{
    m_result = evt.GetInt();
    if (m_result == -2) {
        update_test_msg(_L("Connection failed, please double check IP and Access Code"), false);
    }
    else {
        if (m_need_input_sn) {
            update_test_msg(_L("Connection failed! If your IP and Access Code is correct, \nplease move to step 3 for troubleshooting network issues"), false);
        }
        else {
            update_test_msg(_L("Connection failed! Please refer to the wiki page."), false);
        }

        Layout();
        Fit();
    }

    m_button_ok->Enable(true);
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
    m_button_ok->SetTextColor(StateColor::darkModeColorFor("#FFFFFE"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
}

void InputIpAddressDialog::on_text(wxCommandEvent &evt)
{
    auto str_ip              = m_input_ip->GetTextCtrl()->GetValue();
    auto str_access_code     = m_input_access_code->GetTextCtrl()->GetValue();
    auto str_sn     = m_input_sn->GetTextCtrl()->GetValue();
    bool invalid_access_code = true;

    for (char c : str_access_code) {
        if (!('0' <= c && c <= '9' || 'a' <= c && c <= 'z' || 'A' <= c && c <= 'Z')) {
            invalid_access_code = false;
            return;
        }
    }

    if (isIp(str_ip.ToStdString()) && str_access_code.Length() == 8 && invalid_access_code) {
        m_button_ok->Enable(true);
        StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                                std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
        m_button_ok->SetTextColor(StateColor::darkModeColorFor("#FFFFFE"));
        m_button_ok->SetBackgroundColor(btn_bg_green);
    } else {
        m_button_ok->Enable(false);
        m_button_ok->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
        m_button_ok->SetBorderColor(wxColour(0x90, 0x90, 0x90));
    }

    if (current_input_index == 1){
        if (str_sn.length() == 15) {
            m_button_ok->Enable(true);
            StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                                    std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
            m_button_ok->SetTextColor(StateColor::darkModeColorFor("#FFFFFE"));
            m_button_ok->SetBackgroundColor(btn_bg_green);
        } else {
            m_button_ok->Enable(false);
            m_button_ok->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
            m_button_ok->SetBorderColor(wxColour(0x90, 0x90, 0x90));
        }
    }
}

InputIpAddressDialog::~InputIpAddressDialog()
{
}

void InputIpAddressDialog::on_dpi_changed(const wxRect& suggested_rect)
{

}


 SendFailedConfirm::SendFailedConfirm(wxWindow *parent /*= nullptr*/):
     DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe),
                  wxID_ANY,
                  _L("sending failed"),
                  wxDefaultPosition,
                  wxDefaultSize,
                  wxCAPTION | wxCLOSE_BOX)
 {
     SetMinSize(wxSize(FromDIP(560), -1));
     SetMaxSize(wxSize(FromDIP(560), -1));

     std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
     SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

     SetBackgroundColour(*wxWHITE);
     auto m_sizer_main    = new wxBoxSizer(wxVERTICAL);
     auto m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(400), 1));
     m_line_top->SetBackgroundColour(wxColour(166, 169, 170));


     auto tip = new Label(this, _L("Failed to send. Click Retry to attempt sending again. If retrying does not work, please check the reason."));
     tip->Wrap(FromDIP(480));
     tip->SetMinSize(wxSize(FromDIP(480), -1));
     tip->SetMaxSize(wxSize(FromDIP(480), -1));

     wxBoxSizer *button_sizer = new wxBoxSizer(wxHORIZONTAL);

     StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                             std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

     StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                             std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));


     auto m_button_retry = new Button(this, _L("Retry"));
     m_button_retry->SetBackgroundColor(btn_bg_green);
     m_button_retry->SetBorderColor(*wxWHITE);
     m_button_retry->SetTextColor(wxColour("#FFFFFE"));
     m_button_retry->SetFont(Label::Body_12);
     m_button_retry->SetSize(wxSize(-1, FromDIP(24)));
     m_button_retry->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
     m_button_retry->SetCornerRadius(FromDIP(12));

     m_button_retry->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
         EndModal(wxYES);
     });

     auto m_button_input = new Button(this, _L("reconnect"));
     m_button_input->SetBackgroundColor(btn_bg_white);
     m_button_input->SetBorderColor(wxColour(38, 46, 48));
     m_button_input->SetFont(Label::Body_12);
     m_button_input->SetSize(wxSize(-1, FromDIP(24)));
     m_button_input->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
     m_button_input->SetCornerRadius(FromDIP(12));

     m_button_input->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
         EndModal(wxAPPLY);
     });

     button_sizer->Add(0, 0, 1, wxEXPAND, 5);
     button_sizer->Add(m_button_retry, 0, wxALL, 5);
     button_sizer->Add(m_button_input, 0, wxALL, 5);

     m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
     m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(20));
     m_sizer_main->Add(tip, 0, wxEXPAND|wxLEFT|wxRIGHT, FromDIP(25));
     m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(25));
     m_sizer_main->Add(button_sizer, 0, wxEXPAND|wxRIGHT, FromDIP(25));
     m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(20));

     SetSizer(m_sizer_main);
     Layout();
     Fit();

     CentreOnParent();
     wxGetApp().UpdateDlgDarkUI(this);
 }

void SendFailedConfirm::on_dpi_changed(const wxRect &suggested_rect) {}


 HelioStatementDialog::HelioStatementDialog(wxWindow *parent /*= nullptr*/)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Enable Helio"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
     SetBackgroundColour(*wxWHITE);

     wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);

     wxPanel* line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
     line->SetBackgroundColour(wxColour(166, 169, 170));



     m_title = new Label(this, Label::Head_14, _L("Terms of Service"));
     //m_title->SetForegroundColour(wxColour(0x26, 0x2E, 0x30));



     Label* m_description_line1 = new Label(this, Label::Body_13,
                               _L("You are about to enable a third-party software service feature from Helio Additive! Before confirming the use of this feature, please carefully read the following statements."));
     //m_description_line1->SetForegroundColour(wxColour(144, 144, 144));
     m_description_line1->SetMinSize(wxSize(FromDIP(680), -1));
     m_description_line1->SetMaxSize(wxSize(FromDIP(680), -1));
     m_description_line1->Wrap(FromDIP(680));

     Label *m_description_line2 = new Label(this, Label::Body_13,
                                            _L("Unless otherwise specified, Bambu Lab only provides support for the software features officially provided. The slicing evaluation and slicing optimization features based on Helio Additive's cloud service in this software will be developed, operated, provided, and maintained by Helio Additive. Helio Additive is responsible for the effectiveness and availability of this service. The optimization feature of this service may modify the default print commands, posing a risk of printer damage. These features will collect necessary user information and data to achieve relevant service functions. Subscriptions and payments may be involved. Please visit Helio Additive and refer to the Helio Additive Privacy Agreement and the Helio Additive User Agreement for detailed information."));

     Label* m_description_line3 = new Label(this, Label::Body_13,
         _L("Meanwhile, you understand that this product is provided to you \"as is\" based on Helio Additive's services, and Bambu makes no express or implied warranties of any kind, nor can it control the service effects. To the fullest extent permitted by applicable law, Bambu or its licensors/affiliates do not provide any express or implied representations or warranties, including but not limited to warranties regarding merchantability, satisfactory quality, fitness for a particular purpose, accuracy, confidentiality, and non-infringement of third-party rights. Due to the nature of network services, Bambu cannot guarantee that the service will be available at all times, and Bambu reserves the right to terminate the service based on relevant circumstances."));

     Label* m_description_line4 = new Label(this, Label::Body_13,
         _L("You agree not to use this product and its related updates to engage in the following activities:"));

     Label* m_description_line5 = new Label(this, Label::Body_13,
         _L("1.Copy or use any part of this product outside the authorized scope of Helio Additive and Bambu."));

     Label* m_description_line6 = new Label(this, Label::Body_13,
         _L("2.Attempt to disrupt, bypass, alter, invalidate, or evade any Digital Rights Management system related to and/or an integral part of this product."));

     Label* m_description_line7 = new Label(this, Label::Body_13,
         _L("3.Using this software and services for any improper or illegal activities."));

     m_description_line2->SetMinSize(wxSize(FromDIP(680), -1));
     m_description_line2->SetMaxSize(wxSize(FromDIP(680), -1));
     m_description_line2->Wrap(FromDIP(680));

     m_description_line3->SetMinSize(wxSize(FromDIP(680), -1));
     m_description_line3->SetMaxSize(wxSize(FromDIP(680), -1));
     m_description_line3->Wrap(FromDIP(680));

     m_description_line4->SetMinSize(wxSize(FromDIP(680), -1));
     m_description_line4->SetMaxSize(wxSize(FromDIP(680), -1));
     m_description_line4->Wrap(FromDIP(680));

     m_description_line5->SetMinSize(wxSize(FromDIP(680), -1));
     m_description_line5->SetMaxSize(wxSize(FromDIP(680), -1));
     m_description_line5->Wrap(FromDIP(680));

     m_description_line6->SetMinSize(wxSize(FromDIP(680), -1));
     m_description_line6->SetMaxSize(wxSize(FromDIP(680), -1));
     m_description_line6->Wrap(FromDIP(680));

     m_description_line7->SetMinSize(wxSize(FromDIP(680), -1));
     m_description_line7->SetMaxSize(wxSize(FromDIP(680), -1));
     m_description_line7->Wrap(FromDIP(680));

     auto helio_home_link = new LinkLabel(this, _L("https://www.helioadditive.com/"), "https://www.helioadditive.com/");
     LinkLabel* helio_privacy_link = nullptr;
     LinkLabel* helio_tou_link = nullptr;

     if (GUI::wxGetApp().app_config->get("region") == "China") {
         helio_privacy_link = new LinkLabel(this, _L("Privacy Policy of Helio Additive"), "https://www.helioadditive.com/zh-cn/policies/privacy");
         helio_tou_link     = new LinkLabel(this, _L("Terms of Use of Helio Additive"), "https://www.helioadditive.com/zh-cn/policies/terms");
     }
     else {
         helio_privacy_link = new LinkLabel(this, _L("Privacy Policy of Helio Additive"), "https://www.helioadditive.com/en-us/policies/privacy");
         helio_tou_link     = new LinkLabel(this, _L("Terms of Use of Helio Additive"), "https://www.helioadditive.com/en-us/policies/terms");
     }


     helio_home_link->SeLinkLabelFColour(wxColour(0, 119, 250));
     helio_privacy_link->SeLinkLabelFColour(wxColour(0, 119, 250));
     helio_tou_link->SeLinkLabelFColour(wxColour(0, 119, 250));

     helio_home_link->getLabel()->SetFont(::Label::Body_13);
     helio_privacy_link->getLabel()->SetFont(::Label::Body_13);
     helio_tou_link->getLabel()->SetFont(::Label::Body_13);

     Label *m_description_line8 =
         new Label(this, Label::Body_13,
                   _L("When you confirm to enable this feature, it means that you have confirmed and agreed to the above statements."));
     m_description_line8->SetMinSize(wxSize(FromDIP(680), -1));
     m_description_line8->SetMaxSize(wxSize(FromDIP(680), -1));
     m_description_line8->Wrap(FromDIP(680));



     wxBoxSizer *button_sizer;

     StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                               std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

     StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                             std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

     m_button_confirm = new Button(this, _L("Agree"));
     m_button_confirm->SetBackgroundColor(btn_bg_green);
     m_button_confirm->SetBorderColor(*wxWHITE);
     m_button_confirm->SetTextColor(wxColour(255, 255, 254));
     m_button_confirm->SetFont(Label::Body_12);
     m_button_confirm->SetSize(wxSize(FromDIP(58), FromDIP(24)));
     m_button_confirm->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
     m_button_confirm->SetCornerRadius(FromDIP(12));
     m_button_confirm->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { EndModal(wxID_OK); });

     m_button_cancel = new Button(this, _L("Cancel"));
     m_button_cancel->SetBackgroundColor(btn_bg_white);
     m_button_cancel->SetBorderColor(*wxWHITE);
     m_button_cancel->SetBorderColor(wxColour(38, 46, 48));
     m_button_cancel->SetFont(Label::Body_12);
     m_button_cancel->SetSize(wxSize(FromDIP(58), FromDIP(24)));
     m_button_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
     m_button_cancel->SetCornerRadius(FromDIP(12));
     m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { EndModal(wxID_NO); });


     button_sizer = new wxBoxSizer(wxHORIZONTAL);
     button_sizer->Add(0, 0, 1, wxEXPAND, 0);
     button_sizer->Add(m_button_confirm, 0, 0, 0);
     button_sizer->Add(0, 0, 0, wxLEFT, FromDIP(20));
     button_sizer->Add(m_button_cancel, 0, 0, 0);
     button_sizer->Add(0, 0, 0, wxRIGHT, FromDIP(50));

     main_sizer->Add(line, 0, wxEXPAND, 0);
     main_sizer->Add(0, 0, 0, wxTOP, FromDIP(16));
     main_sizer->Add(m_title, 0, wxALIGN_CENTER, 0);
     main_sizer->Add(0, 0, 0, wxTOP, FromDIP(14));
     main_sizer->Add(m_description_line1, 0, wxLEFT | wxRIGHT, FromDIP(50));
     main_sizer->Add(0, 0, 0, wxTOP, FromDIP(15));
     main_sizer->Add(m_description_line2, 0, wxLEFT | wxRIGHT, FromDIP(50));
     main_sizer->Add(0, 0, 0, wxTOP, FromDIP(15));
     main_sizer->Add(m_description_line3, 0, wxLEFT | wxRIGHT, FromDIP(50));
     main_sizer->Add(0, 0, 0, wxTOP, FromDIP(16));
     main_sizer->Add(m_description_line4, 0, wxLEFT | wxRIGHT, FromDIP(50));
     main_sizer->Add(0, 0, 0, wxTOP, FromDIP(16));
     main_sizer->Add(m_description_line5, 0, wxLEFT | wxRIGHT, FromDIP(50));
     main_sizer->Add(0, 0, 0, wxTOP, FromDIP(8));
     main_sizer->Add(m_description_line6, 0, wxLEFT | wxRIGHT, FromDIP(50));
     main_sizer->Add(0, 0, 0, wxTOP, FromDIP(8));
     main_sizer->Add(m_description_line7, 0, wxLEFT | wxRIGHT, FromDIP(50));
     main_sizer->Add(0, 0, 0, wxTOP, FromDIP(16));
     main_sizer->Add(m_description_line8, 0, wxLEFT | wxRIGHT, FromDIP(50));
     main_sizer->Add(0, 0, 0, wxTOP, FromDIP(16));
     main_sizer->Add(helio_home_link, 0, wxLEFT | wxRIGHT, FromDIP(50));
     main_sizer->Add(0, 0, 0, wxTOP, FromDIP(5));
     main_sizer->Add(helio_privacy_link, 0, wxLEFT | wxRIGHT, FromDIP(50));
     main_sizer->Add(0, 0, 0, wxTOP, FromDIP(5));
     main_sizer->Add(helio_tou_link, 0, wxLEFT | wxRIGHT, FromDIP(50));
     main_sizer->Add(0, 0, 0, wxTOP, FromDIP(15));
     main_sizer->Add(button_sizer, 0, wxEXPAND, 0);
     main_sizer->Add(0, 0, 0, wxTOP, FromDIP(16));

     SetSizer(main_sizer);
     Layout();
     Fit();

     CentreOnParent();
     wxGetApp().UpdateDlgDarkUI(this);
 }

void HelioStatementDialog::on_dpi_changed(const wxRect &suggested_rect)
{
}


HelioRemainUsageTime::HelioRemainUsageTime(wxWindow* parent,  wxString label) : wxPanel(parent)
{
    Create(label);
}

void HelioRemainUsageTime::Create(wxString label)
{
    SetBackgroundColour(*wxWHITE);

    Label* label_prefix = new Label(this, label);
    label_prefix->SetMaxSize(wxSize(FromDIP(400), -1));
    label_prefix->SetToolTip(label);

    m_label_remain_usage_time = new Label(this, "0");
    wxFont bold_font = m_label_remain_usage_time->GetFont();
    bold_font.SetWeight(wxFONTWEIGHT_BOLD);
    m_label_remain_usage_time->SetMinSize(wxSize(FromDIP(40), -1));
    m_label_remain_usage_time->SetMaxSize(wxSize(FromDIP(40), -1));
    m_label_remain_usage_time->SetFont(bold_font);

    wxBoxSizer* remain_sizer = new wxBoxSizer(wxHORIZONTAL);
    remain_sizer->Add(label_prefix, 0, wxALIGN_CENTER_VERTICAL);
    remain_sizer->Add(m_label_remain_usage_time, 0, wxALIGN_CENTER_VERTICAL);

    /*label_click_to_use = new Label(this, _L("Click Confirm to start this optimization immediately."));
    label_click_to_use->SetMaxSize(wxSize(FromDIP(400), -1));
    label_click_to_use->Wrap(FromDIP(400));

    label_click_to_buy = new Label(this, _L("Please click the link below to make a purchase or log in."));
    label_click_to_buy->SetMaxSize(wxSize(FromDIP(400), -1));
    label_click_to_buy->Wrap(FromDIP(400));

    label_click_to_use->Hide();
    label_click_to_buy->Hide();*/

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(remain_sizer);
    //sizer->Add(label_click_to_use, 0, wxALIGN_CENTER_VERTICAL);
    SetSizer(sizer);
}

void HelioRemainUsageTime::UpdateHelpTips(int type)
{
    /*if (type == 0) {
        label_click_to_use->Show();
        label_click_to_buy->Hide();
    }
    else {
        label_click_to_use->Hide();
        label_click_to_buy->Show();
    }*/
    Layout();
    Fit();
}

void HelioRemainUsageTime::UpdateRemainTime(int remain_time)
{
    if (m_remain_usage_time != remain_time) {
        m_remain_usage_time = remain_time;
        m_label_remain_usage_time->SetLabelText(wxString::Format("%d", m_remain_usage_time));
    }
}

static int s_get_min_chamber_temp()// TODO, use the chamber of used filaments
{
    auto preset_full_config = wxGetApp().preset_bundle->full_config();
    auto chamber_temperatures = preset_full_config.option<ConfigOptionInts>("chamber_temperatures");

    if (chamber_temperatures)
    {
        int min_temp = std::numeric_limits<int>::max();
        for (auto val : chamber_temperatures->values)
        {
            if (val < min_temp)
            {
                min_temp = val;
            }
        }

        if (min_temp != std::numeric_limits<int>::max())
        {
            return min_temp;
        }
    }

    return 0;
}

static double s_round(double value, int n)
{
    double factor = std::pow(10.0, n);
    return std::round(value * factor) / factor;
}

 HelioInputDialog::HelioInputDialog(wxWindow *parent /*= nullptr*/)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, wxString("Helio Additive"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    shared_ptr = std::make_shared<int>(0);

    if (wxGetApp().dark_mode()) {
        SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    } else {
        SetBackgroundColour(*wxWHITE);
    }

    SetMinSize(wxSize(FromDIP(500), -1));
    SetMaxSize(wxSize(FromDIP(500), -1));

    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
    wxPanel *line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    line->SetBackgroundColour(wxColour(166, 169, 170));

    wxBoxSizer* control_sizer = new wxBoxSizer(wxHORIZONTAL);
    togglebutton_simulate = new CustomToggleButton(this, "Simulation");
    togglebutton_optimize = new CustomToggleButton(this, "Optimization");
    togglebutton_simulate->SetMinSize(wxSize(FromDIP(124), FromDIP(30)));
    togglebutton_optimize->SetMinSize(wxSize(FromDIP(124), FromDIP(30)));
    control_sizer->Add(togglebutton_simulate, 0, wxCENTER, 0);
    control_sizer->Add(0, 0, 0, wxLEFT, FromDIP(10));
    control_sizer->Add(togglebutton_optimize, 0, wxCENTER, 0);

    togglebutton_simulate->Bind(wxEVT_LEFT_DOWN, &HelioInputDialog::on_selected_simulation, this);
    togglebutton_optimize->Bind(wxEVT_LEFT_DOWN, &HelioInputDialog::on_selected_optimaztion, this);

    /*Simulation panel*/
    panel_simulation = new wxPanel(this);
    if (wxGetApp().dark_mode()) {
        panel_simulation->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    } else {
        panel_simulation->SetBackgroundColour(*wxWHITE);
    }
    wxBoxSizer *sizer_simulation = new wxBoxSizer(wxVERTICAL);

    /*chamber temp*/
    //int min_chamber_temp = s_get_min_chamber_temp();
    auto chamber_temp_checker = TextInputValChecker::CreateDoubleRangeChecker(5.0, 70.0, true);
    wxBoxSizer* chamber_temp_item_for_simulation = create_input_item(panel_simulation, "chamber_temp_for_simulation", _L("Chamber Temperature"), wxT("\u00B0C"), {chamber_temp_checker});
    //m_input_items["chamber_temp_for_simulation"]->GetTextCtrl()->SetLabel(wxString::Format("%d", min_chamber_temp));
    m_input_items["chamber_temp_for_simulation"]->GetTextCtrl()->SetHint(wxT("5-70"));

    Label* sub = new Label(panel_simulation, _L("Note: Adjust the above temperature based on actual conditions. More accurate data ensures more precise analysis results."));
    sub->SetForegroundColour(wxColour(144, 144, 144));
    sub->SetSize(wxSize(FromDIP(440), -1));
    sub->Wrap(FromDIP(440));

    sizer_simulation->Add(0, 0, 0, wxTOP, FromDIP(24));
    sizer_simulation->Add(chamber_temp_item_for_simulation, 0, wxEXPAND, 0);
    sizer_simulation->Add(0, 0, 0, wxTOP, FromDIP(16));
    sizer_simulation->Add(sub, 0, wxEXPAND, 0);

    panel_simulation->SetSizer(sizer_simulation);
    panel_simulation->Layout();
    panel_simulation->Fit();

    /*Optimization Pay panel*/
    panel_pay_optimization = new wxPanel(this);
    if (wxGetApp().dark_mode()) {
        panel_pay_optimization->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    } else {
        panel_pay_optimization->SetBackgroundColour(*wxWHITE);
    }
    wxBoxSizer* sizer_pay_optimization = new wxBoxSizer(wxVERTICAL);
    wxStaticBitmap* panel_pay_loading_icon = new wxStaticBitmap(panel_pay_optimization, wxID_ANY, create_scaled_bitmap("helio_loading", panel_optimization, 109), wxDefaultPosition,
        wxSize(FromDIP(136), FromDIP(109)));
    
    Label* pay_sub = new Label(panel_pay_optimization, _L("Helio's advanced feature for power users: available post purchase on its official website."));
    pay_sub->SetForegroundColour(wxColour(144, 144, 144));
    pay_sub->SetSize(wxSize(FromDIP(440), -1));
    pay_sub->Wrap(FromDIP(440));

    sizer_pay_optimization->Add(0, 0, 0, wxTOP, FromDIP(26));
    sizer_pay_optimization->Add(panel_pay_loading_icon, 0, wxALIGN_CENTER, 0);
    sizer_pay_optimization->Add(0, 0, 0, wxTOP, FromDIP(36));
    sizer_pay_optimization->Add(pay_sub, 0, wxLEFT, 0);

    panel_pay_optimization->SetSizer(sizer_pay_optimization);
    panel_pay_optimization->Layout();
    panel_pay_optimization->Fit();
    panel_pay_optimization->Hide();

    /*Optimization panel*/
    panel_optimization = new wxPanel(this);
    if (wxGetApp().dark_mode()) {
        panel_optimization->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    } else {
        panel_optimization->SetBackgroundColour(*wxWHITE);
    }
    wxBoxSizer *sizer_optimization = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *sizer_remain_usage_time = new wxBoxSizer(wxHORIZONTAL);
    m_remain_usage_time = new HelioRemainUsageTime(panel_optimization, _L("Remaining usage times for current account this month: "));
    m_remain_usage_time->UpdateRemainTime(0);
    sizer_remain_usage_time->Add(0, 0, 1, wxEXPAND, 0);
    sizer_remain_usage_time->Add(m_remain_usage_time, 0, wxEXPAND, 0);

    wxBoxSizer* sizer_remain_purchased_time = new wxBoxSizer(wxHORIZONTAL);
    m_remain_purchased_time = new HelioRemainUsageTime(panel_optimization, _L("Purchased add-ons: "));
    m_remain_purchased_time->UpdateRemainTime(0);
    sizer_remain_purchased_time->Add(0, 0, 1, wxEXPAND, 0);
    sizer_remain_purchased_time->Add(m_remain_purchased_time, 0, wxEXPAND, 0);

    

    /*general Options*/
    std::map<int, wxString> config_outerwall;
    config_outerwall[0] = _L("No");
    config_outerwall[1] = _L("Yes");
    auto outerwall =  create_combo_item(panel_optimization, "optimiza_outerwall", _L("Optimise Outer Walls"), config_outerwall, 0);

    wxBoxSizer* advace_setting_sizer = new wxBoxSizer(wxHORIZONTAL);
    advanced_settings_link = new wxPanel(panel_optimization);
    advanced_settings_link->SetBackgroundColour(*wxWHITE);

    Label* more_setting_tips = new Label(advanced_settings_link, _L("Advanced Settings"));
    advace_setting_sizer->Add(more_setting_tips, 0, wxALIGN_LEFT | wxTOP, FromDIP(4));
    advanced_options_icon = new wxStaticBitmap(advanced_settings_link, wxID_ANY, create_scaled_bitmap("advanced_option3", panel_optimization, 18), wxDefaultPosition,
        wxSize(FromDIP(18), FromDIP(18)));
    advace_setting_sizer->Add(advanced_options_icon, 0, wxALIGN_LEFT | wxTOP, FromDIP(4));
    advanced_settings_link->SetSizer(advace_setting_sizer);
    advanced_settings_link->Layout();
    advanced_settings_link->Fit();

    /*buy now*/
    more_setting_tips->SetForegroundColour(wxColour(0, 174, 100));
    more_setting_tips->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_HAND); });
    more_setting_tips->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_ARROW); });
    advanced_settings_link->Hide();

    /*advanced option*/
    panel_advanced_option = new wxPanel(panel_optimization);
    if (wxGetApp().dark_mode()) {
        panel_advanced_option->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    } else {
        panel_advanced_option->SetBackgroundColour(*wxWHITE);
    }
    wxBoxSizer* sizer_advanced_option = new wxBoxSizer(wxVERTICAL);

    auto double_min_checker = TextInputValChecker::CreateDoubleMinChecker(0);

    // velocity
    float min_speed = 0.0;
    float max_speed = 0.0;
    auto plater = Slic3r::GUI::wxGetApp().plater();
    if (plater) { plater->get_preview_min_max_value_of_option((int)GCodeViewer::EViewType::Feedrate, min_speed, max_speed); }
    wxBoxSizer* min_velocity_item = create_input_item(panel_advanced_option, "min_velocity", _L("Min Velocity"), wxT("mm/s"), { double_min_checker } );
    m_input_items["min_velocity"]->GetTextCtrl()->SetLabel(wxString::Format("%.0f", s_round(min_speed, 0)));

    wxBoxSizer* max_velocity_item = create_input_item(panel_advanced_option, "max_velocity", _L("Max Velocity"), wxT("mm/s"), { double_min_checker });
    m_input_items["max_velocity"]->GetTextCtrl()->SetLabel(wxString::Format("%.0f", s_round(max_speed, 0)));

    // volumetric speed
    float min_volumetric_speed = 0.0;
    float max_volumetric_speed = 0.0;
    if (plater) { plater->get_preview_min_max_value_of_option((int)GCodeViewer::EViewType::VolumetricRate, min_volumetric_speed, max_volumetric_speed); }
    wxBoxSizer* min_volumetric_speed_item = create_input_item(panel_advanced_option, "min_volumetric_speed", _L("Min Volumetric Speed"), wxT("mm\u00B3/s"), { double_min_checker });
    m_input_items["min_volumetric_speed"]->GetTextCtrl()->SetLabel(wxString::Format("%.2f", s_round(min_volumetric_speed, 2)));

    wxBoxSizer* max_volumetric_speed_item = create_input_item(panel_advanced_option, "max_volumetric_speed", _L("Max Volumetric Speed"), wxT("mm\u00B3/s"), { double_min_checker });
    m_input_items["max_volumetric_speed"]->GetTextCtrl()->SetLabel(wxString::Format("%.2f", s_round(max_volumetric_speed, 2)));

    // layers to optimize
    int layer_count = plater ? plater->get_gcode_layers_count() : 0;
    wxBoxSizer* layers_to_optimize_item = create_input_optimize_layers(panel_advanced_option, layer_count);

    sizer_advanced_option->Add(min_velocity_item, 0, wxEXPAND, FromDIP(0));
    sizer_advanced_option->Add(max_velocity_item, 0, wxEXPAND|wxTOP, FromDIP(6));
    sizer_advanced_option->Add(min_volumetric_speed_item, 0, wxEXPAND|wxTOP, FromDIP(6));
    sizer_advanced_option->Add(max_volumetric_speed_item, 0, wxEXPAND|wxTOP, FromDIP(6));
    sizer_advanced_option->Add(layers_to_optimize_item, 0, wxEXPAND|wxTOP, FromDIP(6));
    panel_advanced_option->SetSizer(sizer_advanced_option);
    panel_advanced_option->Layout();
    panel_advanced_option->Fit();

    sizer_optimization->Add(0, 0, 0, wxTOP, FromDIP(24));
    sizer_optimization->Add(sizer_remain_usage_time, 0, wxEXPAND, 0);
    sizer_optimization->Add(0, 0, 0, wxTOP, FromDIP(3));
    sizer_optimization->Add(sizer_remain_purchased_time, 0, wxEXPAND, 0);
    sizer_optimization->Add(0, 0, 0, wxTOP, FromDIP(10));
    sizer_optimization->Add(outerwall, 0, wxEXPAND, 0);
    sizer_optimization->Add(0, 0, 0, wxTOP, FromDIP(5));
    sizer_optimization->Add(advanced_settings_link, 0, wxEXPAND, 0);
    sizer_optimization->Add(0, 0, 0, wxTOP, FromDIP(8));
    sizer_optimization->Add(panel_advanced_option, 0, wxEXPAND, 0);
    sizer_optimization->Add(0, 0, 0, wxTOP, FromDIP(8));

    panel_optimization->SetSizer(sizer_optimization);
    panel_optimization->Layout();
    panel_optimization->Fit();

    panel_optimization->Hide();
    panel_advanced_option->Hide();

    more_setting_tips->Bind(wxEVT_LEFT_DOWN, [=](wxMouseEvent& e) {
        if (only_advanced_settings)
            return;

        if (!use_advanced_settings) {
            use_advanced_settings = true;
        }
        else {
            use_advanced_settings = false;
        }
        show_advanced_mode();
    });

    panel_optimization->Hide();
    
    /*helio wiki*/
    auto helio_wiki_link = new LinkLabel(this, _L("Click for more details"), wxGetApp().app_config->get("language") =="zh_CN"? "https://wiki.helioadditive.com/zh/home" : "https://wiki.helioadditive.com/en/home");
    helio_wiki_link->SeLinkLabelFColour(wxColour(0, 174, 66));
    helio_wiki_link->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    helio_wiki_link->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });

    buy_now_link = new LinkLabel(this, _L("Buy add-ons"), "https://wiki.helioadditive.com/");
    buy_now_link->SeLinkLabelFColour(wxColour(0, 174, 66));
    buy_now_link->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    buy_now_link->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });

    /*confirm*/
    wxBoxSizer* button_sizer = new wxBoxSizer(wxHORIZONTAL);
    StateColor btn_bg_green( std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Enabled), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Disabled), std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    m_button_confirm = new Button(this, _L("Confirm"));
    m_button_confirm->SetBackgroundColor(btn_bg_green);
    m_button_confirm->SetBorderColor(*wxWHITE);
    m_button_confirm->SetTextColor(wxColour(255, 255, 254));
    m_button_confirm->SetFont(Label::Body_12);
    m_button_confirm->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_confirm->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_confirm->SetCornerRadius(FromDIP(12));
    m_button_confirm->Bind(wxEVT_LEFT_DOWN, &HelioInputDialog::on_confirm, this);

    button_sizer->Add(0, 0, 1, wxEXPAND, 0);
    button_sizer->Add(m_button_confirm, 0, 0, 0);
    button_sizer->Add(0, 0, 0, wxLEFT, FromDIP(25));

    main_sizer->Add(line, 0, wxEXPAND, 0);
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(20));
    main_sizer->Add(control_sizer, 0, wxCENTER, FromDIP(30));
    main_sizer->Add(panel_simulation, 0,wxEXPAND | wxLEFT | wxRIGHT, FromDIP(25));
    main_sizer->Add(panel_pay_optimization, 0,wxEXPAND | wxLEFT | wxRIGHT, FromDIP(25));
    main_sizer->Add(panel_optimization, 0,wxEXPAND | wxLEFT | wxRIGHT, FromDIP(25));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(8));
    main_sizer->Add(helio_wiki_link, 0, wxLEFT | wxRIGHT, FromDIP(25));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(4));
    main_sizer->Add(buy_now_link, 0, wxLEFT | wxRIGHT, FromDIP(25));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(11));
    main_sizer->Add(button_sizer, 0, wxEXPAND, 0);
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(16));

    update_action(0);

    SetSizer(main_sizer);
    Layout();
    Fit();

    CentreOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void HelioInputDialog::update_action(int action)
{
    if (current_action == action) {
        return;
    }

    if (action == 0) {
        togglebutton_simulate->SetIsSelected(true);
        togglebutton_optimize->SetIsSelected(false);
        panel_simulation->Show();
        panel_pay_optimization->Hide();
        panel_optimization->Hide();
        m_button_confirm->Enable();
    }
    else {
        m_button_confirm->Disable();
        std::string helio_api_url = Slic3r::HelioQuery::get_helio_api_url();
        std::string helio_api_key = Slic3r::HelioQuery::get_helio_pat();

        std::weak_ptr<int> weak_ptr = shared_ptr;
        HelioQuery::request_remaining_optimizations(helio_api_url, helio_api_key, [this, weak_ptr](int times, int addons) {
            if (auto temp_ptr = weak_ptr.lock()) {
                CallAfter([=]() {
                    if (times <= 0) {
                        advanced_settings_link->Hide();
                        buy_now_link->Show();
                        if (m_remain_usage_time) { m_remain_usage_time->UpdateHelpTips(0); }
                    } else {
                        m_button_confirm->Enable();
                        advanced_settings_link->Show();
                        buy_now_link->Hide();
                        if (m_remain_usage_time) { m_remain_usage_time->UpdateHelpTips(1); }
                    }
                    Layout();
                    Fit();

                    /*set buy url*/
                    std::string helio_api_key = Slic3r::HelioQuery::get_helio_pat();
                    if (helio_api_key.empty()) return;

                    wxString url;
                    if (wxGetApp().app_config->get("region") == "China") {
                        url = "store.helioam.cn?patToken=" + helio_api_key;
                    } else {
                        url = "store.helioadditive.com?patToken=" + helio_api_key;
                    }
                    buy_now_link->setLinkUrl(url);

                    if (m_remain_usage_time) { m_remain_usage_time->UpdateRemainTime(times); }
                    if (m_remain_purchased_time) { m_remain_purchased_time->UpdateRemainTime(addons); }
                });
            }
            else {
                //already closed dialog
            }
        });

        togglebutton_simulate->SetIsSelected(false);
        togglebutton_optimize->SetIsSelected(true);
        panel_simulation->Hide();
        if (support_optimization == 0) {
            panel_pay_optimization->Hide();
            panel_optimization->Show();
        }
        else {
            panel_pay_optimization->Show();
            panel_optimization->Hide();
        }

        /*hide based mode when nozzle diameter  = 0.2*/
        const auto& full_config = wxGetApp().preset_bundle->full_config();
        const auto& project_config = wxGetApp().preset_bundle->project_config;
        double nozzle_diameter = full_config.option<ConfigOptionFloatsNullable>("nozzle_diameter")->values[0];

        if (boost::str(boost::format("%.1f") % nozzle_diameter) == "0.2") {
            only_advanced_settings = true;
            use_advanced_settings = true;
            show_advanced_mode();
        }    
    }
    Layout();
    Fit();

    current_action = action;
}

void HelioInputDialog::show_advanced_mode()
{
    if (use_advanced_settings) {
        advanced_options_icon->SetBitmap(create_scaled_bitmap("advanced_option3", panel_optimization, 18));
        panel_advanced_option->Show();
    }
    else {
        advanced_options_icon->SetBitmap(create_scaled_bitmap("advanced_option4", panel_optimization, 18));
        panel_advanced_option->Hide();
    }
    Layout();
    Fit();
}

wxBoxSizer* HelioInputDialog::create_input_item(wxWindow* parent, std::string key, wxString name, wxString unit,
                                                const std::vector<std::shared_ptr<TextInputValChecker>>& checkers)
{
    wxBoxSizer* item_sizer = new wxBoxSizer(wxHORIZONTAL);
    Label* inout_title = new Label(parent, Label::Body_14, name);
    inout_title->SetFont(::Label::Body_14);
    inout_title->SetForegroundColour(wxColour("#262E30"));

    TextInput *m_input_item = new TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(120), -1), wxTE_PROCESS_ENTER, unit);
    wxColour   parent_bg    = parent->GetBackgroundColour();
    //TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, DESIGN_INPUT_SIZE, wxTE_PROCESS_ENTER);
    StateColor input_bg(std::pair<wxColour, int>(wxColour("#F0F0F1"), StateColor::Disabled), std::pair<wxColour, int>(parent_bg, StateColor::Enabled));
    m_input_item->SetBackgroundColor(input_bg);
    wxTextValidator validator(wxFILTER_NUMERIC);
    
    m_input_item->GetTextCtrl()->SetBackgroundColour(parent_bg);
    m_input_item->GetTextCtrl()->SetWindowStyleFlag(wxBORDER_NONE | wxTE_PROCESS_ENTER);
    m_input_item->GetTextCtrl()->Bind(wxEVT_TEXT, [=](wxCommandEvent &) { 
        wxTextCtrl *ctrl = m_input_item->GetTextCtrl();
        wxColour new_bg = ctrl->GetValue().IsEmpty() && (key != "chamber_temp_for_simulation") ? wxColour(255, 182, 193) : parent_bg;
        if (ctrl->GetBackgroundColour() != new_bg) {
            ctrl->SetBackgroundColour(new_bg);
            ctrl->Refresh();
        }
        });
    m_input_item->GetTextCtrl()->SetValidator(validator);
    m_input_item->GetTextCtrl()->SetMaxLength(10);
    m_input_item->SetValCheckers(checkers);

    item_sizer->Add(inout_title, 0, wxALIGN_CENTER, 0);
    item_sizer->Add(0, 0, 1, wxEXPAND, 0);
    item_sizer->Add(m_input_item, 0);

    m_input_items.insert(std::pair<std::string, TextInput*>(key, m_input_item));
    return item_sizer;
}

wxBoxSizer* HelioInputDialog::create_combo_item(wxWindow* parent, std::string key,  wxString name, std::map<int, wxString> combolist, int def)
{
    wxBoxSizer* item_sizer = new wxBoxSizer(wxHORIZONTAL);
    Label* inout_title = new Label(parent, Label::Body_14, name);
    inout_title->SetFont(::Label::Body_14);
    inout_title->SetForegroundColour(wxColour("#262E30"));

    auto combobox = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(120), -1), 0, nullptr, wxCB_READONLY);
    combobox->SetFont(::Label::Body_13);
    combobox->GetDropDown().SetFont(::Label::Body_13);

    std::vector<wxString>::iterator iter;
    for (auto label : combolist)
        combobox->Append(label.second);

    if (def < combolist.size()) {
        combobox->SetSelection(def);
    }

    item_sizer->Add(inout_title, 0, wxALIGN_CENTER, 0);
    item_sizer->Add(0, 0, 1, wxEXPAND, 0);
    item_sizer->Add(combobox, 0);

    m_combo_items.insert(std::pair<std::string, ComboBox*>(key, combobox));
    return item_sizer;
}


wxBoxSizer* HelioInputDialog::create_input_optimize_layers(wxWindow* parent, int layer_count)
{
    wxBoxSizer* item_sizer = new wxBoxSizer(wxHORIZONTAL);
    Label* inout_title = new Label(parent, Label::Body_14, _L("Layers to Optimize "));
    inout_title->SetFont(::Label::Body_14);
    inout_title->SetForegroundColour(wxColour("#262E30"));

    StateColor input_bg(std::pair<wxColour, int>(wxColour("#F0F0F1"), StateColor::Disabled), std::pair<wxColour, int>(*wxWHITE, StateColor::Enabled));
    auto layer_range_checker = TextInputValChecker::CreateIntRangeChecker(0, layer_count);
    wxColour parent_bg = parent->GetBackgroundColour();

    TextInput* m_layer_min_item = new TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(76), -1), wxTE_PROCESS_ENTER);
    m_layer_min_item->SetBackgroundColor(input_bg);
    m_layer_min_item->GetTextCtrl()->SetWindowStyleFlag(wxBORDER_NONE | wxTE_PROCESS_ENTER);
    m_layer_min_item->GetTextCtrl()->SetBackgroundColour(parent_bg);
    m_layer_min_item->GetTextCtrl()->Bind(wxEVT_TEXT, [=](wxCommandEvent &) {
        wxTextCtrl *ctrl = m_layer_min_item->GetTextCtrl();
        wxColour new_bg = ctrl->GetValue().IsEmpty() ? wxColour(255, 182, 193) : parent_bg;
        if (ctrl->GetBackgroundColour() != new_bg) {
            ctrl->SetBackgroundColour(new_bg);
            ctrl->Refresh();
        }
    });
    m_layer_min_item->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_layer_min_item->GetTextCtrl()->SetMaxLength(10);
    m_layer_min_item->GetTextCtrl()->SetLabel("2");
    m_layer_min_item->SetValCheckers({layer_range_checker});

    TextInput* m_layer_max_item = new TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(76), -1), wxTE_PROCESS_ENTER);
    m_layer_max_item->SetBackgroundColor(input_bg);
    m_layer_min_item->GetTextCtrl()->SetWindowStyleFlag(wxBORDER_NONE | wxTE_PROCESS_ENTER);
    m_layer_max_item->GetTextCtrl()->SetBackgroundColour(parent_bg);
    m_layer_max_item->GetTextCtrl()->Bind(wxEVT_TEXT, [=](wxCommandEvent &) {
        wxTextCtrl *ctrl = m_layer_max_item->GetTextCtrl();
        wxColour new_bg = ctrl->GetValue().IsEmpty() ? wxColour(255, 182, 193) : parent_bg;
        if (ctrl->GetBackgroundColour() != new_bg) {
            ctrl->SetBackgroundColour(new_bg);
            ctrl->Refresh();
        }
    });
    m_layer_max_item->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_layer_max_item->GetTextCtrl()->SetMaxLength(10);
    m_layer_max_item->GetTextCtrl()->SetLabel(wxString::Format("%d", layer_count));
    m_layer_max_item->SetValCheckers({ layer_range_checker });

    item_sizer->Add(inout_title, 0, wxALIGN_CENTER, 0);
    item_sizer->Add(0, 0, 1, wxEXPAND, 0);
    item_sizer->Add(m_layer_min_item);
    item_sizer->AddSpacer(FromDIP(1));
    item_sizer->Add(new Label(parent, Label::Body_13, "-"), 0, wxALIGN_CENTER_VERTICAL, 0);
    item_sizer->AddSpacer(FromDIP(1));
    item_sizer->Add(m_layer_max_item);

    m_input_items.insert(std::pair<std::string, TextInput*>("layers_to_optimize_min", m_layer_min_item));
    m_input_items.insert(std::pair<std::string, TextInput*>("layers_to_optimize_max", m_layer_max_item));

    return item_sizer;
}


static bool s_get_double_regex(const wxString& str, double& value)
{
    value = -1;

    std::string s = str.ToStdString();
    std::regex  pattern("^[-+]?[0-9]*\\.?[0-9]+$");

    if (std::regex_match(s, pattern))
    {
        return str.ToDouble(&value);
    }

    return false;
}

static bool s_get_int_regex(const wxString& str, int& value)
{
    value = -1;

    std::string s = str.ToStdString();
    std::regex  pattern("^[-+]?[0-9]+$");

    if (std::regex_match(s, pattern))
    {
        long val;
        if (str.ToLong(&val))
        {
            if (val >= INT_MIN && val <= INT_MAX)
            {
                value = static_cast<int>(val);
                return true;
            }
        }
    }
    return false;
}


static bool s_get_double_val(TextInput* item, float& val)
{
    double double_val = -1;
    if (item->CheckValid() && s_get_double_regex(item->GetTextCtrl()->GetValue(), double_val))
    {
        val = double_val;
        return true;
    }

    val = double_val;
    item->SetFocus();
    item->GetTextCtrl()->SetInsertionPointEnd();
    return false;
};

static bool s_get_int_val(TextInput* item, int& val)
{
    if (item->CheckValid() && s_get_int_regex(item->GetTextCtrl()->GetValue(), val))
    {
        return true;
    }

    item->SetFocus();
    item->GetTextCtrl()->SetInsertionPointEnd();
    return false;
}

Slic3r::HelioQuery::SimulationInput HelioInputDialog::get_simulation_input(bool& ok)
{
    HelioQuery::SimulationInput data;
    if (m_input_items["chamber_temp_for_simulation"]->GetTextCtrl()->GetValue().IsEmpty()) {
        ok = true;
    }
    else {
        ok = s_get_double_val(m_input_items["chamber_temp_for_simulation"], data.chamber_temp);
    }
    return data;
}

Slic3r::HelioQuery::OptimizationInput HelioInputDialog::get_optimization_input(bool& ok)
{
    ok = false;
    HelioQuery::OptimizationInput data;
    data.outer_wall = m_combo_items["optimiza_outerwall"]->GetSelection();

    if (!s_get_int_val(m_input_items["layers_to_optimize_min"], data.layers_to_optimize[0])) { return data; }
    if (!s_get_int_val(m_input_items["layers_to_optimize_max"], data.layers_to_optimize[1])) { return data; }

    if (data.layers_to_optimize[0] > data.layers_to_optimize[1]) {
        int temp = data.layers_to_optimize[0];
        data.layers_to_optimize[0] = data.layers_to_optimize[1];
        data.layers_to_optimize[1] = temp;
    }

    if (!use_advanced_settings) {
        ok = true;
        return data;
    }

    if (!s_get_double_val(m_input_items["min_velocity"], data.min_velocity)) { return data; }
    if (!s_get_double_val(m_input_items["max_velocity"], data.max_velocity)) { return data; }
    if (!s_get_double_val(m_input_items["min_volumetric_speed"], data.min_volumetric_speed)) { return data; }
    if (!s_get_double_val(m_input_items["max_volumetric_speed"], data.max_volumetric_speed)) { return data; }
    ok = true;
    return data;
}


void HelioInputDialog::on_confirm(wxMouseEvent& e)
{
    bool ok = false;
    if (current_action == 0) // Simulation
    {
        get_simulation_input(ok);
    }
    else if (current_action == 1) // Optimization
    {
        get_optimization_input(ok);
    }

    if (ok)
    {
        EndModal(wxID_OK);
    }
}

void HelioInputDialog::on_dpi_changed(const wxRect &suggested_rect) 
{
}

HelioPatNotEnoughDialog::HelioPatNotEnoughDialog(wxWindow* parent /*= nullptr*/)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, wxString("Helio Additive"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    SetBackgroundColour(*wxWHITE);

    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    wxPanel* line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    line->SetBackgroundColour(wxColour(166, 169, 170));

    Label* text = new Label(this, Label::Body_13, _L("Failed to obtain Helio PAT. The number of issued PATs has reached the upper limit. Please pay attention to the information on the Helio official website. Click Refresh to get it again once it is available."), LB_AUTO_WRAP);
    text->SetForegroundColour(wxColour("#6C6C6C"));
    text->SetMinSize(wxSize(FromDIP(450), -1));
    text->SetMaxSize(wxSize(FromDIP(450), -1));
    text->Wrap(FromDIP(450));

    auto helio_wiki_link = new LinkLabel(this, _L("Click for more details"), wxGetApp().app_config->get("language") =="zh_CN"? "https://wiki.helioadditive.com/zh/home" : "https://wiki.helioadditive.com/en/home");
    helio_wiki_link->SeLinkLabelFColour(wxColour(0, 174, 66));
    helio_wiki_link->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    helio_wiki_link->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW); });

    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));


    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);
    auto m_button_ok = new Button(this, _L("Confirm"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(*wxWHITE);
    m_button_ok->SetTextColor(wxColour(255, 255, 254));
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetCornerRadius(FromDIP(12));

    sizer_button->AddStretchSpacer();
    sizer_button->Add(m_button_ok, 0, wxALL, FromDIP(5));

    m_button_ok->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        EndModal(wxID_OK);
        });

    main_sizer->Add(line, 0, wxEXPAND, 0);
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(26));
    main_sizer->Add(text, 0, wxEXPAND|wxLEFT|wxRIGHT, FromDIP(30));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(15));
    main_sizer->Add(helio_wiki_link, 0, wxLEFT|wxRIGHT, FromDIP(30));
    main_sizer->Add(0, 0, 0, wxTOP, FromDIP(15));
    main_sizer->Add(sizer_button, 0, wxEXPAND|wxLEFT|wxRIGHT, FromDIP(30));

    SetSizer(main_sizer);
    Layout();
    Fit();
}

HelioPatNotEnoughDialog::~HelioPatNotEnoughDialog() {}

void HelioPatNotEnoughDialog::on_dpi_changed(const wxRect& suggested_rect)
{

}

 }} // namespace Slic3r::GUI
