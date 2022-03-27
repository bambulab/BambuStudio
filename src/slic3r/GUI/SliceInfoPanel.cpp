#include "SliceInfoPanel.hpp"

#include "I18N.hpp"
#include "Widgets/Label.hpp"

namespace Slic3r {
namespace GUI {

#define THUMBNAIL_SIZE  (wxSize(FromDIP(60), FromDIP(60)))
#define ICON_SIZE       (wxSize(FromDIP(16), FromDIP(16)))
#define PRINT_ICON_SIZE (wxSize(FromDIP(18), FromDIP(18)))

SliceInfoPanel::SliceInfoPanel(wxWindow *parent, wxBitmap &prediction, wxBitmap &cost, wxBitmap &print,
    wxWindowID id, const wxPoint &pos, const wxSize &size, long style, const wxString &name)
    : wxPanel(parent, id, pos, size, style, name)
{
    this->SetBackgroundColour(*wxWHITE);

    m_item_top_sizer = new wxBoxSizer(wxHORIZONTAL);
    
    m_bmp_item_thumbnail = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW | 0);
    m_bmp_item_thumbnail->SetMinSize(THUMBNAIL_SIZE);
    m_bmp_item_thumbnail->SetSize(THUMBNAIL_SIZE);

    m_item_top_sizer->Add(m_bmp_item_thumbnail, 0, wxALL, 0);

    wxBoxSizer *m_item_content_sizer;
    m_item_content_sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *m_item_info_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_bmp_item_prediction = new wxStaticBitmap(this, wxID_ANY, prediction);
    m_bmp_item_prediction->SetMinSize(ICON_SIZE);
    m_bmp_item_prediction->SetSize(ICON_SIZE);
    m_item_info_sizer->Add(m_bmp_item_prediction, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));

    m_text_item_prediction = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxSize(FromDIP(60), -1));
    m_text_item_prediction->Wrap(-1);
    m_item_info_sizer->Add(m_text_item_prediction, 1, wxALIGN_CENTER_VERTICAL | wxALL, 0);

    m_bmp_item_cost = new wxStaticBitmap(this, wxID_ANY, cost);
    m_bmp_item_cost->SetMinSize(ICON_SIZE);
    m_bmp_item_cost->SetSize(ICON_SIZE);
    m_item_info_sizer->Add(m_bmp_item_cost, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));

    m_text_item_cost = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxSize(FromDIP(35), -1));
    m_text_item_cost->Wrap(-1);
    m_item_info_sizer->Add(m_text_item_cost, 1, wxALIGN_CENTER_VERTICAL | wxALL, 0);

    m_item_content_sizer->Add(m_item_info_sizer, 0, wxEXPAND, 0);

    wxGridSizer *m_filament_info_sizer = new wxGridSizer(0, 3, 0, 8);

    m_item_content_sizer->Add(m_filament_info_sizer, 0, wxEXPAND, 0);

    m_item_top_sizer->Add(m_item_content_sizer, 0, wxEXPAND, 0);

    wxBoxSizer *m_item_right_sizer;
    m_item_right_sizer = new wxBoxSizer(wxVERTICAL);

    m_bmp_item_print = new wxStaticBitmap(this, wxID_ANY, print, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW | 0);
    m_bmp_item_print->SetMinSize(PRINT_ICON_SIZE);
    m_bmp_item_print->SetSize(PRINT_ICON_SIZE);
    m_item_right_sizer->Add(m_bmp_item_print, 0, wxALL, FromDIP(5));

    m_item_right_sizer->Add(0, 0, 1, wxEXPAND, FromDIP(5));

    m_text_plate_index = new wxStaticText(this, wxID_ANY, "");
    m_text_plate_index->Wrap(-1);
    m_item_right_sizer->Add(m_text_plate_index, 0, wxALIGN_RIGHT | wxALL, FromDIP(5));

    m_item_top_sizer->Add(m_item_right_sizer, 0, wxEXPAND, 0);

    this->SetSizer(m_item_top_sizer);
    this->Layout();

    Bind(wxEVT_WEBREQUEST_STATE, &SliceInfoPanel::on_webrequest_state, this);

    // Connect Events
    //m_bmp_item_thumbnail->Connect(wxEVT_ENTER_WINDOW, wxMouseEventHandler(SliceInfoPanel::on_thumbnail_enter), NULL, this);
    //m_bmp_item_thumbnail->Connect(wxEVT_LEAVE_WINDOW, wxMouseEventHandler(SliceInfoPanel::on_thumbnail_leave), NULL, this);
    m_bmp_item_print->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(SliceInfoPanel::on_subtask_print), NULL, this);
}

