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

#include <wx/regex.h>
#include <wx/progdlg.h>
#include <wx/clipbrd.h>
#include <wx/dcgraph.h>
#include <miniz.h>
#include <algorithm>
#include "Plater.hpp"
#include "BitmapCache.hpp"

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_SECONDARY_CHECK_CONFIRM, wxCommandEvent);
wxDEFINE_EVENT(EVT_SECONDARY_CHECK_CANCEL, wxCommandEvent);
wxDEFINE_EVENT(EVT_CHECKBOX_CHANGE, wxCommandEvent);
wxDEFINE_EVENT(EVT_ENTER_IP_ADDRESS, wxCommandEvent);
wxDEFINE_EVENT(EVT_CLOSE_IPADDRESS_DLG, wxCommandEvent);
wxDEFINE_EVENT(EVT_CHECK_IP_ADDRESS_FAILED, wxCommandEvent);

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

    m_text_up_info = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    m_text_up_info->SetFont(::Label::Head_14);
    m_text_up_info->SetForegroundColour(wxColour(0x26, 0x2E, 0x30));
    m_text_up_info->Wrap(-1);
    m_sizer_right->Add(m_text_up_info, 0, 0, 0);

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
    auto        m_staticText_release_note = new wxStaticText(m_vebview_release_note, wxID_ANY, release_note, wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_release_note->SetForegroundColour(*wxBLACK);
    m_staticText_release_note->Wrap(FromDIP(530));
    sizer_text_release_note->Add(m_staticText_release_note, 0, wxALL, 5);
    m_vebview_release_note->SetSizer(sizer_text_release_note);
    m_vebview_release_note->Layout();
    m_vebview_release_note->Fit();
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

    m_text_up_info = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    m_text_up_info->SetFont(::Label::Head_13);
    m_text_up_info->SetMaxSize(wxSize(FromDIP(260), -1));
    m_text_up_info->Wrap(FromDIP(260));
    m_text_up_info->SetForegroundColour(wxColour(0x26, 0x2E, 0x30));


    operation_tips = new ::Label(this, _L("Click OK to update the Network plug-in when Bambu Studio launches next time."));
    operation_tips->SetFont(::Label::Body_12);
    operation_tips->SetSize(wxSize(FromDIP(260), -1));
    operation_tips->Wrap(FromDIP(260));
    operation_tips->SetForegroundColour(*wxBLACK);


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
    m_button_ok->SetTextColor(wxColour(0xFFFFFE));
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

    m_sizer_right->Add(m_text_up_info, 0, 0, 0);
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
    catch(nlohmann::detail::parse_error &err) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__<< ": parse "<<json_path<<" got a nlohmann::detail::parse_error, reason = " << err.what();
        return;
    }

    version = from_u8(version_str);
    description = from_u8(description_str);

    m_text_up_info->SetLabel(wxString::Format(_L("A new Network plug-in(%s) available, Do you want to install it?"), version));
    m_text_up_info->SetMaxSize(wxSize(FromDIP(260), -1));
    m_text_up_info->Wrap(FromDIP(260));
    wxBoxSizer* sizer_text_release_note = new wxBoxSizer(wxVERTICAL);
    auto        m_text_label = new ::Label(m_vebview_release_note, description);
    m_text_label->SetFont(::Label::Body_13);
    m_text_label->SetForegroundColour(*wxBLACK);
    m_text_label->SetMaxSize(wxSize(FromDIP(235), -1));
    m_text_label->Wrap(FromDIP(235));

    sizer_text_release_note->Add(m_text_label, 0, wxALL, 5);
    m_vebview_release_note->SetSizer(sizer_text_release_note);
    m_vebview_release_note->Layout();
    m_vebview_release_note->Fit();
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

    m_text_up_info = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    m_text_up_info->SetFont(::Label::Head_14);
    m_text_up_info->SetForegroundColour(wxColour(0x26, 0x2E, 0x30));
    m_text_up_info->Wrap(-1);

    

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
	ph /= "resources/tooltip/common/releasenote.html";
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


    m_remind_choice = new wxCheckBox( this, wxID_ANY, _L("Don't remind me of this version again"), wxDefaultPosition, wxDefaultSize, 0 );
    m_remind_choice->SetValue(false);
    m_remind_choice->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, &UpdateVersionDialog::alter_choice,this);

    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);


    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                            std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

    m_button_ok = new Button(this, _L("OK"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(*wxWHITE);
    m_button_ok->SetTextColor(wxColour("#FFFFFE"));
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetCornerRadius(FromDIP(12));

    m_button_ok->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        EndModal(wxID_YES);
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

    sizer_button->Add(m_remind_choice, 0, wxALL | wxEXPAND, FromDIP(5));
    sizer_button->AddStretchSpacer();
    sizer_button->Add(m_button_ok, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_cancel, 0, wxALL, FromDIP(5));

    m_sizer_right->Add(m_text_up_info, 0, wxBOTTOM|wxTOP, FromDIP(15));
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

void UpdateVersionDialog::alter_choice(wxCommandEvent& event)
{
    wxGetApp().set_skip_version(m_remind_choice->GetValue());
}

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
    m_button_ok->Rescale();
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
    std::string url_line    = "";
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
    }
    else {
        m_simplebook_release_note->SetMaxSize(wxSize(FromDIP(560), FromDIP(430)));
        m_simplebook_release_note->SetSelection(0);
        m_text_up_info->SetLabel(wxString::Format(_L("Click to download new version in default browser: %s"), version));
        wxBoxSizer* sizer_text_release_note = new wxBoxSizer(wxVERTICAL);
        auto        m_staticText_release_note = new wxStaticText(m_scrollwindows_release_note, wxID_ANY, release_note, wxDefaultPosition, wxDefaultSize, 0);
        m_staticText_release_note->SetForegroundColour(*wxBLACK);
        m_staticText_release_note->Wrap(FromDIP(530));
        sizer_text_release_note->Add(m_staticText_release_note, 0, wxALL, 5);
        m_scrollwindows_release_note->SetSizer(sizer_text_release_note);
        m_scrollwindows_release_note->Layout();
        m_scrollwindows_release_note->Fit();
        SetMinSize(GetSize());
        SetMaxSize(GetSize());
    }
    Layout();
    Fit();
}

