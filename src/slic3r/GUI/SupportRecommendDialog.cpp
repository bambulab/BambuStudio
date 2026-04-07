#include "SupportRecommendDialog.hpp"
#include "I18N.hpp"

#include "GUI_App.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/CheckBox.hpp"

#define BODY_ITEM_WIDTH 200
#define SUPPORT_ITEM_WIDTH 200
#define PARAM_ITEM_WIDTH 350

namespace Slic3r {
namespace GUI {

SupportComboCard::SupportComboCard(wxWindow                                  *parent,
                                   const std::vector<wxString>               &objects,
                                   const std::vector<std::tuple<int, wxColour, wxString>> &mainMat,
                                   const std::tuple<int, wxColour, wxString> &supportMat,
                                   const wxArrayString                       &params)
    : wxPanel(parent, wxID_ANY)
{
    create_ui(objects, mainMat, supportMat, params);
    wxGetApp().UpdateDarkUIWin(this);
    restore_color();
}

void SupportComboCard::restore_color()
{
    for (auto& pair : m_color_boxes) {
        pair.first->SetBackgroundColour(pair.second);
    }
    for (auto& pair : m_color_texts) {
        pair.first->SetForegroundColour(pair.second);
    }
    Refresh();
}

void SupportComboCard::create_ui(const std::vector<wxString>               &objects,
                                 const std::vector<std::tuple<int, wxColour, wxString>> &mainMat,
                                 const std::tuple<int, wxColour, wxString> &supportMat,
                                 const wxArrayString                       &params)
{
    SetBackgroundColour(wxColour("#F7F7F7"));
    SetMinSize(wxSize(-1, FromDIP(250)));
    SetMaxSize(wxSize(-1, FromDIP(250)));

    wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);
    this->SetSizer(mainSizer);

    wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText *objectsLabel  = new wxStaticText(this, wxID_ANY, _L("Availabale Objects"));
    leftSizer->Add(objectsLabel, 0, wxALL, 5);
    objectsLabel->SetForegroundColour(wxColour("#6B6B6B"));
    wxScrolledWindow *objectsScroll = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    objectsScroll->SetScrollRate(0, 10);
    objectsScroll->SetBackgroundColour(wxColour("#F7F7F7"));
    objectsScroll->SetMinSize(wxSize(FromDIP(150), -1));

    wxBoxSizer *objectsSizer = new wxBoxSizer(wxVERTICAL);
    objectsScroll->SetSizer(objectsSizer);

    for (const auto& obj : objects) {
        wxStaticText* text = new wxStaticText(objectsScroll, wxID_ANY, obj);
        objectsSizer->Add(text, 0, wxALL, 5);
    }
    leftSizer->Add(objectsScroll, 1, wxEXPAND);
    
    mainSizer->Add(leftSizer, 0, wxEXPAND | wxRIGHT, 10);

    wxPanel *separator1 = new wxPanel(this, wxID_ANY);
    separator1->SetBackgroundColour(wxColour("#CECECE"));
    separator1->SetSize(wxSize(FromDIP(1), -1));
    mainSizer->Add(separator1, 0, wxEXPAND | wxALL, 5);

    wxBoxSizer *rightSizer = new wxBoxSizer(wxVERTICAL);
    
    wxBoxSizer *topSizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *mainMatAreaSizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText* mainMatLabel = new wxStaticText(this, wxID_ANY, _L("Main Material"));
    mainMatLabel->SetForegroundColour(wxColour("#6B6B6B"));
    mainMatAreaSizer->Add(mainMatLabel, 0, wxALL, 5);    

    wxScrolledWindow *mainMatScroll = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    mainMatScroll->SetScrollRate(0, 10);
    mainMatScroll->SetBackgroundColour(wxColour("#F7F7F7"));
    mainMatScroll->SetMinSize(wxSize(FromDIP(BODY_ITEM_WIDTH), FromDIP(100)));

    wxBoxSizer *mainMatSizer = new wxBoxSizer(wxVERTICAL);
    mainMatScroll->SetSizer(mainMatSizer);