SliceInfoPanel::~SliceInfoPanel()
{
    // Disconnect Events
    //m_bmp_item_thumbnail->Disconnect(wxEVT_ENTER_WINDOW, wxMouseEventHandler(SliceInfoPanel::on_thumbnail_enter), NULL, this);
    //m_bmp_item_thumbnail->Disconnect(wxEVT_LEAVE_WINDOW, wxMouseEventHandler(SliceInfoPanel::on_thumbnail_leave), NULL, this);
    m_bmp_item_print->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(SliceInfoPanel::on_subtask_print), NULL, this);
}

void SliceInfoPanel::SetImages(wxBitmap &prediction, wxBitmap &cost, wxBitmap &printing)
{
    m_bmp_item_prediction->SetBitmap(prediction);
    m_bmp_item_cost->SetBitmap(cost);
    m_bmp_item_print->SetBitmap(printing);
}

void SliceInfoPanel::on_subtask_print(wxCommandEvent &evt) {
    ;
}

void SliceInfoPanel::on_thumbnail_enter(wxMouseEvent &event)
{
    if (!m_thumbnail_img.IsOk()) return;
    m_thumbnail_popup = std::make_shared<ImageTransientPopup>(this, false, m_thumbnail_img);
    wxWindow *ctrl    = (wxWindow *) event.GetEventObject();
    wxPoint   pos     = ctrl->ClientToScreen(wxPoint(0, 0));
    wxSize    sz      = ctrl->GetSize();
    m_thumbnail_popup->Position(pos, sz);
    m_thumbnail_popup->Popup();
}

void SliceInfoPanel::on_thumbnail_leave(wxMouseEvent &event)
{
    if (m_thumbnail_popup) { m_thumbnail_popup->Hide(); }
}

void SliceInfoPanel::on_mouse_enter(wxMouseEvent &event) { ; }

void SliceInfoPanel::on_mouse_leave(wxMouseEvent &event) { ; }

void SliceInfoPanel::on_webrequest_state(wxWebRequestEvent &evt)
{
    BOOST_LOG_TRIVIAL(trace) << "monitor: sub_task_panel web request state = " << evt.GetState();
    switch (evt.GetState()) {
    case wxWebRequest::State_Completed: {
        m_thumbnail_img    = *evt.GetResponse().GetStream();
        wxImage resize_img = m_thumbnail_img.Scale(m_bmp_item_thumbnail->GetSize().x, m_bmp_item_thumbnail->GetSize().y);
        m_bmp_item_thumbnail->SetBitmap(resize_img);
        break;
    }
    case wxWebRequest::State_Failed: {
        break;
    }
    case wxWebRequest::State_Cancelled:
    case wxWebRequest::State_Unauthorized:
    case wxWebRequest::State_Active:
    case wxWebRequest::State_Idle: break;
    default: break;
    }
}

void SliceInfoPanel::update(BBLSliceInfo *info)
{
    wxString prediction = wxString::Format("%s", get_bbl_time_dhms(info->prediction));
    m_text_item_prediction->SetLabelText(prediction);

    wxString weight = wxString::Format("%sg", info->weight);
    m_text_item_cost->SetLabelText(weight);

    m_text_plate_index->SetLabelText(info->index);

    if (web_request.IsOk()) web_request.Cancel();

    if (!info->thumbnail_url.empty()) {
        web_request = wxWebSession::GetDefault().CreateRequest(this, info->thumbnail_url);
        BOOST_LOG_TRIVIAL(trace) << "slice info: start reqeust thumbnail, url = " << info->thumbnail_url;
        web_request.Start();
    }

    this->Layout();
}

void SliceInfoPanel::msw_rescale()
{
    m_bmp_item_prediction->SetMinSize(ICON_SIZE);
    m_bmp_item_prediction->SetSize(ICON_SIZE);
    m_bmp_item_cost->SetMinSize(ICON_SIZE);
    m_bmp_item_cost->SetSize(ICON_SIZE);
    m_bmp_item_print->SetMinSize(PRINT_ICON_SIZE);
    m_bmp_item_print->SetSize(PRINT_ICON_SIZE);
    this->Layout();
}


}
}