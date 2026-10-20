#include "MultiNozzleTpuTipsDialog.hpp"

#include <algorithm>
#include <cctype>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/format.hpp>

#include <wx/bitmap.h>
#include <wx/dcmemory.h>
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
static const char        *WARN_ICON_NAME        = "warning";
static const int          WARN_ICON_SIZE_DIP    = 16;
static const int          BULLET_INDENT_DIP     = WARN_ICON_SIZE_DIP + 6;
static const int          BUTTON_HEIGHT_DIP     = 32;
static const int          TIPS_LINE_GAP_DIP     = 2;

static const wxColour TextPrimaryColor   = wxColour("#262E30");
static const wxColour TextSecondaryColor = wxColour("#6B6B6B");
static const wxColour BrandGreenColor    = wxColour("#00AE42");

// One label per wrapped line, with a small gap, because wxStaticText has no line-spacing control.
static void layout_tips_text(wxWindow *parent, wxBoxSizer *sizer, const wxString &text, int wrap_width)
{
    sizer->Clear(true);

    wxMemoryDC dc;
    wxBitmap   bmp(1, 1);
    dc.SelectObject(bmp);
    dc.SetFont(Label::Body_14);

    wxString wrapped;
    Label::split_lines(dc, std::max(wrap_width, 1), text, wrapped);
    dc.SelectObject(wxNullBitmap);

    const int gap   = parent->FromDIP(TIPS_LINE_GAP_DIP);
    bool      first = true;
    wxString  line;
    auto      add_line = [&](const wxString &content) {
        if (!first)
            sizer->AddSpacer(gap);
        first = false;
        auto *label = new Label(parent, Label::Body_14, content);
        label->SetForegroundColour(TextSecondaryColor);
        sizer->Add(label, 0, wxEXPAND);
    };

    for (size_t i = 0; i < wrapped.size(); ++i) {
        if (wrapped[i] == '\n') {
            add_line(line);
            line.clear();
        } else {
            line.append(wrapped[i]);
        }
    }
    add_line(line);
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
    const int bullet_indent = FromDIP(BULLET_INDENT_DIP);

    auto *main_sizer = new wxBoxSizer(wxVERTICAL);

    m_image = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap(TIPS_IMAGE_NAME, this, TIPS_IMAGE_HEIGHT_DIP));
    main_sizer->Add(m_image, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, padding);

    // "&&" is the Win32 mnemonic escape; a single "&" is not drawn.
    m_heading_label = new Label(this, Label::Head_16, _L("TPU Filament Installation && Feeding Guide"));
    m_heading_label->SetForegroundColour(TextPrimaryColor);
    m_heading_label->Wrap(content_width);
    main_sizer->AddSpacer(FromDIP(16));
    main_sizer->Add(m_heading_label, 0, wxLEFT | wxRIGHT, padding);

    auto *title_row = new wxBoxSizer(wxHORIZONTAL);
    m_warn_icon     = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap(WARN_ICON_NAME, this, WARN_ICON_SIZE_DIP), wxDefaultPosition,
                                         wxSize(FromDIP(WARN_ICON_SIZE_DIP), FromDIP(WARN_ICON_SIZE_DIP)));
    auto *title_label = new Label(this, Label::Body_14, _L("Important Notice:"));
    title_label->SetForegroundColour(TextPrimaryColor);
    title_row->Add(m_warn_icon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    title_row->Add(title_label, 0, wxALIGN_CENTER_VERTICAL);
    main_sizer->AddSpacer(FromDIP(16));
    main_sizer->Add(title_row, 0, wxLEFT | wxRIGHT, padding);

    m_tips_text = _L(
        "• First time printing with TPU ? We strongly recommend reading the official guide first to avoid clogs or feeding failures.\n"
        "• If you are using 2.85 mm TPU, make sure a TPU285 nozzle is installed and the device information has been synced.");
    // Indent bullets so they line up with the notice text, past the warning icon.
    auto *tips_row = new wxBoxSizer(wxHORIZONTAL);
    tips_row->AddSpacer(bullet_indent);
    m_tips_sizer = new wxBoxSizer(wxVERTICAL);
    layout_tips_text(this, m_tips_sizer, m_tips_text, content_width - bullet_indent);
    tips_row->Add(m_tips_sizer, 1, wxEXPAND);
    main_sizer->AddSpacer(FromDIP(8));
    main_sizer->Add(tips_row, 0, wxEXPAND | wxLEFT | wxRIGHT, padding);

    auto *check_sizer    = new wxBoxSizer(wxHORIZONTAL);
    m_dont_show_checkbox = new ::CheckBox(this);
    m_dont_show_checkbox->SetValue(false);
    check_sizer->Add(m_dont_show_checkbox, 0, wxALIGN_CENTER_VERTICAL);

    auto *dont_show_text = new Label(this, Label::Body_14, _L("Don't show this again"));
    dont_show_text->SetForegroundColour(TextPrimaryColor);
    dont_show_text->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { m_dont_show_checkbox->SetValue(!m_dont_show_checkbox->GetValue()); });
    check_sizer->Add(dont_show_text, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));
    main_sizer->Add(check_sizer, 0, wxLEFT | wxRIGHT | wxTOP, padding);

    auto *btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour("#1B8844"), StateColor::Pressed),
                            std::pair<wxColour, int>(wxColour("#3DCB73"), StateColor::Hovered),
                            std::pair<wxColour, int>(BrandGreenColor, StateColor::Normal));
    m_guide_btn = new Button(this, _L("View User Guide"));
    m_guide_btn->SetBackgroundColor(btn_bg_green);
    m_guide_btn->SetBorderColor(BrandGreenColor);
    m_guide_btn->SetTextColor(wxColour("#FFFFFE"));
    m_guide_btn->SetFont(Label::Body_14);
    m_guide_btn->SetPaddingSize(wxSize(FromDIP(18), FromDIP(4)));
    m_guide_btn->SetMinSize(wxSize(-1, FromDIP(BUTTON_HEIGHT_DIP)));
    m_guide_btn->SetCornerRadius(FromDIP(BUTTON_HEIGHT_DIP / 2));
    m_guide_btn->Bind(wxEVT_BUTTON, [](wxCommandEvent &) {
        if (!TPU_GUIDE_URL.empty())
            wxLaunchDefaultBrowser(wxString::FromUTF8(TPU_GUIDE_URL));
    });
    btn_sizer->Add(m_guide_btn, 0, wxALIGN_CENTER_VERTICAL);

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
                            std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                            std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));
    m_confirm_btn = new Button(this, _L("I know risks, continue"));
    m_confirm_btn->SetBackgroundColor(btn_bg_white);
    m_confirm_btn->SetBorderColor(wxColour("#C2C2C2"));
    m_confirm_btn->SetTextColor(TextPrimaryColor);
    m_confirm_btn->SetFont(Label::Body_14);
    m_confirm_btn->SetPaddingSize(wxSize(FromDIP(18), FromDIP(4)));
    m_confirm_btn->SetMinSize(wxSize(-1, FromDIP(BUTTON_HEIGHT_DIP)));
    m_confirm_btn->SetCornerRadius(FromDIP(BUTTON_HEIGHT_DIP / 2));
    m_confirm_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        save_dont_show_choice();
        EndModal(wxID_OK);
    });
    btn_sizer->Add(m_confirm_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));
    main_sizer->Add(btn_sizer, 0, wxALIGN_RIGHT | wxRIGHT | wxTOP, padding);
    main_sizer->AddSpacer(padding);

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
    if (m_heading_label)
        m_heading_label->Wrap(FromDIP(CONTENT_WIDTH_DIP));
    if (m_warn_icon)
        m_warn_icon->SetBitmap(create_scaled_bitmap(WARN_ICON_NAME, this, WARN_ICON_SIZE_DIP));
    if (m_tips_sizer) {
        layout_tips_text(this, m_tips_sizer, m_tips_text, FromDIP(CONTENT_WIDTH_DIP - BULLET_INDENT_DIP));
        for (size_t i = 0; i < m_tips_sizer->GetItemCount(); ++i) {
            if (wxWindow *win = m_tips_sizer->GetItem(i)->GetWindow())
                wxGetApp().UpdateDarkUI(win);
        }
    }
    Layout();
    Fit();
    Refresh();
}

}} // namespace Slic3r::GUI