SecondaryCheckDialog::SecondaryCheckDialog(wxWindow* parent, wxWindowID id, const wxString& title, enum ButtonStyle btn_style, const wxPoint& pos, const wxSize& size, long style, bool not_show_again_check)
    :DPIFrame(parent, id, title, pos, size, style)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(480), 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(5));

    wxBoxSizer* m_sizer_right = new wxBoxSizer(wxVERTICAL);

    m_sizer_right->Add(0, 0, 1, wxTOP, FromDIP(15));

    m_vebview_release_note = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_vebview_release_note->SetScrollRate(0, 5);
    m_vebview_release_note->SetBackgroundColour(*wxWHITE);
    m_vebview_release_note->SetMinSize(wxSize(FromDIP(280), FromDIP(280)));
    m_sizer_right->Add(m_vebview_release_note, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(35));


    auto bottom_sizer = new wxBoxSizer(wxVERTICAL);
    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));


    if (not_show_again_check) {
        m_show_again_checkbox = new wxCheckBox(this, wxID_ANY, _L("Don't show again"), wxDefaultPosition, wxDefaultSize, 0);
        m_show_again_checkbox->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, [this](wxCommandEvent& e) {
            not_show_again = !not_show_again;
            m_show_again_checkbox->SetValue(not_show_again);
        });
        bottom_sizer->Add(m_show_again_checkbox, 0, wxALL, FromDIP(5));
    }
    m_button_ok = new Button(this, _L("Confirm"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(*wxWHITE);
    m_button_ok->SetTextColor(wxColour("#FFFFFE"));
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(-1, FromDIP(24)));
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
    m_button_cancel->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetMinSize(wxSize(-1, FromDIP(24)));
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

    sizer_button->AddStretchSpacer();
    sizer_button->Add(m_button_ok, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_cancel, 0, wxALL, FromDIP(5));
    bottom_sizer->Add(sizer_button, 0, wxEXPAND | wxRIGHT | wxLEFT, 0);


    m_sizer_right->Add(bottom_sizer, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(35));
    m_sizer_right->Add(0, 0, 0, wxTOP,FromDIP(18));

    Bind(wxEVT_CLOSE_WINDOW, [this](auto& e) {this->on_hide();});

    SetSizer(m_sizer_right);
    Layout();
    m_sizer_main->Fit(this);

    CenterOnParent();
    wxGetApp().UpdateFrameDarkUI(this);
}

