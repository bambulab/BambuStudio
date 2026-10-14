#include "MultiNozzleTpuTipsDialog.hpp"

#include <cctype>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/format.hpp>

#include <wx/html/htmlcell.h>
#include <wx/html/htmlwin.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/utils.h>

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "wxExtensions.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"

namespace Slic3r { namespace GUI {

static const char        *DONT_SHOW_AGAIN_KEY = "no_show_tpu_tips";
static const std::string  TPU_GUIDE_URL   = "";
static const int          CONTENT_WIDTH_DIP   = 460;
static const char        *TIPS_IMAGE_NAME       = "tpu_tips";
static const int          TIPS_IMAGE_HEIGHT_DIP = 204;

static const wxColour TextPrimaryColor   = wxColour("#262E30");
static const wxColour TextSecondaryColor = wxColour("#6B6B6B");
static const wxColour BrandGreenColor    = wxColour("#00AE42");

static std::string html_color(const wxColour &color)
{
    const wxColour c = StateColor::darkModeColorFor(color);
    return (boost::format("#%02X%02X%02X") % int(c.Red()) % int(c.Green()) % int(c.Blue())).str();
}

static bool is_tpu_filament(const std::string &preset_name)
{
    const Preset *preset = wxGetApp().preset_bundle->filaments.find_preset(preset_name, false);
    if (preset == nullptr)
        return false;
    const auto *types = preset->config.option<ConfigOptionStrings>("filament_type");
    return types != nullptr && !types->values.empty() && boost::algorithm::icontains(types->values.front(), "TPU");
}

bool MultiNozzleTpuTipsDialog::should_show(const std::string &new_preset)
{
    if (wxGetApp().app_config->get(DONT_SHOW_AGAIN_KEY) == "1")
        return false;
    if (!is_single_ext_multi_nozzle(wxGetApp().preset_bundle->printers.get_edited_preset().config))
        return false;
    return is_tpu_filament(new_preset);
}

MultiNozzleTpuTipsDialog::MultiNozzleTpuTipsDialog(wxWindow *parent)
    : DPIDialog(parent, wxID_ANY, _L("TPU Usage Notes"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    SetBackgroundColour(*wxWHITE);
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    const int content_width = FromDIP(CONTENT_WIDTH_DIP);
    const int padding       = FromDIP(24);

    auto *main_sizer = new wxBoxSizer(wxVERTICAL);

    m_image = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap(TIPS_IMAGE_NAME, this, TIPS_IMAGE_HEIGHT_DIP));
    main_sizer->Add(m_image, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, padding);

    wxString intro = _L("Before using TPU filament for the first time, please click here to view the");
    // Latin-script translations need a space before the link; CJK ones must not get one.
    if (!intro.empty()) {
        const wxUniChar last = intro.Last();
        if (last.IsAscii() && std::isalnum(static_cast<unsigned char>(last.GetValue())))
            intro += ' ';
    }

    // Rendered as HTML so the link flows inline after the intro text instead of wrapping as a separate block.
    m_guide_html = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition, wxSize(content_width, -1), wxHW_SCROLLBAR_NEVER | wxHW_NO_SELECTION);
    m_guide_html->SetBorders(0);
    const wxFont &font      = Label::Body_14;
    const int     font_size = font.GetPointSize();
    int           sizes[]   = {font_size, font_size, font_size, font_size, font_size, font_size, font_size};
    m_guide_html->SetFonts(font.GetFaceName(), font.GetFaceName(), sizes);
    m_guide_html->SetPage(wxString::FromUTF8("<html><body bgcolor=\"" + html_color(GetBackgroundColour()) + "\" text=\"" + html_color(TextPrimaryColor) +
                                             "\" link=\"" + html_color(BrandGreenColor) + "\">" + xml_escape(intro.ToUTF8().data()) + "<a href=\"guide\">" +
                                             xml_escape(_L("TPU User Guide").ToUTF8().data()) + "</a></body></html>"));
    m_guide_html->Bind(wxEVT_HTML_LINK_CLICKED, [](wxHtmlLinkEvent &) {
        if (!TPU_GUIDE_URL.empty())
            wxLaunchDefaultBrowser(wxString::FromUTF8(TPU_GUIDE_URL));
    });
    fit_guide_html();
    main_sizer->Add(m_guide_html, 0, wxLEFT | wxRIGHT | wxTOP, padding);