    for (const auto &mat : mainMat) {
        wxPanel *matItemPanel = new wxPanel(mainMatScroll, wxID_ANY);
        matItemPanel->SetBackgroundColour(wxColour("#F7F7F7"));
        wxBoxSizer *matItemSizer = new wxBoxSizer(wxHORIZONTAL);

        wxPanel *colorBox = new wxPanel(matItemPanel, wxID_ANY);
        wxColour color    = std::get<1>(mat);
        colorBox->SetBackgroundColour(color);
        colorBox->SetMinSize(wxSize(FromDIP(24), FromDIP(24)));
        colorBox->SetMaxSize(wxSize(FromDIP(24), FromDIP(24)));

        m_color_boxes.push_back(std::make_pair(colorBox, color));

        wxBoxSizer   *colorBoxSizer = new wxBoxSizer(wxVERTICAL);
        wxStaticText *numberText    = new wxStaticText(colorBox, wxID_ANY, wxString::Format("%d", std::get<0>(mat)));
        wxColour textColor = color.GetLuminance() >= 0.51 ? wxColour("#000001") : wxColour("#FFFFFE");
        m_color_texts.push_back(std::make_pair(numberText, textColor));

        colorBoxSizer->AddStretchSpacer(1);
        colorBoxSizer->Add(numberText, 1, wxALIGN_CENTER_HORIZONTAL);
        colorBoxSizer->AddStretchSpacer(1);
        colorBox->SetSizer(colorBoxSizer);

        matItemSizer->Add(colorBox, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

        wxStaticText *nameText = new wxStaticText(matItemPanel, wxID_ANY, std::get<2>(mat));
        matItemSizer->Add(nameText, 1, wxALIGN_CENTER_VERTICAL);

        matItemPanel->SetSizer(matItemSizer);
        mainMatSizer->Add(matItemPanel, 0, wxEXPAND | wxALL, 5);
    }

    mainMatAreaSizer->Add(mainMatScroll, 1, wxEXPAND);
    topSizer->Add(mainMatAreaSizer, 1, wxEXPAND | wxRIGHT, 10);

    wxPanel *separator2 = new wxPanel(this, wxID_ANY);
    separator2->SetBackgroundColour(wxColour("#CECECE"));
    separator2->SetSize(wxSize(FromDIP(1), -1));
    topSizer->Add(separator2, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 5);

    wxBoxSizer   *supportMatAreaSizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText *supportMatLabel     = new wxStaticText(this, wxID_ANY, _L("Support Material"));
    supportMatLabel->SetForegroundColour(wxColour("#6B6B6B"));
    supportMatAreaSizer->Add(supportMatLabel, 0, wxALL, 5);

    wxPanel *supportMatPanel = new wxPanel(this, wxID_ANY);
    supportMatPanel->SetBackgroundColour(wxColour("#F7F7F7"));
    supportMatPanel->SetMinSize(wxSize(FromDIP(SUPPORT_ITEM_WIDTH), FromDIP(100)));

    wxBoxSizer *supportMatContentSizer = new wxBoxSizer(wxVERTICAL);

    wxPanel *supportItemPanel = new wxPanel(supportMatPanel, wxID_ANY);
    supportItemPanel->SetBackgroundColour(wxColour("#F7F7F7"));
    wxBoxSizer *supportItemSizer = new wxBoxSizer(wxHORIZONTAL);

    wxPanel *supportColorBox = new wxPanel(supportItemPanel, wxID_ANY);
    wxColour supportColor    = std::get<1>(supportMat);
    supportColorBox->SetBackgroundColour(supportColor);
    supportColorBox->SetMinSize(wxSize(FromDIP(24), FromDIP(24)));
    supportColorBox->SetMaxSize(wxSize(FromDIP(24), FromDIP(24)));
    m_color_boxes.push_back(std::make_pair(supportColorBox, supportColor));

    wxBoxSizer   *supportColorBoxSizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText *supportNumberText    = new wxStaticText(supportColorBox, wxID_ANY, wxString::Format("%d", std::get<0>(supportMat)));
    wxColour textColor = supportColor.GetLuminance() >= 0.51 ? wxColour("#000001") : wxColour("#FFFFFE");
    m_color_texts.push_back(std::make_pair(supportNumberText, textColor));

    supportColorBoxSizer->AddStretchSpacer(1);
    supportColorBoxSizer->Add(supportNumberText, 1, wxALIGN_CENTER_HORIZONTAL);
    supportColorBoxSizer->AddStretchSpacer(1);
    supportColorBox->SetSizer(supportColorBoxSizer);

    supportItemSizer->Add(supportColorBox, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    wxStaticText *supportNameText = new wxStaticText(supportItemPanel, wxID_ANY, std::get<2>(supportMat));
    supportItemSizer->Add(supportNameText, 1, wxALIGN_CENTER_VERTICAL);

    supportItemPanel->SetSizer(supportItemSizer);
    supportMatContentSizer->Add(supportItemPanel, 0, wxEXPAND | wxALL, 5);

    supportMatPanel->SetSizer(supportMatContentSizer);
    supportMatAreaSizer->Add(supportMatPanel, 1, wxEXPAND);
    topSizer->Add(supportMatAreaSizer, 1, wxEXPAND);

    rightSizer->Add(topSizer, 0, wxEXPAND | wxBOTTOM, 10);

    wxPanel *separator3 = new wxPanel(this, wxID_ANY);
    separator3->SetBackgroundColour(wxColour("#CECECE"));
    separator3->SetSize(wxSize(-1, FromDIP(1)));
    rightSizer->Add(separator3, 0, wxEXPAND | wxALL, 5);

    wxBoxSizer *paramsAreaSizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText *paramsLabel = new wxStaticText(this, wxID_ANY, _L("Recommended Parameters"));
    paramsLabel->SetForegroundColour(wxColour("#6B6B6B"));
    paramsAreaSizer->Add(paramsLabel, 0, wxALL, 5);

    wxScrolledWindow *paramsScroll = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    paramsScroll->SetScrollRate(0, 10);
    paramsScroll->SetBackgroundColour(wxColour("#F7F7F7"));
    
    wxGridSizer *paramsSizer = new wxGridSizer(0, 2, 5, 5);
    paramsScroll->SetSizer(paramsSizer);

    for (const auto& param : params) {
        wxStaticText *paramText = new wxStaticText(paramsScroll, wxID_ANY, param);
        paramsSizer->Add(paramText, 0, wxALL, 5);
    }
    paramsAreaSizer->Add(paramsScroll, 1, wxEXPAND);
    rightSizer->Add(paramsAreaSizer, 1, wxEXPAND);

    mainSizer->Add(rightSizer, 1, wxEXPAND);

    Layout();
    GUI::wxGetApp().UpdateDarkUIWin(this);
}

SupportRecommendDialog::SupportRecommendDialog(wxWindow* parent, const wxString& title)
    : DPIDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX)
{
    create_ui();
    Slic3r::GUI::wxGetApp().UpdateDlgDarkUI(this);

    Bind(wxEVT_CLOSE_WINDOW, &SupportRecommendDialog::on_close, this);
}

void SupportRecommendDialog::create_ui()
{
    constexpr int margin = 30;
    this->SetDoubleBuffered(true);
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    int scroll_width = FromDIP(BODY_ITEM_WIDTH + SUPPORT_ITEM_WIDTH + PARAM_ITEM_WIDTH);
    m_main_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(m_main_sizer);

    m_tip_text = new Label(this, _L("Compatible model and support filaments detected. It is recommended to use the suggested filaments for printing the support interfaces of these objects, along with the recommended parameters."));
    m_tip_text->Wrap(scroll_width);
    m_tip_text->SetFont(::Label::Body_14);
    m_tip_text->SetBackgroundColour(*wxWHITE);
    m_main_sizer->Add(m_tip_text, 0, wxALL | wxEXPAND, margin);

    // filament area
    m_scroll_panel = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_scroll_panel->SetMinSize(wxSize(scroll_width, FromDIP(300)));
    m_scroll_panel->SetScrollRate(0, 20);
    m_scroll_sizer = new wxBoxSizer(wxVERTICAL);
    m_scroll_panel->SetSizer(m_scroll_sizer);

    m_main_sizer->Add(m_scroll_panel, 1, wxLEFT | wxRIGHT, margin);

    // btns
    wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);