void SecondaryCheckDialog::update_text(wxString text)
{
    wxBoxSizer* sizer_text_release_note = new wxBoxSizer(wxVERTICAL);

    if (!m_staticText_release_note) {
        m_staticText_release_note = new Label(m_vebview_release_note, text);
    }
    m_staticText_release_note->SetLabelText(text);
    m_staticText_release_note->SetSize(wxSize(FromDIP(260), -1));
    m_staticText_release_note->SetMaxSize(wxSize(FromDIP(260), -1));
    m_staticText_release_note->SetMinSize(wxSize(FromDIP(260), -1));
    m_staticText_release_note->Wrap(FromDIP(260));

    wxBoxSizer* top_blank_sizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* bottom_blank_sizer = new wxBoxSizer(wxVERTICAL);
    top_blank_sizer->Add(FromDIP(5), 0, wxALIGN_CENTER | wxALL, FromDIP(5));
    bottom_blank_sizer->Add(FromDIP(5), 0, wxALIGN_CENTER | wxALL, FromDIP(5));

    sizer_text_release_note->Add(top_blank_sizer, 0, wxALIGN_CENTER | wxALL, FromDIP(5));
    sizer_text_release_note->Add(m_staticText_release_note, 0, wxALIGN_CENTER, FromDIP(5));
    sizer_text_release_note->Add(bottom_blank_sizer, 0, wxALIGN_CENTER | wxALL, FromDIP(5));
    m_vebview_release_note->SetSizer(sizer_text_release_note);
    auto text_size = m_staticText_release_note->GetSize();
    if (text_size.y < FromDIP(280))
        m_vebview_release_note->SetMinSize(wxSize(FromDIP(280), text_size.y + FromDIP(25)));
    else
        m_vebview_release_note->SetMinSize(wxSize(FromDIP(300), FromDIP(280)));

    m_vebview_release_note->Layout();
    m_sizer_main->Layout();
    m_sizer_main->Fit(this);
}