    auto *note_text = new Label(this, Label::Body_14,
                                _L("Note: If you are using TPU 2.85mm filament, the current printer preset must include a TPU285 nozzle. If the corresponding nozzle is "
                                   "installed on the printer, it is recommended to sync the printer information before use."));
    note_text->SetForegroundColour(TextSecondaryColor);
    note_text->Wrap(content_width);
    main_sizer->Add(note_text, 0, wxLEFT | wxRIGHT | wxTOP, padding);

    auto *bottom_sizer   = new wxBoxSizer(wxHORIZONTAL);
    m_dont_show_checkbox = new ::CheckBox(this);
    m_dont_show_checkbox->SetValue(false);
    bottom_sizer->Add(m_dont_show_checkbox, 0, wxALIGN_CENTER_VERTICAL);

    auto *dont_show_text = new Label(this, Label::Body_14, _L("Don't Remind Me"));
    dont_show_text->SetForegroundColour(TextPrimaryColor);
    dont_show_text->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { m_dont_show_checkbox->SetValue(!m_dont_show_checkbox->GetValue()); });
    bottom_sizer->Add(dont_show_text, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));
    bottom_sizer->AddStretchSpacer(1);

    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour("#1B8844"), StateColor::Pressed),
                            std::pair<wxColour, int>(wxColour("#3DCB73"), StateColor::Hovered),
                            std::pair<wxColour, int>(BrandGreenColor, StateColor::Normal));
    m_confirm_btn = new Button(this, _L("Got it"));
    m_confirm_btn->SetBackgroundColor(btn_bg_green);
    m_confirm_btn->SetBorderColor(BrandGreenColor);
    m_confirm_btn->SetTextColor(wxColour("#FFFFFE"));
    m_confirm_btn->SetFont(Label::Body_14);
    m_confirm_btn->SetMinSize(wxSize(FromDIP(74), FromDIP(28)));
    m_confirm_btn->SetCornerRadius(FromDIP(14));
    m_confirm_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        save_dont_show_choice();
        EndModal(wxID_OK);
    });
    bottom_sizer->Add(m_confirm_btn, 0, wxALIGN_CENTER_VERTICAL);
    main_sizer->Add(bottom_sizer, 0, wxEXPAND | wxALL, padding);

    // Closing with the title bar button still honors the checkbox.
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &e) {
        save_dont_show_choice();
        e.Skip();
    });

    SetSizer(main_sizer);
    Layout();
    Fit();
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void MultiNozzleTpuTipsDialog::fit_guide_html()
{
    const int            width = FromDIP(CONTENT_WIDTH_DIP);
    wxHtmlContainerCell *cell  = m_guide_html->GetInternalRepresentation();
    cell->Layout(width);
    m_guide_html->SetMinSize(wxSize(width, cell->GetHeight()));
}

void MultiNozzleTpuTipsDialog::save_dont_show_choice()
{
    if (!m_dont_show_checkbox->GetValue())
        return;
    wxGetApp().app_config->set(DONT_SHOW_AGAIN_KEY, "1");
    wxGetApp().app_config->save();
}

void MultiNozzleTpuTipsDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    m_dont_show_checkbox->Rescale();
    m_confirm_btn->Rescale();
    m_image->SetBitmap(create_scaled_bitmap(TIPS_IMAGE_NAME, this, TIPS_IMAGE_HEIGHT_DIP));
    fit_guide_html();
    Layout();
    Fit();
    Refresh();
}

}} // namespace Slic3r::GUI
