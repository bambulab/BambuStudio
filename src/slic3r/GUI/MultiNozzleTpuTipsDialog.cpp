#include "MultiNozzleTpuTipsDialog.hpp"

#include <cctype>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/format.hpp>

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
static const std::string  TPU_GUIDE_URL   = "https://e.bambulab.com/t?c=NslBZddIdsBrLEoG";
static const int          CONTENT_WIDTH_DIP   = 460;
static const char        *TIPS_IMAGE_NAME       = "tpu_tips";
static const int          TIPS_IMAGE_HEIGHT_DIP = 204;
static const char        *WARN_ICON_NAME        = "text_obj_warning";
static const int          WARN_ICON_SIZE_DIP    = 16;

static const wxColour TextPrimaryColor   = wxColour("#262E30");
static const wxColour TextSecondaryColor = wxColour("#6B6B6B");
static const wxColour BrandGreenColor    = wxColour("#00AE42");

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

    // text_obj_warning.svg stands in for the warning glyph, which the UI font may not contain.
    auto *title_row = new wxBoxSizer(wxHORIZONTAL);
    m_warn_icon     = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap(WARN_ICON_NAME, this, WARN_ICON_SIZE_DIP), wxDefaultPosition,
                                         wxSize(FromDIP(WARN_ICON_SIZE_DIP), FromDIP(WARN_ICON_SIZE_DIP)));
    auto *title_label = new Label(this, Label::Body_14, _L("Important Notice:"));
    title_label->SetForegroundColour(TextPrimaryColor);
    title_row->Add(m_warn_icon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    title_row->Add(title_label, 0, wxALIGN_CENTER_VERTICAL);
    main_sizer->Add(title_row, 0, wxLEFT | wxRIGHT | wxTOP, padding);

    wxString tips_text = _L(
        "• Using TPU filament for the first time? Read the official guide first to avoid clogs or feeding failures.\n"
        "• If you are using 2.85 mm TPU, make sure a TPU285 nozzle is installed and the device information has been synced.");
    m_tips_label = new Label(this, Label::Body_14, tips_text);
    m_tips_label->SetForegroundColour(TextPrimaryColor);
    m_tips_label->Wrap(content_width);
    // Side inset matches the button row, so the body lines up with the checkbox and buttons.
    main_sizer->AddSpacer(FromDIP(8));
    main_sizer->Add(m_tips_label, 0, wxLEFT | wxRIGHT, padding);

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
    m_guide_btn = new Button(this, _L("View User Guide"));
    m_guide_btn->SetBackgroundColor(btn_bg_green);
    m_guide_btn->SetBorderColor(BrandGreenColor);
    m_guide_btn->SetTextColor(wxColour("#FFFFFE"));
    m_guide_btn->SetFont(Label::Body_14);
    m_guide_btn->SetMinSize(wxSize(FromDIP(146), FromDIP(28)));
    m_guide_btn->SetCornerRadius(FromDIP(14));
    m_guide_btn->Bind(wxEVT_BUTTON, [](wxCommandEvent &) {
        if (!TPU_GUIDE_URL.empty())
            wxLaunchDefaultBrowser(wxString::FromUTF8(TPU_GUIDE_URL));
    });
    bottom_sizer->Add(m_guide_btn, 0, wxALIGN_CENTER_VERTICAL);

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
                            std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                            std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));
    m_confirm_btn = new Button(this, _L("Got it"));
    m_confirm_btn->SetBackgroundColor(btn_bg_white);
    m_confirm_btn->SetBorderColor(TextPrimaryColor);
    m_confirm_btn->SetTextColor(TextPrimaryColor);
    m_confirm_btn->SetFont(Label::Body_14);
    m_confirm_btn->SetMinSize(wxSize(FromDIP(74), FromDIP(28)));
    m_confirm_btn->SetCornerRadius(FromDIP(14));
    m_confirm_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        save_dont_show_choice();
        EndModal(wxID_OK);
    });
    bottom_sizer->Add(m_confirm_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
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
    if (m_guide_btn)
        m_guide_btn->Rescale();
    m_confirm_btn->Rescale();
    m_image->SetBitmap(create_scaled_bitmap(TIPS_IMAGE_NAME, this, TIPS_IMAGE_HEIGHT_DIP));
    if (m_warn_icon)
        m_warn_icon->SetBitmap(create_scaled_bitmap(WARN_ICON_NAME, this, WARN_ICON_SIZE_DIP));
    if (m_tips_label)
        m_tips_label->Wrap(FromDIP(CONTENT_WIDTH_DIP));
    Layout();
    Fit();
    Refresh();
}

}} // namespace Slic3r::GUI