void SecondaryCheckDialog::on_show()
{
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

void SecondaryCheckDialog::rescale()
{
    m_button_ok->Rescale();
    m_button_cancel->Rescale();
}

ConfirmBeforeSendDialog::ConfirmBeforeSendDialog(wxWindow* parent, wxWindowID id, const wxString& title, enum ButtonStyle btn_style, const wxPoint& pos, const wxSize& size, long style, bool not_show_again_check)
    :DPIDialog(parent, id, title, pos, size, style)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(480), 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(5));

    wxBoxSizer* m_sizer_right = new wxBoxSizer(wxVERTICAL);

    m_sizer_right->Add(0, 0, 1, wxTOP, FromDIP(15));

    m_vebview_release_note = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_vebview_release_note->SetScrollRate(0, 5);
    m_vebview_release_note->SetBackgroundColour(*wxWHITE);
    m_vebview_release_note->SetMinSize(wxSize(FromDIP(280), FromDIP(280)));
    m_sizer_right->Add(m_vebview_release_note, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(35));


    auto bottom_sizer = new wxBoxSizer(wxVERTICAL);
    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));


    if (not_show_again_check) {
        m_show_again_checkbox = new wxCheckBox(this, wxID_ANY, _L("Don't show again"), wxDefaultPosition, wxDefaultSize, 0);
        m_show_again_checkbox->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, [this](wxCommandEvent& e) {
            not_show_again = !not_show_again;
            m_show_again_checkbox->SetValue(not_show_again);
            });
        bottom_sizer->Add(m_show_again_checkbox, 0, wxALL, FromDIP(5));
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

    sizer_button->AddStretchSpacer();
    sizer_button->Add(m_button_ok, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_cancel, 0, wxALL, FromDIP(5));
    bottom_sizer->Add(sizer_button, 0, wxEXPAND | wxRIGHT | wxLEFT, 0);


    m_sizer_right->Add(bottom_sizer, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(35));
    m_sizer_right->Add(0, 0, 0, wxTOP, FromDIP(18));

    Bind(wxEVT_CLOSE_WINDOW, [this](auto& e) {this->on_hide(); });

    SetSizer(m_sizer_right);
    Layout();
    m_sizer_main->Fit(this);

    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void ConfirmBeforeSendDialog::update_text(wxString text)
{
    wxBoxSizer* sizer_text_release_note = new wxBoxSizer(wxVERTICAL);
    if (!m_staticText_release_note)
        m_staticText_release_note = new Label(m_vebview_release_note, text);
    else
        m_staticText_release_note->SetLabelText(text);
    m_staticText_release_note->Wrap(FromDIP(260));
    m_staticText_release_note->SetSize(wxSize(FromDIP(260), -1));
    m_staticText_release_note->SetMaxSize(wxSize(FromDIP(260), -1));
    m_staticText_release_note->SetMinSize(wxSize(FromDIP(260), -1));

    wxBoxSizer* top_blank_sizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* bottom_blank_sizer = new wxBoxSizer(wxVERTICAL);
    top_blank_sizer->Add(FromDIP(5), 0, wxALIGN_CENTER | wxALL, FromDIP(5));
    bottom_blank_sizer->Add(FromDIP(5), 0, wxALIGN_CENTER | wxALL, FromDIP(5));

    sizer_text_release_note->Add(top_blank_sizer, 0, wxALIGN_CENTER | wxALL, FromDIP(5));
    sizer_text_release_note->Add(m_staticText_release_note, 0, wxALIGN_CENTER, FromDIP(5));
    sizer_text_release_note->Add(bottom_blank_sizer, 0, wxALIGN_CENTER | wxALL, FromDIP(5));
    m_vebview_release_note->SetSizer(sizer_text_release_note);
    auto text_size = m_staticText_release_note->GetSize();
    if (text_size.y < FromDIP(280))
        m_vebview_release_note->SetMinSize(wxSize(FromDIP(280), text_size.y + FromDIP(25)));
    else
        m_vebview_release_note->SetMinSize(wxSize(FromDIP(300), FromDIP(280)));

    m_vebview_release_note->Layout();
    m_sizer_main->Layout();
    m_sizer_main->Fit(this);
}

