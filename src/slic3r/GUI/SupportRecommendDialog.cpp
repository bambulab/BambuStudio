#include "SupportRecommendDialog.hpp"
#include "I18N.hpp"

#include "GUI_App.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"

#define BODY_ITEM_WIDTH 200
#define SUPPORT_ITEM_WIDTH 200
#define PARAM_ITEM_WIDTH 350

namespace Slic3r {
namespace GUI {

SupportComboCard::SupportComboCard(wxWindow* parent,
                                   const wxString& mainMat,
                                   const wxString& supportMat,
                                   const wxArrayString& params)
    : wxPanel(parent, wxID_ANY)
{
    create_ui(mainMat, supportMat, params);
}

void SupportComboCard::create_ui(const wxString& mainMat,
                                 const wxString& supportMat,
                                 const wxArrayString& params)
{
    SetBackgroundColour(wxColour("#EEE"));

    wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);
    this->SetSizer(mainSizer);

    wxPanel* mat1Panel = new wxPanel(this, wxID_ANY);
    mat1Panel->SetBackgroundColour(wxColour("#EEE"));
    wxBoxSizer* mat1Sizer = new wxBoxSizer(wxHORIZONTAL);

    wxStaticText* text1 = new wxStaticText(mat1Panel, wxID_ANY, mainMat);
    mat1Sizer->Add(text1, 1, wxALL | wxALIGN_CENTER_VERTICAL, 8);
    mat1Panel->SetSizer(mat1Sizer);
    mat1Panel->SetMinSize(wxSize(FromDIP(BODY_ITEM_WIDTH), -1));
    mainSizer->Add(mat1Panel, 0, wxEXPAND | wxALL);

    wxPanel* mat2Panel = new wxPanel(this, wxID_ANY);
    mat2Panel->SetBackgroundColour(wxColour("#EEE"));
    wxBoxSizer* mat2Sizer = new wxBoxSizer(wxHORIZONTAL);

    wxStaticText* text2 = new wxStaticText(mat2Panel, wxID_ANY, supportMat);

    mat2Sizer->Add(text2, 1, wxALL | wxALIGN_CENTER_VERTICAL, 8);
    mat2Panel->SetSizer(mat2Sizer);
    mat2Panel->SetMinSize(wxSize(FromDIP(SUPPORT_ITEM_WIDTH), -1));
    mainSizer->Add(mat2Panel, 0, wxEXPAND | wxALL);

    wxPanel* rightPanel = new wxPanel(this, wxID_ANY);
    wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);
    rightPanel->SetSizer(rightSizer);
    rightPanel->SetBackgroundColour(wxColour("#EEE"));
    rightPanel->SetMinSize(wxSize(FromDIP(PARAM_ITEM_WIDTH), -1));
    rightPanel->SetMaxSize(wxSize(FromDIP(PARAM_ITEM_WIDTH), -1));
    rightPanel->SetSize(wxSize(FromDIP(PARAM_ITEM_WIDTH), -1));

    for (const auto& param : params) {
        wxStaticText* paramText = new wxStaticText(rightPanel, wxID_ANY, param);
        paramText->SetMinSize(wxSize(FromDIP(PARAM_ITEM_WIDTH), -1));
        paramText->SetMaxSize(wxSize(FromDIP(PARAM_ITEM_WIDTH), -1));
        paramText->SetSize(wxSize(FromDIP(PARAM_ITEM_WIDTH), -1));
        paramText->SetBackgroundColour(wxColour("#EEE"));
        rightSizer->Add(paramText, 0, wxTOP | wxALIGN_LEFT, 5);
    }

    mainSizer->Add(rightPanel, 1, wxEXPAND | wxALL, 10);
    Layout();
}

SupportRecommendDialog::SupportRecommendDialog(wxWindow* parent, const wxString& title)
    : DPIDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX)
{
    create_ui();
    Slic3r::GUI::wxGetApp().UpdateDlgDarkUI(this);
}