    m_dont_show_again = new CheckBox(this);
    m_dont_show_again->SetValue(false);

    Label *dont_show_label = new Label(this, _L("Don't remind me again"));
    dont_show_label->SetBackgroundColour(*wxWHITE);
    dont_show_label->SetForegroundColour(wxColour("#6B6B6B"));

    btnSizer->Add(m_dont_show_again, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    btnSizer->Add(dont_show_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

    m_cancel_btn = new Button(this, _L("Cancel"));
    m_cancel_btn->Bind(wxEVT_BUTTON, &SupportRecommendDialog::on_cancel_click, this);

    const auto& apply_style = StateColor(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
                                         std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                                         std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    m_apply_btn = new Button(this, _L("Apply"));
    m_apply_btn->Bind(wxEVT_BUTTON, &SupportRecommendDialog::on_apply_click, this);
    m_apply_btn->SetBackgroundColor(apply_style);
    m_apply_btn->SetForegroundColour(*wxWHITE);

    btnSizer->AddStretchSpacer();
    btnSizer->Add(m_cancel_btn, 0, wxRIGHT, 10);
    btnSizer->Add(m_apply_btn);
    m_main_sizer->Add(btnSizer, 0, wxALL | wxEXPAND, margin);

    SetMinSize(wxSize(FromDIP(720), FromDIP(350)));
    SetBackgroundColour(*wxWHITE);
    Fit();
    Layout();
    CenterOnParent();
}

void SupportRecommendDialog::AddSupportComboCard(const std::vector<wxString>                            &objects,
                                                 const std::vector<std::tuple<int, wxColour, wxString>> &mainMat,
                                                 const std::tuple<int, wxColour, wxString>              &supportMat,
                                                 const wxArrayString                                    &params)
{
    SupportComboCard* card = new SupportComboCard(m_scroll_panel, objects, mainMat, supportMat, params);
    m_scroll_sizer->Add(card, 0, wxEXPAND | wxBOTTOM, 10);
    Layout();
}

void SupportRecommendDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    m_cancel_btn->Rescale();
    m_apply_btn->Rescale();
    Layout();
}

void SupportRecommendDialog::save_dont_show_again()
{
    if (m_dont_show_again->GetValue()) {
        wxGetApp().app_config->set("show_support_recommend_dialog", "false");
    }
}

void SupportRecommendDialog::SetTipText(const wxString& text)
{
    m_tip_text->Wrap(FromDIP(600));
    m_tip_text->SetLabel(text);
}

void SupportRecommendDialog::SetComboTitle(const wxString& title)
{
    m_combo_title->Wrap(FromDIP(600));
    m_combo_title->SetLabel(title);
}

} // namespace GUI
} // namespace Slic3r