void ConfirmBeforeSendDialog::on_show()
{
    wxGetApp().UpdateDlgDarkUI(this);
    // recover button color
    wxMouseEvent evt_ok(wxEVT_LEFT_UP);
    m_button_ok->GetEventHandler()->ProcessEvent(evt_ok);
    wxMouseEvent evt_cancel(wxEVT_LEFT_UP);
    m_button_cancel->GetEventHandler()->ProcessEvent(evt_cancel);
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

void ConfirmBeforeSendDialog::rescale()
{
    m_button_ok->Rescale();
    m_button_cancel->Rescale();
}

InputIpAddressDialog::InputIpAddressDialog(wxWindow* parent)
    :DPIDialog(static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, _L("LAN Connection Failed (Sending print file)"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    wxBoxSizer* m_sizer_body = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* m_sizer_main = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer* m_sizer_main_left = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* m_sizer_main_right = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));

    comfirm_before_enter_text = _L("Step 1, please confirm Bambu Studio and your printer are in the same LAN.");
    comfirm_after_enter_text = _L("Step 2, if the IP and Access Code below are different from the actual values on your printer, please correct them.");


    m_tip1 = new Label(this, comfirm_before_enter_text);
    m_tip1->SetFont(::Label::Body_13);
    m_tip1->SetMinSize(wxSize(FromDIP(352), -1));
    m_tip1->SetMaxSize(wxSize(FromDIP(352), -1));
    m_tip1->Wrap(FromDIP(352));

    auto        m_line_tips = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_tips->SetBackgroundColour(wxColour(0xEEEEEE));

    m_tip2 = new Label(this, comfirm_after_enter_text);
    m_tip2->SetFont(::Label::Body_13);
    m_tip2->SetMinSize(wxSize(FromDIP(352), -1));
    m_tip2->SetMaxSize(wxSize(FromDIP(352), -1));
    m_tip2->Wrap(FromDIP(352));

    auto m_input_tip_area = new wxBoxSizer(wxHORIZONTAL);
    auto m_input_area = new wxBoxSizer(wxHORIZONTAL);

    m_tips_ip = new Label(this, _L("IP"));
    m_tips_ip->SetMinSize(wxSize(FromDIP(168), -1));
    m_tips_ip->SetMaxSize(wxSize(FromDIP(168), -1));

    m_input_ip = new TextInput(this, wxEmptyString, wxEmptyString);
    m_input_ip->Bind(wxEVT_TEXT, &InputIpAddressDialog::on_text, this);
    m_input_ip->SetMinSize(wxSize(FromDIP(168), FromDIP(28)));
    m_input_ip->SetMaxSize(wxSize(FromDIP(168), FromDIP(28)));

    m_tips_access_code = new Label(this, _L("Access Code"));
    m_tips_access_code->SetMinSize(wxSize(FromDIP(168),-1));
    m_tips_access_code->SetMaxSize(wxSize(FromDIP(168),-1));

    m_input_access_code = new TextInput(this, wxEmptyString, wxEmptyString);
    m_input_access_code->Bind(wxEVT_TEXT, &InputIpAddressDialog::on_text, this);
    m_input_access_code->SetMinSize(wxSize(FromDIP(168), FromDIP(28)));
    m_input_access_code->SetMaxSize(wxSize(FromDIP(168), FromDIP(28)));

    m_input_tip_area->Add(m_tips_ip, 0, wxALIGN_CENTER, 0);
    m_input_tip_area->Add(0, 0, 0, wxLEFT, FromDIP(16));
    m_input_tip_area->Add(m_tips_access_code, 0, wxALIGN_CENTER, 0);
   
    m_input_area->Add(m_input_ip, 0, wxALIGN_CENTER, 0);
    m_input_area->Add(0, 0, 0, wxLEFT, FromDIP(16));
    m_input_area->Add(m_input_access_code, 0, wxALIGN_CENTER, 0);

    m_error_msg = new Label(this, wxEmptyString);
    m_error_msg->SetFont(::Label::Body_13);
    m_error_msg->SetForegroundColour(wxColour(208,27,27));
    m_error_msg->Hide();

    m_tip3 = new Label(this, _L("Where to find your printer's IP and Access Code?"));
    m_tip3->SetFont(::Label::Body_12);
    m_tip3->SetMinSize(wxSize(FromDIP(352), -1));
    m_tip3->SetMaxSize(wxSize(FromDIP(352), -1));
    m_tip3->Wrap(FromDIP(352));

    m_img_help1 = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("input_accesscode_help1", this, 198), wxDefaultPosition, wxSize(FromDIP(352), FromDIP(198)), 0);
    
    m_img_help2 = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("input_accesscode_help2", this, 118), wxDefaultPosition, wxSize(FromDIP(352), FromDIP(118)), 0);
    
    m_img_help1->Hide();
    m_img_help2->Hide();


    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);

    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

    m_button_ok = new Button(this, _L("OK"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(*wxWHITE);
    m_button_ok->SetTextColor(wxColour(0xFFFFFE));
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetCornerRadius(FromDIP(12));
   

    m_button_ok->Bind(wxEVT_LEFT_DOWN, &InputIpAddressDialog::on_ok, this);

    auto m_button_cancel = new Button(this, _L("Cancel"));
    m_button_cancel->SetBackgroundColor(btn_bg_white);
    m_button_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_cancel->SetFont(Label::Body_12);
    m_button_cancel->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetCornerRadius(FromDIP(12));

    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
         on_cancel();
    });

    sizer_button->AddStretchSpacer();
    sizer_button->Add(m_button_ok, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_cancel, 0, wxALL, FromDIP(5));

    m_status_bar    = std::make_shared<BBLStatusBarSend>(this);
    m_status_bar->get_panel()->Hide();


    auto m_step_icon_panel1 = new wxWindow(this, wxID_ANY);
    auto m_step_icon_panel2 = new wxWindow(this, wxID_ANY);

    m_step_icon_panel1->SetBackgroundColour(*wxWHITE);
    m_step_icon_panel2->SetBackgroundColour(*wxWHITE);

    auto m_sizer_step_icon_panel1 = new wxBoxSizer(wxVERTICAL);
    auto m_sizer_step_icon_panel2 = new wxBoxSizer(wxVERTICAL);


    m_img_step1 = new wxStaticBitmap(m_step_icon_panel1, wxID_ANY, create_scaled_bitmap("ip_address_step", this, 6), wxDefaultPosition, wxSize(FromDIP(6), FromDIP(6)), 0);

    auto        m_line_tips_left = new wxPanel(m_step_icon_panel1, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_tips_left->SetBackgroundColour(wxColour(0xEEEEEE));
    m_img_step2 = new wxStaticBitmap(m_step_icon_panel2, wxID_ANY, create_scaled_bitmap("ip_address_step", this, 6), wxDefaultPosition, wxSize(FromDIP(6), FromDIP(6)), 0);

    m_sizer_step_icon_panel1->Add(m_img_step1, 0, wxALIGN_CENTER|wxALL, FromDIP(5));
   

    m_step_icon_panel1->SetSizer(m_sizer_step_icon_panel1);
    m_step_icon_panel1->Layout();
    m_step_icon_panel1->Fit();

    m_step_icon_panel2->SetSizer(m_sizer_step_icon_panel2);
    m_step_icon_panel2->Layout();
    m_step_icon_panel2->Fit();


    m_sizer_step_icon_panel2->Add(m_img_step2, 0, wxALIGN_CENTER|wxALL, FromDIP(5));

    m_step_icon_panel1->SetMinSize(wxSize(-1, m_tip1->GetSize().y));
    m_step_icon_panel1->SetMaxSize(wxSize(-1, m_tip1->GetSize().y));

    m_sizer_main_left->Add(m_step_icon_panel1, 0, wxEXPAND, 0);
    m_sizer_main_left->Add(0, 0, 0, wxTOP, FromDIP(20));
    m_sizer_main_left->Add(m_line_tips_left, 1, wxEXPAND, 0);
    m_sizer_main_left->Add(0, 0, 0, wxTOP, FromDIP(20));
    m_sizer_main_left->Add(m_step_icon_panel2, 0, wxEXPAND, 0);

   
    m_sizer_main_right->Add(m_tip1, 0, wxRIGHT|wxEXPAND, FromDIP(18));
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(20));
    m_sizer_main_right->Add(m_line_tips, 0, wxRIGHT|wxEXPAND, FromDIP(18));
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(20));
    m_sizer_main_right->Add(m_tip2, 0, wxRIGHT|wxEXPAND, FromDIP(18));
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(12));
    m_sizer_main_right->Add(m_input_tip_area, 0, wxRIGHT|wxEXPAND, FromDIP(18));
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(4));
    m_sizer_main_right->Add(m_input_area, 0, wxRIGHT|wxEXPAND, FromDIP(18));
    m_sizer_main_right->Add(m_error_msg, 0, wxRIGHT|wxEXPAND, FromDIP(18));
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(16));
    m_sizer_main_right->Add(m_tip3, 0, wxRIGHT|wxEXPAND, FromDIP(18));
    
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(4));
    m_sizer_main_right->Add(m_img_help1, 0, 0, 0);
    m_sizer_main_right->Add(m_img_help2, 0, 0, 0);
    
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(30));
    m_sizer_main_right->Add(sizer_button, 1, wxRIGHT|wxEXPAND, FromDIP(18));
    m_sizer_main_right->Add(0, 0, 0, wxTOP, FromDIP(16));
    m_sizer_main_right->Add(m_status_bar->get_panel(), 0,wxRIGHT|wxEXPAND, FromDIP(18));
    m_sizer_main_right->Layout();

    auto str_ip = m_input_ip->GetTextCtrl()->GetValue();
    auto str_access_code = m_input_access_code->GetTextCtrl()->GetValue();
    if (isIp(str_ip.ToStdString()) && str_access_code.Length() == 8) {
        m_button_ok->Enable(true);
        StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
            std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
        m_button_ok->SetTextColor(StateColor::darkModeColorFor("#FFFFFE"));
        m_button_ok->SetBackgroundColor(btn_bg_green);
    }
    else {
        m_button_ok->Enable(false);
        m_button_ok->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
        m_button_ok->SetBorderColor(wxColour(0x90, 0x90, 0x90));
    }
   
    m_sizer_main->Add(m_sizer_main_left, 0, wxLEFT, FromDIP(18));
    m_sizer_main->Add(m_sizer_main_right, 0, wxLEFT|wxEXPAND, FromDIP(4));
    m_sizer_main->Layout();

    m_sizer_body->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_body->Add(0, 0, 0, wxTOP, FromDIP(20));
    m_sizer_body->Add(m_sizer_main, 0, wxEXPAND, 0);
   
    SetSizer(m_sizer_body);
    Layout();
    Fit();

    CentreOnParent(wxBOTH);
    Move(wxPoint(GetScreenPosition().x, GetScreenPosition().y - FromDIP(50)));
    wxGetApp().UpdateDlgDarkUI(this);

    Bind(EVT_CHECK_IP_ADDRESS_FAILED, &InputIpAddressDialog::on_check_ip_address_failed, this);

    Bind(EVT_CLOSE_IPADDRESS_DLG, [this](auto& e) {
        m_status_bar->reset();
        EndModal(wxID_YES);
    });
    Bind(wxEVT_CLOSE_WINDOW, [this](auto& e) {on_cancel();});
}