void SupportRecommendDialog::create_ui()
{
    this->SetDoubleBuffered(true);
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    m_main_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(m_main_sizer);

    m_tip_text = new Label(this, "");
    m_tip_text->SetFont(::Label::Body_14);
    m_tip_text->SetBackgroundColour(*wxWHITE);
    m_main_sizer->Add(m_tip_text, 0, wxALL | wxEXPAND, 30);

    m_combo_title = new Label(this, "");
    m_combo_title->SetBackgroundColour(*wxWHITE);
    m_combo_title->SetFont(::Label::Body_12);
    m_main_sizer->Add(m_combo_title, 0, wxLEFT | wxBOTTOM, 30);

    // filament area
    int scroll_width = FromDIP(BODY_ITEM_WIDTH + SUPPORT_ITEM_WIDTH + PARAM_ITEM_WIDTH);
    m_scroll_panel = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_scroll_panel->SetMinSize(wxSize(scroll_width, FromDIP(200)));
    m_scroll_panel->SetScrollRate(0, 20);
    m_scroll_sizer = new wxBoxSizer(wxVERTICAL);
    m_scroll_panel->SetSizer(m_scroll_sizer);

    // Title row
    wxSizer* title_sizer = new wxBoxSizer(wxHORIZONTAL);
    Label* main_mat_label = new Label(m_scroll_panel, _L("Main Material"), wxALIGN_CENTER);
    main_mat_label->SetFont(::Label::Body_12);
    main_mat_label->SetBackgroundColour(*wxWHITE);
    main_mat_label->SetMinSize(wxSize(FromDIP(BODY_ITEM_WIDTH), -1));
    main_mat_label->SetMaxSize(wxSize(FromDIP(BODY_ITEM_WIDTH), -1));
    title_sizer->Add(main_mat_label, 0, wxALIGN_CENTER_HORIZONTAL);

    Label* support_mat_label = new Label(m_scroll_panel, _L("Support Material"), wxALIGN_CENTER);
    support_mat_label->SetFont(::Label::Body_12);
    support_mat_label->SetBackgroundColour(*wxWHITE);
    support_mat_label->SetMinSize(wxSize(FromDIP(SUPPORT_ITEM_WIDTH), -1));
    support_mat_label->SetMaxSize(wxSize(FromDIP(SUPPORT_ITEM_WIDTH), -1));
    title_sizer->Add(support_mat_label, 0, wxALIGN_CENTER_HORIZONTAL);

    Label* param_label = new Label(m_scroll_panel, _L("Parameters"), wxALIGN_CENTER);
    param_label->SetFont(::Label::Body_12);
    param_label->SetBackgroundColour(*wxWHITE);
    param_label->SetMinSize(wxSize(FromDIP(PARAM_ITEM_WIDTH), -1));
    param_label->SetMaxSize(wxSize(FromDIP(PARAM_ITEM_WIDTH), -1));
    title_sizer->Add(param_label, 0, wxALIGN_CENTER_HORIZONTAL);

    m_scroll_sizer->Add(title_sizer, 0, wxBOTTOM, 10);
    m_main_sizer->Add(m_scroll_panel, 1, wxLEFT | wxRIGHT, 30);

    // btns
    wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
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
    m_main_sizer->Add(btnSizer, 0, wxALL | wxALIGN_RIGHT, 15);

    SetMinSize(wxSize(FromDIP(720), FromDIP(350)));
    SetBackgroundColour(*wxWHITE);
    Fit();
    Layout();
    CenterOnParent();
}

void SupportRecommendDialog::AddSupportComboCard(const wxString& mainMat,
                                                 const wxString& supportMat,
                                                 const wxArrayString& params)
{
    SupportComboCard* card = new SupportComboCard(m_scroll_panel, mainMat, supportMat, params);
    m_scroll_sizer->Add(card, 0, wxEXPAND | wxBOTTOM, 5);
    Layout();
}

void SupportRecommendDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    m_cancel_btn->Rescale();
    m_apply_btn->Rescale();
    Layout();
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
