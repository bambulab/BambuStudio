#include "ImageMessageDialog.hpp"
#include "HMS.hpp"

#include "Widgets/Button.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"

namespace Slic3r {
namespace GUI
{

ImageMessageDialog::ImageMessageDialog(wxWindow *parent, wxWindowID id, const wxString &title, const wxString &message, const wxPoint &pos, const wxSize &size, long style)
    :DPIDialog(parent, id, title, pos, size, style)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));
    SetBackgroundColour(*wxWHITE);

    SetTitle(_L("Parameter recommendation"));

    auto        m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(380), 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));

    m_scroll_area = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_scroll_area->SetBackgroundColour(*wxWHITE);

    wxBoxSizer* text_sizer = new wxBoxSizer(wxVERTICAL);

    int target_w = FromDIP(380);
    int target_h = FromDIP(270);
    std::string image_path = (boost::format("%1%/images/clumping.png") % resources_dir()).str();
    wxImage* image = new wxImage(image_path, wxBITMAP_TYPE_PNG);
    if (image && image->IsOk()) {
        *image = image->Scale(target_w, target_h, wxIMAGE_QUALITY_HIGH);
    }
    wxBitmap bitmap   = wxBitmap(*image);
    m_error_msg_label = new Label(m_scroll_area, message, LB_AUTO_WRAP);
    m_error_picture   = new wxStaticBitmap(m_scroll_area, wxID_ANY, bitmap, wxDefaultPosition, wxSize(target_w, target_h));

    text_sizer->Add(m_error_picture, 0, wxALIGN_CENTER, FromDIP(5));
    text_sizer->AddSpacer(10);
    text_sizer->Add(m_error_msg_label, 0, wxEXPAND, FromDIP(5));

    m_scroll_area->SetSizer(text_sizer);

    Button *msg_button = new Button(this, _L("Click for more details"));
    msg_button->SetBackgroundColor(btn_bg_white);
    msg_button->SetBorderColor(wxColour(38, 46, 48));
    msg_button->SetFont(Label::Body_14);
    msg_button->SetSize(wxSize(FromDIP(300), FromDIP(30)));
    msg_button->SetMinSize(wxSize(FromDIP(300), FromDIP(30)));
    msg_button->SetMaxSize(wxSize(-1, FromDIP(30)));
    msg_button->SetCornerRadius(FromDIP(5));

    msg_button->Bind(wxEVT_LEFT_DOWN, [this, style](wxMouseEvent &e) {
        std::string language = wxGetApp().app_config->get("language");
        wxString    region   = L"en";
        if (language.find("zh") == 0)
            region = L"zh";
        const wxString wiki_link = wxString::Format(L"https://wiki.bambulab.com/%s/software/bambu-studio/nozzle-clumping-detection-by-probing", region);
        wxGetApp().open_browser_with_warning_dialog(wiki_link);
        e.Skip();
    });

    auto bottom_sizer = new wxBoxSizer(wxVERTICAL);
    m_sizer_button    = new wxBoxSizer(wxVERTICAL);
    m_sizer_button->Add(msg_button, 0, wxALIGN_CENTER, FromDIP(5));
    bottom_sizer->Add(m_sizer_button, 0, wxEXPAND | wxRIGHT | wxLEFT, 0);

    wxBoxSizer* m_center_sizer = new wxBoxSizer(wxVERTICAL);
    m_center_sizer->Add(0, 0, 1, wxTOP, FromDIP(5));
    m_center_sizer->Add(m_scroll_area, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(15));
    m_center_sizer->Add(bottom_sizer, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(20));
    m_center_sizer->Add(0, 0, 0, wxTOP, FromDIP(10));

    m_sizer_main = new wxBoxSizer(wxVERTICAL);
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(5));
    m_sizer_main->Add(m_center_sizer, 0, wxBOTTOM | wxEXPAND, FromDIP(5));

    SetSizer(m_sizer_main);
    Layout();
    m_sizer_main->Fit(this);
    m_sizer_main->SetSizeHints(this);

    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

ImageMessageDialog::~ImageMessageDialog()
{
    m_error_picture->SetBitmap(wxBitmap());
}

void ImageMessageDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    for (auto used_button : m_used_button) { used_button->Rescale();}
    wxGetApp().UpdateDlgDarkUI(this);
    Refresh();
}
}
} // namespace Slic3r::GUI