void InputIpAddressDialog::on_cancel()
{
    if (m_send_job) {
        if (m_send_job->is_running()) {
            m_send_job->cancel();
            m_send_job->join();
        }
    }
    this->EndModal(wxID_CANCEL);
}


void InputIpAddressDialog::update_title(wxString title)
{
    SetTitle(title);
}

void InputIpAddressDialog::set_machine_obj(MachineObject* obj)
{
    m_obj = obj;
    m_input_ip->GetTextCtrl()->SetLabelText(m_obj->dev_ip);
    m_input_access_code->GetTextCtrl()->SetLabelText(m_obj->get_access_code());

    if (m_obj->printer_type == "C11") {
        m_img_help1->Hide();
        m_img_help2->Show();
    }
    else if (m_obj->printer_type == "BL-P001" || m_obj->printer_type == "BL-P002") {
         m_img_help1->Show();
         m_img_help2->Hide();
    }
    Layout();
    Fit();
}

void InputIpAddressDialog::update_error_msg(wxString msg)
{
    if (msg.empty()) {
        m_error_msg->Hide();
    }
    else {
         m_error_msg->Show();
         m_error_msg->SetLabelText(msg);
         m_error_msg->SetMinSize(wxSize(FromDIP(352), -1));
         m_error_msg->SetMaxSize(wxSize(FromDIP(352), -1));
         m_error_msg->Wrap(FromDIP(352));
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
    wxString ip = m_input_ip->GetTextCtrl()->GetValue();
    wxString str_access_code = m_input_access_code->GetTextCtrl()->GetValue();

    //check support function
    if (!m_obj) return;
    if (!m_obj->is_function_supported(PrinterFunction::FUNC_SEND_TO_SDCARD)) {
        wxString input_str = wxString::Format("%s|%s", ip, str_access_code);
        auto event = wxCommandEvent(EVT_ENTER_IP_ADDRESS);
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

    if (m_send_job) {
        m_send_job->join();
    }

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


    m_send_job = std::make_shared<SendJob>(m_status_bar, wxGetApp().plater(), m_obj->dev_id);
    m_send_job->m_dev_ip = ip.ToStdString();
    m_send_job->m_access_code = str_access_code.ToStdString();
    m_send_job->m_local_use_ssl = m_obj->local_use_ssl;
    m_send_job->connection_type = m_obj->connection_type();
    m_send_job->cloud_print_only = true;
    m_send_job->has_sdcard = m_obj->has_sdcard();
    m_send_job->set_check_mode();
    m_send_job->set_project_name("verify_job");

    m_send_job->on_check_ip_address_fail([this]() {
        this->check_ip_address_failed();
    });

    m_send_job->on_check_ip_address_success([this, ip, str_access_code]() {
        wxString input_str = wxString::Format("%s|%s", ip, str_access_code);
        auto event = wxCommandEvent(EVT_ENTER_IP_ADDRESS);
        event.SetString(input_str);
        event.SetEventObject(this);
        wxPostEvent(this, event);

        auto event_close = wxCommandEvent(EVT_CLOSE_IPADDRESS_DLG);
        event_close.SetEventObject(this);
        wxPostEvent(this, event_close);
    });

    m_send_job->start();
}

void InputIpAddressDialog::check_ip_address_failed()
{
    auto evt = new wxCommandEvent(EVT_CHECK_IP_ADDRESS_FAILED);
    wxQueueEvent(this, evt);
}

void InputIpAddressDialog::on_check_ip_address_failed(wxCommandEvent& evt)
{
    update_error_msg(_L("Error: IP or Access Code are not correct"));
    m_button_ok->Enable(true);
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
    m_button_ok->SetTextColor(StateColor::darkModeColorFor("#FFFFFE"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
}

void InputIpAddressDialog::on_text(wxCommandEvent& evt)
{
    auto str_ip = m_input_ip->GetTextCtrl()->GetValue();
    auto str_access_code = m_input_access_code->GetTextCtrl()->GetValue();

    if (isIp(str_ip.ToStdString()) && str_access_code.Length() == 8) {
        m_button_ok->Enable(true);
        StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
            std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
        m_button_ok->SetTextColor(StateColor::darkModeColorFor("#FFFFFE"));
        m_button_ok->SetBackgroundColor(btn_bg_green);
    }
    else {
        m_button_ok->Enable(false);
        m_button_ok->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
        m_button_ok->SetBorderColor(wxColour(0x90, 0x90, 0x90));
    }
}

InputIpAddressDialog::~InputIpAddressDialog()
{

}

void InputIpAddressDialog::on_dpi_changed(const wxRect& suggested_rect)
{

}


 }} // namespace Slic3r::GUI
