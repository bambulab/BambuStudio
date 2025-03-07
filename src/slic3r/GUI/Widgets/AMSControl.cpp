#include "AMSControl.hpp"
#include "Label.hpp"
#include "../BitmapCache.hpp"
#include "../I18N.hpp"
#include "../GUI_App.hpp"

#include "slic3r/GUI/DeviceTab/uiAmsHumidityPopup.h"

#include <wx/simplebook.h>
#include <wx/dcgraph.h>

#include <boost/log/trivial.hpp>

#include "CalibUtils.hpp"

namespace Slic3r { namespace GUI {

static const wxColour AMS_TRAY_DEFAULT_COL = wxColour(255, 255, 255);

wxDEFINE_EVENT(EVT_AMS_EXTRUSION_CALI, wxCommandEvent);
wxDEFINE_EVENT(EVT_AMS_LOAD, SimpleEvent);
wxDEFINE_EVENT(EVT_AMS_UNLOAD, SimpleEvent);
wxDEFINE_EVENT(EVT_AMS_SETTINGS, SimpleEvent);
wxDEFINE_EVENT(EVT_AMS_FILAMENT_BACKUP, SimpleEvent);
wxDEFINE_EVENT(EVT_AMS_REFRESH_RFID, wxCommandEvent);
wxDEFINE_EVENT(EVT_AMS_ON_SELECTED, wxCommandEvent);
wxDEFINE_EVENT(EVT_AMS_ON_FILAMENT_EDIT, wxCommandEvent);
wxDEFINE_EVENT(EVT_VAMS_ON_FILAMENT_EDIT, wxCommandEvent);
wxDEFINE_EVENT(EVT_AMS_CLIBRATION_AGAIN, wxCommandEvent);
wxDEFINE_EVENT(EVT_AMS_CLIBRATION_CANCEL, wxCommandEvent);
wxDEFINE_EVENT(EVT_AMS_GUIDE_WIKI, wxCommandEvent);
wxDEFINE_EVENT(EVT_AMS_RETRY, wxCommandEvent);
wxDEFINE_EVENT(EVT_AMS_SHOW_HUMIDITY_TIPS, wxCommandEvent);
wxDEFINE_EVENT(EVT_AMS_UNSELETED_VAMS, wxCommandEvent);
wxDEFINE_EVENT(EVT_CLEAR_SPEED_CONTROL, wxCommandEvent);

bool AMSinfo::parse_ams_info(MachineObject *obj, Ams *ams, bool remain_flag, bool humidity_flag)
{
    if (!ams) return false;
    this->ams_id = ams->id;

    if (humidity_flag) {
        this->ams_humidity = ams->humidity;
    }
    else {
        this->ams_humidity = -1;
    }

    cans.clear();
    for (int i = 0; i < 4; i++) {
        auto    it = ams->trayList.find(std::to_string(i));
        Caninfo info;
        // tray is exists
        if (it != ams->trayList.end() && it->second->is_exists) {
            if (it->second->is_tray_info_ready()) {
                info.can_id        = it->second->id;
                info.ctype         = it->second->ctype;
                info.material_name = it->second->get_display_filament_type();
                info.cali_idx      = it->second->cali_idx;
                info.filament_id   = it->second->setting_id;
                if (!it->second->color.empty()) {
                    info.material_colour = AmsTray::decode_color(it->second->color);
                } else {
                    // set to white by default
                    info.material_colour = AMS_TRAY_DEFAULT_COL;
                }

                for (std::string cols:it->second->cols) {
                    info.material_cols.push_back(AmsTray::decode_color(cols));
                }

                if (MachineObject::is_bbl_filament(it->second->tag_uid)) {
                    info.material_state = AMSCanType::AMS_CAN_TYPE_BRAND;
                } else {
                    info.material_state = AMSCanType::AMS_CAN_TYPE_THIRDBRAND;
                }

                if (!MachineObject::is_bbl_filament(it->second->tag_uid) || !remain_flag) {
                    info.material_remain = 100;
                } else {
                    info.material_remain = it->second->remain < 0 ? 0 : it->second->remain;
                    info.material_remain = it->second->remain > 100 ? 100 : info.material_remain;
                }


            } else {
                info.can_id = it->second->id;
                info.material_name = "";
                info.cali_idx = -1;
                info.filament_id = "";
                info.ctype = 0;
                info.material_colour = AMS_TRAY_DEFAULT_COL;
                info.material_state = AMSCanType::AMS_CAN_TYPE_THIRDBRAND;
                wxColour(255, 255, 255);
            }

            if (it->second->is_tray_info_ready() && obj->cali_version >= 0) {
                CalibUtils::get_pa_k_n_value_by_cali_idx(obj, it->second->cali_idx, info.k, info.n);
            }
            else {
                info.k = it->second->k;
                info.n = it->second->n;
            }
        } else {
            info.can_id         = i;
            info.material_state = AMSCanType::AMS_CAN_TYPE_EMPTY;
        }
        cans.push_back(info);
    }
    return true;
}

/*************************************************
Description:AMSrefresh
**************************************************/

AMSrefresh::AMSrefresh() { SetFont(Label::Body_10);}

AMSrefresh::AMSrefresh(wxWindow *parent, wxString number, Caninfo info, const wxPoint &pos, const wxSize &size) : AMSrefresh()
{
    m_info = info;
    m_can_id = number.ToStdString();
    create(parent, wxID_ANY, pos, size);
}

AMSrefresh::AMSrefresh(wxWindow *parent, int number, Caninfo info, const wxPoint &pos, const wxSize &size) : AMSrefresh()
{
    m_info = info;
    m_can_id = wxString::Format("%d", number).ToStdString();
    create(parent, wxID_ANY, pos, size);
}

 AMSrefresh::~AMSrefresh()
 {
     if (m_playing_timer) {
         m_playing_timer->Stop();
         delete m_playing_timer;
         m_playing_timer = nullptr;
     }
 }

void AMSrefresh::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    wxWindow::Create(parent, id, pos, size, wxBORDER_NONE);
    SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);

    Bind(wxEVT_TIMER, &AMSrefresh::on_timer, this);
    Bind(wxEVT_PAINT, &AMSrefresh::paintEvent, this);
    Bind(wxEVT_ENTER_WINDOW, &AMSrefresh::OnEnterWindow, this);
    Bind(wxEVT_LEAVE_WINDOW, &AMSrefresh::OnLeaveWindow, this);
    Bind(wxEVT_LEFT_DOWN, &AMSrefresh::OnClick, this);

    m_bitmap_normal   = ScalableBitmap(this, "ams_refresh_normal", 30);
    m_bitmap_selected = ScalableBitmap(this, "ams_refresh_selected", 30);

    m_bitmap_ams_rfid_0 = ScalableBitmap(this, "ams_rfid_0", 30);
    m_bitmap_ams_rfid_1 = ScalableBitmap(this, "ams_rfid_1", 30);
    m_bitmap_ams_rfid_2 = ScalableBitmap(this, "ams_rfid_2", 30);
    m_bitmap_ams_rfid_3 = ScalableBitmap(this, "ams_rfid_3", 30);
    m_bitmap_ams_rfid_4 = ScalableBitmap(this, "ams_rfid_4", 30);
    m_bitmap_ams_rfid_5 = ScalableBitmap(this, "ams_rfid_5", 30);
    m_bitmap_ams_rfid_6 = ScalableBitmap(this, "ams_rfid_6", 30);
    m_bitmap_ams_rfid_7 = ScalableBitmap(this, "ams_rfid_7", 30);

    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_0);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_1);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_2);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_3);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_4);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_5);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_6);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_7);

    m_playing_timer = new wxTimer();
    m_playing_timer->SetOwner(this);
    wxPostEvent(this, wxTimerEvent());

    SetSize(AMS_REFRESH_SIZE);
    SetMinSize(AMS_REFRESH_SIZE);
    SetMaxSize(AMS_REFRESH_SIZE);
}

void AMSrefresh::on_timer(wxTimerEvent &event)
{
    //if (m_rotation_angle >= m_rfid_bitmap_list.size()) {
    //    m_rotation_angle = 0;
    //} else {
    //    m_rotation_angle++;
    //}
    Refresh();
}

void AMSrefresh::PlayLoading()
{
    if (m_play_loading | m_disable_mode)  return;
    m_play_loading = true;
    //m_rotation_angle = 0;
    m_playing_timer->Start(AMS_REFRESH_PLAY_LOADING_TIMER);
    Refresh();
}

void AMSrefresh::StopLoading()
{
    if (!m_play_loading | m_disable_mode) return;
    m_playing_timer->Stop();
    m_play_loading = false;
    Refresh();
}

void AMSrefresh::OnEnterWindow(wxMouseEvent &evt)
{
    m_selected = true;
    Refresh();
}

void AMSrefresh::OnLeaveWindow(wxMouseEvent &evt)
{
    m_selected = false;
    Refresh();
}

void AMSrefresh::OnClick(wxMouseEvent &evt) {
    post_event(wxCommandEvent(EVT_AMS_REFRESH_RFID));
}

void AMSrefresh::post_event(wxCommandEvent &&event)
{
    if (m_disable_mode)
        return;
    event.SetString(m_info.can_id);
    event.SetEventObject(m_parent);
    wxPostEvent(m_parent, event);
    event.Skip();
}

void AMSrefresh::paintEvent(wxPaintEvent &evt)
{
    wxSize    size = GetSize();
    wxPaintDC dc(this);

    auto colour = StateColor::darkModeColorFor(AMS_CONTROL_GRAY700);
    if (!wxWindow::IsEnabled()) { colour = AMS_CONTROL_GRAY500; }

    auto pot = wxPoint((size.x - m_bitmap_selected.GetBmpSize().x) / 2, (size.y - m_bitmap_selected.GetBmpSize().y) / 2);

    if (!m_disable_mode) {
        if (!m_play_loading) {
            dc.DrawBitmap(m_selected ? m_bitmap_selected.bmp() : m_bitmap_normal.bmp(), pot);
        }
        else {
            /* m_bitmap_rotation    = ScalableBitmap(this, "ams_refresh_normal", 30);
             auto           image = m_bitmap_rotation.bmp().ConvertToImage();
             wxPoint        offset;
             auto           loading_img = image.Rotate(m_rotation_angle, wxPoint(image.GetWidth() / 2, image.GetHeight() / 2), true, &offset);
             ScalableBitmap loading_bitmap;
             loading_bitmap.bmp() = wxBitmap(loading_img);
             dc.DrawBitmap(loading_bitmap.bmp(), offset.x , offset.y);*/
            m_rotation_angle++;
            if (m_rotation_angle >= m_rfid_bitmap_list.size()) {
                m_rotation_angle = 0;
            }
            if (m_rfid_bitmap_list.size() <= 0)return;
            dc.DrawBitmap(m_rfid_bitmap_list[m_rotation_angle].bmp(), pot);
        }
    }

    dc.SetPen(wxPen(colour));
    dc.SetBrush(wxBrush(colour));
    dc.SetFont(Label::Body_11);
    dc.SetTextForeground(colour);
    auto tsize = dc.GetTextExtent(m_refresh_id);
    pot        = wxPoint((size.x - tsize.x) / 2, (size.y - tsize.y) / 2);
    dc.DrawText(m_refresh_id, pot);
}

void AMSrefresh::Update(std::string ams_id, Caninfo info)
{
    m_ams_id = ams_id;
    m_info   = info;

    if (!m_ams_id.empty() && !m_can_id.empty()) {
        auto aid = atoi(m_ams_id.c_str());
        auto tid = atoi(m_can_id.c_str());
        auto tray_id = aid * 4 + tid;
        m_refresh_id = wxGetApp().transition_tridid(tray_id);
    }
    StopLoading();
}

void AMSrefresh::msw_rescale() {
    m_bitmap_normal     = ScalableBitmap(this, "ams_refresh_normal", 30);
    m_bitmap_selected   = ScalableBitmap(this, "ams_refresh_selected", 30);
    m_bitmap_ams_rfid_0 = ScalableBitmap(this, "ams_rfid_0", 30);
    m_bitmap_ams_rfid_1 = ScalableBitmap(this, "ams_rfid_1", 30);
    m_bitmap_ams_rfid_2 = ScalableBitmap(this, "ams_rfid_2", 30);
    m_bitmap_ams_rfid_3 = ScalableBitmap(this, "ams_rfid_3", 30);
    m_bitmap_ams_rfid_4 = ScalableBitmap(this, "ams_rfid_4", 30);
    m_bitmap_ams_rfid_5 = ScalableBitmap(this, "ams_rfid_5", 30);
    m_bitmap_ams_rfid_6 = ScalableBitmap(this, "ams_rfid_6", 30);
    m_bitmap_ams_rfid_7 = ScalableBitmap(this, "ams_rfid_7", 30);

    m_rfid_bitmap_list.clear();
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_0);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_1);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_2);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_3);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_4);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_5);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_6);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_7);
}

void AMSrefresh::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    wxWindow::DoSetSize(x, y, width, height, sizeFlags);
}

/*************************************************
Description:AMSextruder
**************************************************/
void AMSextruderImage::TurnOn(wxColour col)
{
    if (m_colour != col) {
        m_colour = col;
        Refresh();
    }
}

void AMSextruderImage::TurnOff()
{
    if (m_colour != AMS_EXTRUDER_DEF_COLOUR) {
        m_colour = AMS_EXTRUDER_DEF_COLOUR;
        Refresh();
    }
}

void AMSextruderImage::msw_rescale()
{
    //m_ams_extruder.SetSize(AMS_EXTRUDER_BITMAP_SIZE);
    //auto image     = m_ams_extruder.ConvertToImage();
    m_ams_extruder = ScalableBitmap(this, "monitor_ams_extruder", 55);
    Refresh();
}

void AMSextruderImage::paintEvent(wxPaintEvent &evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AMSextruderImage::render(wxDC &dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({0, 0}, size, &dc, {0, 0});

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void AMSextruderImage::doRender(wxDC &dc)
{
    auto size = GetSize();
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(m_colour);
    dc.DrawRectangle(0, FromDIP(18), size.x, size.y - FromDIP(18) - FromDIP(5));
    dc.DrawBitmap(m_ams_extruder.bmp(), wxPoint((size.x - m_ams_extruder.GetBmpSize().x) / 2, (size.y - m_ams_extruder.GetBmpSize().y) / 2));
}


AMSextruderImage::AMSextruderImage(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    wxWindow::Create(parent, id, pos, AMS_EXTRUDER_BITMAP_SIZE);
    SetBackgroundColour(*wxWHITE);

    m_ams_extruder = ScalableBitmap(this, "monitor_ams_extruder",55);
    SetSize(AMS_EXTRUDER_BITMAP_SIZE);
    SetMinSize(AMS_EXTRUDER_BITMAP_SIZE);
    SetMaxSize(AMS_EXTRUDER_BITMAP_SIZE);


    Bind(wxEVT_PAINT, &AMSextruderImage::paintEvent, this);
}

AMSextruderImage::~AMSextruderImage() {}



AMSextruder::AMSextruder(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size) { create(parent, id, pos, size); }

 AMSextruder::~AMSextruder() {}

void AMSextruder::TurnOn(wxColour col)
{
    m_amsSextruder->TurnOn(col);
}

void AMSextruder::TurnOff()
{
    m_amsSextruder->TurnOff();
}

void AMSextruder::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    wxWindow::Create(parent, id, pos, AMS_EXTRUDER_SIZE, wxBORDER_NONE);
    SetBackgroundColour(AMS_CONTROL_WHITE_COLOUR);

    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxVERTICAL);

    m_bitmap_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, AMS_EXTRUDER_BITMAP_SIZE, wxTAB_TRAVERSAL);
    m_bitmap_panel->SetBackgroundColour(AMS_EXTRUDER_DEF_COLOUR);
    m_bitmap_panel->SetDoubleBuffered(true);
    m_bitmap_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_amsSextruder = new AMSextruderImage(m_bitmap_panel, wxID_ANY, wxDefaultPosition, AMS_EXTRUDER_BITMAP_SIZE);
    m_bitmap_sizer->Add(m_amsSextruder, 0, wxALIGN_CENTER, 0);

    m_bitmap_panel->SetSizer(m_bitmap_sizer);
    m_bitmap_panel->Layout();
    m_sizer_body->Add( 0, 0, 1, wxEXPAND, 0 );
    m_sizer_body->Add(m_bitmap_panel, 0, wxALIGN_CENTER, 0);

    SetSizer(m_sizer_body);

    Bind(wxEVT_PAINT, &AMSextruder::paintEvent, this);
    Layout();
}

void AMSextruder::OnVamsLoading(bool load, wxColour col)
{
    if (m_vams_loading != load) {
        m_vams_loading = load;
        if (load) m_current_colur = col;
        //m_current_colur = col;
        Refresh();
    }
}

void AMSextruder::OnAmsLoading(bool load, wxColour col /*= AMS_CONTROL_GRAY500*/)
{
    if (m_ams_loading != load) {
        m_ams_loading = load;
        //m_current_colur = col;
        if (load) m_current_colur = col;
        Refresh();
    }
}

void AMSextruder::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AMSextruder::render(wxDC& dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({ 0, 0 }, size, &dc, { 0, 0 });

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif

}

void AMSextruder::doRender(wxDC& dc)
{
    //m_current_colur =
    wxSize size = GetSize();
    dc.SetPen(wxPen(AMS_CONTROL_GRAY500, 2, wxSOLID));
    dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));

    if (!m_none_ams_mode) {
        dc.DrawLine(size.x / 2, -1, size.x / 2, size.y * 0.6 - 1);
    }

    if (m_has_vams) {
        dc.DrawRoundedRectangle(-size.x / 2, size.y * 0.1, size.x, size.y, 4);

        if (m_vams_loading) {

            if (m_current_colur.Alpha() == 0) { dc.SetPen(wxPen(*wxWHITE, 6, wxSOLID)); }
            else { dc.SetPen(wxPen(m_current_colur, 6, wxSOLID)); }
            dc.DrawRoundedRectangle(-size.x / 2, size.y * 0.1, size.x, size.y, 4);

            if ((m_current_colur == *wxWHITE || m_current_colur.Alpha() == 0) && !wxGetApp().dark_mode()) {
                dc.SetPen(wxPen(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, 1, wxSOLID));
                dc.DrawRoundedRectangle(-size.x / 2 - FromDIP(3), size.y * 0.1 + FromDIP(3), size.x, size.y, 3);
                dc.DrawRoundedRectangle(-size.x / 2 + FromDIP(3), size.y * 0.1 - FromDIP(3), size.x, size.y, 5);
            }
        }

        if (m_ams_loading && !m_none_ams_mode) {
            if (m_current_colur.Alpha() == 0) {dc.SetPen(wxPen(*wxWHITE, 6, wxSOLID));}
            else {dc.SetPen(wxPen(m_current_colur, 6, wxSOLID));}
            dc.DrawLine(size.x / 2, -1, size.x / 2, size.y * 0.6 - 1);

            if ((m_current_colur == *wxWHITE || m_current_colur.Alpha() == 0) && !wxGetApp().dark_mode()) {
                dc.SetPen(wxPen(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, 1, wxSOLID));
                dc.DrawLine(size.x / 2 - FromDIP(4), -1, size.x / 2 - FromDIP(3), size.y * 0.6 - 1);
                dc.DrawLine(size.x / 2 + FromDIP(3), -1, size.x / 2 + FromDIP(3), size.y * 0.6 - 1);
            }
        }
    }
    else {
        if (m_ams_loading) {
            if (m_current_colur.Alpha() == 0) { dc.SetPen(wxPen(*wxWHITE, 6, wxSOLID)); }
            else { dc.SetPen(wxPen(m_current_colur, 6, wxSOLID)); }
            dc.DrawLine(size.x / 2, -1, size.x / 2, size.y * 0.6 - 1);

            if ((m_current_colur == *wxWHITE || m_current_colur.Alpha() == 0) && !wxGetApp().dark_mode()) {
                dc.SetPen(wxPen(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, 1, wxSOLID));
                dc.DrawLine(size.x / 2 - FromDIP(4), -1, size.x / 2 - FromDIP(3), size.y * 0.6 - 1);
                dc.DrawLine(size.x / 2 + FromDIP(3), -1, size.x / 2 + FromDIP(3), size.y * 0.6 - 1);
            }
        }
    }

}

void AMSextruder::msw_rescale()
{
    m_amsSextruder->msw_rescale();
    Layout();
    Update();
    Refresh();
}

/*************************************************
Description:AMSVirtualRoad
**************************************************/

AMSVirtualRoad::AMSVirtualRoad(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size) { create(parent, id, pos, size); }

AMSVirtualRoad::~AMSVirtualRoad() {}

void AMSVirtualRoad::OnVamsLoading(bool load, wxColour col)
{
    if (m_vams_loading != load) {
        m_vams_loading = load;
        if (load)m_current_color = col;
        //m_current_color = col;
        Refresh();
    }
}

void AMSVirtualRoad::create(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size)
{
    wxWindow::Create(parent, id, pos, wxDefaultSize, wxBORDER_NONE);
    SetBackgroundColour(AMS_CONTROL_WHITE_COLOUR);
    Layout();
    Bind(wxEVT_PAINT, &AMSVirtualRoad::paintEvent, this);
}

void AMSVirtualRoad::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AMSVirtualRoad::render(wxDC& dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({ 0, 0 }, size, &dc, { 0, 0 });

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void AMSVirtualRoad::doRender(wxDC& dc)
{
    if (!m_has_vams) return;

    wxSize size = GetSize();
    if (m_vams_loading) {
        if (m_current_color.Alpha() == 0) { dc.SetPen(wxPen(*wxWHITE, 6, wxSOLID)); }
        else { dc.SetPen(wxPen(m_current_color, 6, wxSOLID)); }
    }
    else {
        dc.SetPen(wxPen(AMS_CONTROL_GRAY500, 2, wxSOLID));
    }

    dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
    dc.DrawRoundedRectangle(size.x / 2, -size.y / 1.1 + FromDIP(1), size.x, size.y, 4);

    if ((m_current_color == *wxWHITE || m_current_color.Alpha() == 0) && !wxGetApp().dark_mode()) {
        dc.SetPen(wxPen(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, 1, wxSOLID));
        dc.DrawRoundedRectangle(size.x / 2 - FromDIP(3), -size.y / 1.1 + FromDIP(4), size.x, size.y, 5);
        dc.DrawRoundedRectangle(size.x / 2 + FromDIP(3), -size.y / 1.1 - FromDIP(2), size.x, size.y, 3);
    }
}


void AMSVirtualRoad::msw_rescale()
{
    Layout();
    Update();
    Refresh();
}


/*************************************************
Description:AMSLib
**************************************************/
AMSLib::AMSLib(wxWindow *parent, Caninfo info)
{
    m_border_color   = (wxColour(130, 130, 128));
    m_road_def_color = AMS_CONTROL_GRAY500;
    wxWindow::SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    create(parent);

    Bind(wxEVT_PAINT, &AMSLib::paintEvent, this);
    Bind(wxEVT_ENTER_WINDOW, &AMSLib::on_enter_window, this);
    Bind(wxEVT_LEAVE_WINDOW, &AMSLib::on_leave_window, this);
    Bind(wxEVT_LEFT_DOWN, &AMSLib::on_left_down, this);

    Update(info, false);
}

void AMSLib::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    wxWindow::Create(parent, id, pos, size);

    SetSize(AMS_CAN_LIB_SIZE);
    SetMinSize(AMS_CAN_LIB_SIZE);
    SetMaxSize(AMS_CAN_LIB_SIZE);

    auto m_sizer_body = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *m_sizer_edit = new wxBoxSizer(wxHORIZONTAL);

    m_bitmap_editable       = ScalableBitmap(this, "ams_editable", 14);
    m_bitmap_editable_light = ScalableBitmap(this, "ams_editable_light", 14);
    m_bitmap_readonly       = ScalableBitmap(this, "ams_readonly", 14);
    m_bitmap_readonly_light = ScalableBitmap(this, "ams_readonly_light", 14);
    m_bitmap_transparent    = ScalableBitmap(this, "transparent_ams_lib", 68);

    m_bitmap_extra_tray_left    = ScalableBitmap(this, "extra_ams_tray_left", 80);
    m_bitmap_extra_tray_right    = ScalableBitmap(this, "extra_ams_tray_right", 80);

    m_bitmap_extra_tray_left_hover = ScalableBitmap(this, "extra_ams_tray_left_hover", 80);
    m_bitmap_extra_tray_right_hover = ScalableBitmap(this, "extra_ams_tray_right_hover", 80);

    m_bitmap_extra_tray_left_selected = ScalableBitmap(this, "extra_ams_tray_left_selected", 80);
    m_bitmap_extra_tray_right_selected = ScalableBitmap(this, "extra_ams_tray_right_selected", 80);


    m_sizer_body->Add(0, 0, 1, wxEXPAND, 0);
    m_sizer_body->Add(m_sizer_edit, 0, wxALIGN_CENTER, 0);
    m_sizer_body->Add(0, 0, 0, wxBOTTOM, GetSize().y * 0.12);
    SetSizer(m_sizer_body);
    Layout();
}

void AMSLib::on_enter_window(wxMouseEvent &evt)
{
    m_hover = true;
    Refresh();
}

void AMSLib::on_leave_window(wxMouseEvent &evt)
{
    m_hover = false;
    Refresh();
}

void AMSLib::on_left_down(wxMouseEvent &evt)
{
    if (m_info.material_state != AMSCanType::AMS_CAN_TYPE_EMPTY && m_info.material_state != AMSCanType::AMS_CAN_TYPE_NONE) {
        auto size = GetSize();
        auto pos  = evt.GetPosition();
        if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_THIRDBRAND || m_info.material_state == AMSCanType::AMS_CAN_TYPE_BRAND ||
            m_info.material_state == AMSCanType::AMS_CAN_TYPE_VIRTUAL) {

            auto left = FromDIP(10);
            auto right = size.x - FromDIP(10);
            auto top = 0;
            auto bottom = 0;

            if (m_ams_model == AMSModel::GENERIC_AMS) {
                top = (size.y - FromDIP(15) - m_bitmap_editable_light.GetBmpSize().y);
                bottom = size.y - FromDIP(15);
            }
            else if (m_ams_model == AMSModel::EXTRA_AMS) {
                top = (size.y - FromDIP(20) - m_bitmap_editable_light.GetBmpSize().y);
                bottom = size.y - FromDIP(20);
            }

            if (pos.x >= left && pos.x <= right && pos.y >= top && top <= bottom) {
                if (m_selected) {
                    if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_VIRTUAL) {
                        post_event(wxCommandEvent(EVT_VAMS_ON_FILAMENT_EDIT));
                    }
                    else {
                        post_event(wxCommandEvent(EVT_AMS_ON_FILAMENT_EDIT));
                    }
                } else {
                    BOOST_LOG_TRIVIAL(trace) << "current amslib is not selected";
                }
            }
        }
    }
}


void AMSLib::paintEvent(wxPaintEvent &evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AMSLib::render(wxDC &dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({0, 0}, size, &dc, {0, 0});

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif

    // text
    if (m_ams_model == AMSModel::GENERIC_AMS) {
        render_generic_text(dc);
    }
    else if (m_ams_model == AMSModel::EXTRA_AMS) {
        render_extra_text(dc);
    }
}

void AMSLib::render_extra_text(wxDC& dc)
{
    auto tmp_lib_colour = m_info.material_colour;

    change_the_opacity(tmp_lib_colour);
    auto temp_text_colour = AMS_CONTROL_GRAY800;

    if (tmp_lib_colour.GetLuminance() < 0.6) {
        temp_text_colour = AMS_CONTROL_WHITE_COLOUR;
    }
    else {
        temp_text_colour = AMS_CONTROL_GRAY800;
    }

    if (m_info.material_remain < 50) {
        temp_text_colour = AMS_CONTROL_GRAY800;
    }

    if (tmp_lib_colour.Alpha() == 0) {
        temp_text_colour = AMS_CONTROL_GRAY800;
    }

    dc.SetFont(::Label::Body_13);
    dc.SetTextForeground(temp_text_colour);

    auto libsize = GetSize();
    if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_THIRDBRAND
        || m_info.material_state == AMSCanType::AMS_CAN_TYPE_BRAND
        || m_info.material_state == AMSCanType::AMS_CAN_TYPE_VIRTUAL) {

        if (m_info.material_name.empty()) {
            auto tsize = dc.GetMultiLineTextExtent("?");
            auto pot = wxPoint(0, 0);
            pot = wxPoint((libsize.x - tsize.x) / 2 + FromDIP(2), (libsize.y - tsize.y) / 2 - FromDIP(5));
            dc.DrawText(L("?"), pot);
        }
        else {
            auto tsize = dc.GetMultiLineTextExtent(m_info.material_name);
            std::vector<std::string> split_char_arr = { " ", "-" };
            bool has_split = false;
            std::string has_split_char = " ";

            for (std::string split_char : split_char_arr) {
                if (m_info.material_name.find(split_char) != std::string::npos) {
                    has_split = true;
                    has_split_char = split_char;
                }
            }


            if (has_split) {
                dc.SetFont(::Label::Body_10);
                auto line_top = m_info.material_name.substr(0, m_info.material_name.find(has_split_char));
                auto line_bottom = m_info.material_name.substr(m_info.material_name.find(has_split_char));

                auto line_top_tsize = dc.GetMultiLineTextExtent(line_top);
                auto line_bottom_tsize = dc.GetMultiLineTextExtent(line_bottom);

                auto pot_top = wxPoint((libsize.x - line_top_tsize.x) / 2 + FromDIP(3), (libsize.y - line_top_tsize.y) / 2 - line_top_tsize.y);
                dc.DrawText(line_top, pot_top);

                auto pot_bottom = wxPoint((libsize.x - line_bottom_tsize.x) / 2 + FromDIP(3), (libsize.y - line_bottom_tsize.y) / 2);
                dc.DrawText(line_bottom, pot_bottom);


            }
            else {
                dc.SetFont(::Label::Body_10);
                auto pot = wxPoint(0, 0);
                if (m_obj ) {
                    pot = wxPoint((libsize.x - tsize.x) / 2 + FromDIP(6), (libsize.y - tsize.y) / 2 - FromDIP(5));
                }
                dc.DrawText(m_info.material_name, pot);
            }
        }
    }

    if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_EMPTY) {
        auto tsize = dc.GetMultiLineTextExtent(_L("/"));
        auto pot = wxPoint((libsize.x - tsize.x) / 2 + FromDIP(2), (libsize.y - tsize.y) / 2 + FromDIP(3));
        dc.DrawText(_L("/"), pot);
    }
}

void AMSLib::render_generic_text(wxDC &dc)
{
    bool show_k_value = true;
    if (m_info.material_name.empty()) {
        show_k_value = false;
    }
    else if (m_info.cali_idx == -1 || (m_obj && (CalibUtils::get_selected_calib_idx(m_obj->pa_calib_tab, m_info.cali_idx) == -1))) {
        get_default_k_n_value(m_info.filament_id, m_info.k, m_info.n);
    }

    auto tmp_lib_colour = m_info.material_colour;
    change_the_opacity(tmp_lib_colour);

    auto temp_text_colour = AMS_CONTROL_GRAY800;

    if (tmp_lib_colour.GetLuminance() < 0.6) {
        temp_text_colour = AMS_CONTROL_WHITE_COLOUR;
    }
    else {
        temp_text_colour = AMS_CONTROL_GRAY800;
    }

    if (m_info.material_remain < 50) {
        temp_text_colour = AMS_CONTROL_GRAY800;
    }

    if (tmp_lib_colour.Alpha() == 0) {
        temp_text_colour = AMS_CONTROL_GRAY800;
    }

    dc.SetFont(::Label::Body_13);
    dc.SetTextForeground(temp_text_colour);
    auto alpha = m_info.material_colour.Alpha();
    if (alpha != 0 && alpha != 255 && alpha != 254) {
        dc.SetTextForeground(*wxBLACK);
    }

    auto libsize = GetSize();
    if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_THIRDBRAND
        || m_info.material_state == AMSCanType::AMS_CAN_TYPE_BRAND
        || m_info.material_state == AMSCanType::AMS_CAN_TYPE_VIRTUAL) {

        if (m_info.material_name.empty() /*&&  m_info.material_state != AMSCanType::AMS_CAN_TYPE_VIRTUAL*/) {
            auto tsize = dc.GetMultiLineTextExtent("?");
            auto pot = wxPoint(0, 0);

            if (m_obj && show_k_value) {
                pot = wxPoint((libsize.x - tsize.x) / 2, (libsize.y - tsize.y) / 2 - FromDIP(9));
            }
            else {
                pot = wxPoint((libsize.x - tsize.x) / 2, (libsize.y - tsize.y) / 2 + FromDIP(3));
            }
            dc.DrawText(L("?"), pot);

        }
        else {
            auto tsize = dc.GetMultiLineTextExtent(m_info.material_name);
            std::vector<std::string> split_char_arr = { " ", "-" };
            bool has_split = false;
            std::string has_split_char = " ";

            for (std::string split_char : split_char_arr) {
                if (m_info.material_name.find(split_char) != std::string::npos) {
                    has_split = true;
                    has_split_char = split_char;
                }
            }


            if (has_split) {
                dc.SetFont(::Label::Body_12);

                auto line_top = m_info.material_name.substr(0, m_info.material_name.find(has_split_char));
                auto line_bottom = m_info.material_name.substr(m_info.material_name.find(has_split_char));

                auto line_top_tsize = dc.GetMultiLineTextExtent(line_top);
                auto line_bottom_tsize = dc.GetMultiLineTextExtent(line_bottom);

                if (!m_show_kn) {
                    auto pot_top = wxPoint((libsize.x - line_top_tsize.x) / 2, (libsize.y - line_top_tsize.y) / 2 - line_top_tsize.y + FromDIP(6));
                    dc.DrawText(line_top, pot_top);


                    auto pot_bottom = wxPoint((libsize.x - line_bottom_tsize.x) / 2, (libsize.y - line_bottom_tsize.y) / 2 + FromDIP(4));
                    dc.DrawText(line_bottom, pot_bottom);
                }
                else {
                    auto pot_top = wxPoint((libsize.x - line_top_tsize.x) / 2, (libsize.y - line_top_tsize.y) / 2 - line_top_tsize.y - FromDIP(6));
                    dc.DrawText(line_top, pot_top);

                    auto pot_bottom = wxPoint((libsize.x - line_bottom_tsize.x) / 2, (libsize.y - line_bottom_tsize.y) / 2 - FromDIP(8));
                    dc.DrawText(line_bottom, pot_bottom);
                }


            }
            else {
                auto pot = wxPoint(0, 0);
                if (m_obj && show_k_value) {
                    pot = wxPoint((libsize.x - tsize.x) / 2, (libsize.y - tsize.y) / 2 - FromDIP(9));
                } else {
                    pot = wxPoint((libsize.x - tsize.x) / 2, (libsize.y - tsize.y) / 2 + FromDIP(3));
                }
                dc.DrawText(m_info.material_name, pot);
            }
        }

        //draw k&n
        if (m_obj && show_k_value) {
            if (m_show_kn) {
                wxString str_k = wxString::Format("K %1.3f", m_info.k);
                wxString str_n = wxString::Format("N %1.3f", m_info.n);
                dc.SetFont(::Label::Body_11);
                auto tsize = dc.GetMultiLineTextExtent(str_k);
                auto pot_k = wxPoint((libsize.x - tsize.x) / 2, (libsize.y - tsize.y) / 2 - FromDIP(9) + tsize.y);
                dc.DrawText(str_k, pot_k);
            }
        }
    }

    if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_EMPTY) {
        auto tsize = dc.GetMultiLineTextExtent(_L("Empty"));
        auto pot = wxPoint((libsize.x - tsize.x) / 2, (libsize.y - tsize.y) / 2 + FromDIP(3));
        dc.DrawText(_L("Empty"), pot);
    }
}

void AMSLib::doRender(wxDC &dc)
{
    if (m_ams_model == AMSModel::GENERIC_AMS) {
        render_generic_lib(dc);
    }
    else if (m_ams_model == AMSModel::EXTRA_AMS) {
        render_extra_lib(dc);
    }
}

void AMSLib::render_extra_lib(wxDC& dc)
{
    wxSize size = GetSize();

    ScalableBitmap tray_bitmap = m_can_index <= 1 ? m_bitmap_extra_tray_left : m_bitmap_extra_tray_right;
    ScalableBitmap tray_bitmap_hover = m_can_index <= 1 ? m_bitmap_extra_tray_left_hover : m_bitmap_extra_tray_right_hover;
    ScalableBitmap tray_bitmap_selected = m_can_index <= 1 ? m_bitmap_extra_tray_left_selected : m_bitmap_extra_tray_right_selected;


    auto   tmp_lib_colour    = m_info.material_colour;
    change_the_opacity(tmp_lib_colour);

    auto   temp_bitmap_third = m_bitmap_editable_light;
    auto   temp_bitmap_brand = m_bitmap_readonly_light;

    //draw road


    dc.SetPen(wxPen(AMS_CONTROL_GRAY500, 2, wxSOLID));
    dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));

    if (m_pass_road) {
        dc.SetPen(wxPen(m_info.material_colour, 6, wxSOLID));
    }

    if (m_can_index == 0 || m_can_index == 3) {
        dc.DrawLine(size.x / 2, size.y / 2, size.x / 2, size.y);
    }
    else {
        dc.DrawLine(size.x / 2, size.y / 2, size.x / 2, 0);
    }


    //draw def background
    dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
    dc.SetBrush(wxBrush(AMS_CONTROL_DEF_LIB_BK_COLOUR));
    dc.DrawRoundedRectangle(FromDIP(10), FromDIP(10), size.x - FromDIP(20), size.y - FromDIP(20), 0);

    if (tmp_lib_colour.GetLuminance() < 0.6) {
        temp_bitmap_third = m_bitmap_editable_light;
        temp_bitmap_brand = m_bitmap_readonly_light;
    }
    else {
        temp_bitmap_third = m_bitmap_editable;
        temp_bitmap_brand = m_bitmap_readonly;
    }

    if (m_info.material_remain < 50) {
        temp_bitmap_third = m_bitmap_editable;
        temp_bitmap_brand = m_bitmap_readonly;
    }

    if (tmp_lib_colour.Alpha() == 0) {
        temp_bitmap_third = m_bitmap_editable;
        temp_bitmap_brand = m_bitmap_readonly;
    }

    dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
    if (m_info.material_cols.size() > 1) {
        int left = FromDIP(10);
        int gwidth = std::round(size.x / (m_info.material_cols.size() - 1));
        //gradient
        if (m_info.ctype == 0) {
            for (int i = 0; i < m_info.material_cols.size() - 1; i++) {
                auto rect = wxRect(left, FromDIP(10), size.x - FromDIP(20), size.y - FromDIP(20));
                dc.GradientFillLinear(rect, m_info.material_cols[i], m_info.material_cols[i + 1], wxEAST);
                left += gwidth;
            }
        }
        else {
            int cols_size = m_info.material_cols.size();
            for (int i = 0; i < cols_size; i++) {
                dc.SetBrush(wxBrush(m_info.material_cols[i]));
                float x = FromDIP(10) + ((float)size.x - FromDIP(20)) * i / cols_size;
                dc.DrawRoundedRectangle(x, FromDIP(10), ((float)size.x - FromDIP(20)) / cols_size, size.y - FromDIP(20), 0);
            }
            dc.SetBrush(wxBrush(tmp_lib_colour));
        }
    }
    else  {
        dc.SetBrush(wxBrush(tmp_lib_colour));
        dc.DrawRoundedRectangle(FromDIP(10), FromDIP(10), size.x - FromDIP(20), size.y - FromDIP(20), 0);
    }
    dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
    dc.SetBrush(wxBrush(tmp_lib_colour));
    if (!m_disable_mode) {
        // edit icon
        if (m_info.material_state != AMSCanType::AMS_CAN_TYPE_EMPTY && m_info.material_state != AMSCanType::AMS_CAN_TYPE_NONE)
        {
            if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_THIRDBRAND || m_info.material_state == AMSCanType::AMS_CAN_TYPE_VIRTUAL)
                dc.DrawBitmap(temp_bitmap_third.bmp(), (size.x - temp_bitmap_third.GetBmpSize().x) / 2 + FromDIP(2), (size.y - FromDIP(18) - temp_bitmap_third.GetBmpSize().y));
            if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_BRAND)
                dc.DrawBitmap(temp_bitmap_brand.bmp(), (size.x - temp_bitmap_brand.GetBmpSize().x) / 2 + FromDIP(2), (size.y - FromDIP(18) - temp_bitmap_brand.GetBmpSize().y));
        }
    }

    // selected & hover
    if (m_selected) {
        dc.DrawBitmap(tray_bitmap_selected.bmp(), (size.x - tray_bitmap_selected.GetBmpSize().x) / 2, (size.y - tray_bitmap_selected.GetBmpSize().y) / 2);
    }
    else if (!m_selected && m_hover) {
        dc.DrawBitmap(tray_bitmap_hover.bmp(), (size.x - tray_bitmap_hover.GetBmpSize().x) / 2, (size.y - tray_bitmap_hover.GetBmpSize().y) / 2);
    }
    else {
        dc.DrawBitmap(tray_bitmap.bmp(), (size.x - tray_bitmap.GetBmpSize().x) / 2, (size.y - tray_bitmap.GetBmpSize().y) / 2);
    }
}


void AMSLib::render_generic_lib(wxDC &dc)
{
    wxSize size = GetSize();
    auto   tmp_lib_colour = m_info.material_colour;
    change_the_opacity(tmp_lib_colour);

    auto   temp_bitmap_third = m_bitmap_editable_light;
    auto   temp_bitmap_brand = m_bitmap_readonly_light;

    //draw def background
    dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
    dc.SetBrush(wxBrush(AMS_CONTROL_DEF_LIB_BK_COLOUR));
    dc.DrawRoundedRectangle(FromDIP(4), FromDIP(4), size.x - FromDIP(8), size.y - FromDIP(8), m_radius);

    if (tmp_lib_colour.GetLuminance() < 0.6) {
        temp_bitmap_third = m_bitmap_editable_light;
        temp_bitmap_brand = m_bitmap_readonly_light;
    }
    else {
        temp_bitmap_third = m_bitmap_editable;
        temp_bitmap_brand = m_bitmap_readonly;
    }

    if (m_info.material_remain < 50) {
        temp_bitmap_third = m_bitmap_editable;
        temp_bitmap_brand = m_bitmap_readonly;
    }

    if (tmp_lib_colour.Alpha() == 0) {
        temp_bitmap_third = m_bitmap_editable;
        temp_bitmap_brand = m_bitmap_readonly;
    }

    // selected
    if (m_selected) {
        dc.SetPen(wxPen(tmp_lib_colour, 2, wxSOLID));
        if (tmp_lib_colour.Alpha() == 0) {
            dc.SetPen(wxPen(wxColour(tmp_lib_colour.Red(), tmp_lib_colour.Green(),tmp_lib_colour.Blue(),128), 2, wxSOLID));
        }
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        if (m_radius == 0) {
            dc.DrawRectangle(0, 0, size.x, size.y);
        }
        else {
            dc.DrawRoundedRectangle(FromDIP(1), FromDIP(1), size.x - FromDIP(1), size.y - FromDIP(1), m_radius);
        }

        dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
        dc.SetBrush(wxBrush(tmp_lib_colour));
    }

    if (!m_selected && m_hover) {
        dc.SetPen(wxPen(AMS_CONTROL_BRAND_COLOUR, 2, wxSOLID));
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        if (m_radius == 0) {
            dc.DrawRectangle(0, 0, size.x, size.y);
        }
        else {
            dc.DrawRoundedRectangle(FromDIP(1), FromDIP(1), size.x - FromDIP(1), size.y - FromDIP(1), m_radius);
        }

        dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
        dc.SetBrush(wxBrush(tmp_lib_colour));
    }
    else {
        dc.SetPen(wxPen(tmp_lib_colour, 1, wxSOLID));
        dc.SetBrush(wxBrush(tmp_lib_colour));
    }

    //draw remain
    auto alpha = m_info.material_colour.Alpha();
    int height = size.y - FromDIP(8);
    int curr_height = height * float(m_info.material_remain * 1.0 / 100.0);
    dc.SetFont(::Label::Body_13);

    int top = height - curr_height;

    if (curr_height >= FromDIP(6)) {

        //transparent

        if (alpha == 0) {
            dc.DrawBitmap(m_bitmap_transparent.bmp(), FromDIP(4), FromDIP(4));
        }
        else if (alpha != 255 && alpha != 254) {
            if (transparent_changed) {
                std::string rgb = (tmp_lib_colour.GetAsString(wxC2S_HTML_SYNTAX)).ToStdString();
                if (rgb.size() == 9) {
                    //delete alpha value
                    rgb = rgb.substr(0, rgb.size() - 2);
                }
                float alpha_f = 0.7 * tmp_lib_colour.Alpha() / 255.0;
                std::vector<std::string> replace;
                replace.push_back(rgb);
                std::string fill_replace = "fill-opacity=\"" + std::to_string(alpha_f);
                replace.push_back(fill_replace);
                m_bitmap_transparent = ScalableBitmap(this, "transparent_ams_lib", 68, false, false, true, replace);
                transparent_changed = false;

            }
            dc.DrawBitmap(m_bitmap_transparent.bmp(), FromDIP(4), FromDIP(4));
        }
        //gradient
        if (m_info.material_cols.size() > 1) {
            int left = FromDIP(4);
            float total_width = size.x - FromDIP(8);
            int gwidth = std::round(total_width / (m_info.material_cols.size() - 1));
            //gradient
            if (m_info.ctype == 0) {
                for (int i = 0; i < m_info.material_cols.size() - 1; i++) {

                    if ((left + gwidth) > (size.x - FromDIP(8))) {
                        gwidth = (size.x - FromDIP(4)) - left;
                    }

                    auto rect = wxRect(left, height - curr_height + FromDIP(4), gwidth, curr_height);
                    dc.GradientFillLinear(rect, m_info.material_cols[i], m_info.material_cols[i + 1], wxEAST);
                    left += gwidth;
                }
            }
            else {
                //multicolour
                gwidth = std::round(total_width / m_info.material_cols.size());
                for (int i = 0; i < m_info.material_cols.size(); i++) {
                    dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
                    dc.SetBrush(wxBrush(m_info.material_cols[i]));
                    if (i == 0 || i == m_info.material_cols.size() - 1) {
#ifdef __APPLE__
                        dc.DrawRoundedRectangle(left + gwidth * i, height - curr_height + FromDIP(4), gwidth, curr_height, m_radius);
#else
                        dc.DrawRoundedRectangle(left + gwidth * i, height - curr_height + FromDIP(4), gwidth, curr_height, m_radius - 1);
#endif
                        //add rectangle
                        int dr_gwidth = std::round(gwidth * 0.6);
                        if (i == 0) {
                            dc.DrawRectangle(left + gwidth - dr_gwidth, height - curr_height + FromDIP(4), dr_gwidth, curr_height);
                        }
                        else {
                            dc.DrawRectangle(left + gwidth*i, height - curr_height + FromDIP(4), dr_gwidth, curr_height);
                        }
                    }
                    else {
                        dc.DrawRectangle(left + gwidth * i, height - curr_height + FromDIP(4), gwidth, curr_height);
                    }
                }
                //reset pen and brush
                if (m_selected || m_hover) {
                    dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
                    dc.SetBrush(wxBrush(tmp_lib_colour));
                }
                else {
                    dc.SetPen(wxPen(tmp_lib_colour, 1, wxSOLID));
                    dc.SetBrush(wxBrush(tmp_lib_colour));
                }
            }
        }
        else {
            auto brush = dc.GetBrush();
            if (alpha != 0 && alpha != 255 && alpha != 254) dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
#ifdef __APPLE__
            dc.DrawRoundedRectangle(FromDIP(4), FromDIP(4) + top, size.x - FromDIP(8), curr_height, m_radius);
#else
            dc.DrawRoundedRectangle(FromDIP(4), FromDIP(4) + top, size.x - FromDIP(8), curr_height, m_radius - 1);
#endif
            dc.SetBrush(brush);
        }
    }

    if (top > 2) {
        if (curr_height >= FromDIP(6)) {
            dc.DrawRectangle(FromDIP(4), FromDIP(4) + top, size.x - FromDIP(8), FromDIP(2));
            if (alpha != 255 && alpha != 254) {
                dc.SetPen(wxPen(*wxWHITE));
                dc.SetBrush(wxBrush(*wxWHITE));
#ifdef __APPLE__
                dc.DrawRoundedRectangle(FromDIP(4), FromDIP(4) , size.x - FromDIP(8), top, m_radius);
#else
                dc.DrawRoundedRectangle(FromDIP(4), FromDIP(4) , size.x - FromDIP(8), top, m_radius - 1);
#endif
            }
            if (tmp_lib_colour.Red() > 238 && tmp_lib_colour.Green() > 238 && tmp_lib_colour.Blue() > 238) {
                dc.SetPen(wxPen(wxColour(130, 129, 128), 1, wxSOLID));
                dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
                dc.DrawLine(FromDIP(4), FromDIP(4) + top, size.x - FromDIP(4), FromDIP(4) + top);
            }
        }
        else {
            dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
            if (tmp_lib_colour.Red() > 238 && tmp_lib_colour.Green() > 238 && tmp_lib_colour.Blue() > 238) {
                dc.SetPen(wxPen(wxColour(130, 129, 128), 2, wxSOLID));
            }
            else {
                dc.SetPen(wxPen(tmp_lib_colour, 2, wxSOLID));
            }

#ifdef __APPLE__
            dc.DrawLine(FromDIP(5), FromDIP(4) + height - FromDIP(2), size.x - FromDIP(5), FromDIP(4) + height - FromDIP(2));
            dc.DrawLine(FromDIP(6), FromDIP(4) + height - FromDIP(1), size.x - FromDIP(6), FromDIP(4) + height - FromDIP(1));
#else
            dc.DrawLine(FromDIP(4), FromDIP(4) + height - FromDIP(2), size.x - FromDIP(4), FromDIP(4) + height - FromDIP(2));
            dc.DrawLine(FromDIP(5), FromDIP(4) + height - FromDIP(1), size.x - FromDIP(5), FromDIP(4) + height - FromDIP(1));
#endif
        }
    }

    //border
    dc.SetPen(wxPen(wxColour(130, 130, 128), 1, wxSOLID));
    dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
#ifdef __APPLE__
    dc.DrawRoundedRectangle(FromDIP(4), FromDIP(4), size.x - FromDIP(7), size.y - FromDIP(7), m_radius);
#else
    dc.DrawRoundedRectangle(FromDIP(3), FromDIP(3), size.x - FromDIP(6), size.y - FromDIP(6), m_radius);
#endif

    if (!m_disable_mode) {
        // edit icon
        if (m_info.material_state != AMSCanType::AMS_CAN_TYPE_EMPTY && m_info.material_state != AMSCanType::AMS_CAN_TYPE_NONE)
        {
            if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_THIRDBRAND || m_info.material_state == AMSCanType::AMS_CAN_TYPE_VIRTUAL)
                dc.DrawBitmap(temp_bitmap_third.bmp(), (size.x - temp_bitmap_third.GetBmpSize().x) / 2, (size.y - FromDIP(10) - temp_bitmap_third.GetBmpSize().y));
            if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_BRAND)
                dc.DrawBitmap(temp_bitmap_brand.bmp(), (size.x - temp_bitmap_brand.GetBmpSize().x) / 2, (size.y - FromDIP(10) - temp_bitmap_brand.GetBmpSize().y));
        }
    }
}

void AMSLib::on_pass_road(bool pass)
{
    if (m_pass_road != pass) {
        m_pass_road = pass;
        Refresh();
    }
}

void AMSLib::Update(Caninfo info, bool refresh)
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;
    if (dev->get_selected_machine() && dev->get_selected_machine() != m_obj) {
        m_obj = dev->get_selected_machine();
    }
    if (info.material_colour.Alpha() != 0 && info.material_colour.Alpha() != 255 && info.material_colour.Alpha() != 254 && m_info.material_colour != info.material_colour) {
        transparent_changed = true;
    }

    if (m_info == info) {
        //todo
    } else {
        m_info = info;
        Layout();
        if (refresh) Refresh();
    }
}

wxColour AMSLib::GetLibColour() { return m_info.material_colour; }

void AMSLib::OnSelected()
{
    if (!wxWindow::IsEnabled()) return;
    if (m_unable_selected) return;

    post_event(wxCommandEvent(EVT_AMS_ON_SELECTED));
    m_selected = true;
    Refresh();
}

void AMSLib::post_event(wxCommandEvent &&event)
{
    event.SetString(m_info.can_id);
    event.SetEventObject(m_parent);
    wxPostEvent(m_parent, event);
    event.Skip();
}

void AMSLib::UnSelected()
{
    m_selected = false;
    Refresh();
}

bool AMSLib::Enable(bool enable) { return wxWindow::Enable(enable); }

void AMSLib::msw_rescale()
{
    m_bitmap_transparent.msw_rescale();
}

/*************************************************
Description:AMSRoad
**************************************************/
AMSRoad::AMSRoad() : m_road_def_color(AMS_CONTROL_GRAY500), m_road_color(AMS_CONTROL_GRAY500) {}
AMSRoad::AMSRoad(wxWindow *parent, wxWindowID id, Caninfo info, int canindex, int maxcan, const wxPoint &pos, const wxSize &size)
    : AMSRoad()
{
    m_info     = info;
    m_canindex = canindex;
    // road type
    auto mode = AMSRoadMode::AMS_ROAD_MODE_END;
    if (m_canindex == 0 && maxcan == 1) {
        m_rode_mode = AMSRoadMode::AMS_ROAD_MODE_NONE;
    } else if (m_canindex == 0 && maxcan > 1) {
        m_rode_mode = AMSRoadMode::AMS_ROAD_MODE_END;
    } else if (m_canindex < (maxcan - 1)) {
        m_rode_mode = AMSRoadMode::AMS_ROAD_MODE_LEFT_RIGHT;
    } else if (m_canindex == (maxcan - 1)) {
        m_rode_mode = AMSRoadMode::AMS_ROAD_MODE_LEFT;
    } else if (m_canindex == -1 && maxcan == -1) {
        m_rode_mode = AMSRoadMode::AMS_ROAD_MODE_VIRTUAL_TRAY;
    }
    else {
        m_rode_mode = AMSRoadMode::AMS_ROAD_MODE_NONE_ANY_ROAD;
    }

    for (int i = 1; i <= 5; i++) {
        ams_humidity_img.push_back(ScalableBitmap(this, "hum_level" + std::to_string(i) + "_light", 32));
    }

    for (int i = 1; i <= 5; i++) {
        ams_humidity_img.push_back(ScalableBitmap(this, "hum_level" + std::to_string(i) + "_dark", 32));
    }
    if (m_rode_mode != AMSRoadMode::AMS_ROAD_MODE_VIRTUAL_TRAY) {
        create(parent, id, pos, size);
    }
    else {
        wxSize virtual_size(size.x - 1, size.y + 2);
        create(parent, id, pos, virtual_size);

    }

    Bind(wxEVT_PAINT, &AMSRoad::paintEvent, this);
    wxWindow::SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);

    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) {
        if (m_canindex == 3 && m_show_humidity) {
            auto mouse_pos = ClientToScreen(e.GetPosition());
            auto rect = ClientToScreen(wxPoint(0, 0));

            if (mouse_pos.x > rect.x + GetSize().x - FromDIP(40) &&
                mouse_pos.y > rect.y + GetSize().y - FromDIP(40)) {
                wxCommandEvent show_event(EVT_AMS_SHOW_HUMIDITY_TIPS);
                wxPostEvent(GetParent()->GetParent(), show_event);

#ifdef __WXMSW__
                wxCommandEvent close_event(EVT_CLEAR_SPEED_CONTROL);
                wxPostEvent(GetParent()->GetParent(), close_event);
#endif // __WXMSW__

            }
        }
    });
}

void AMSRoad::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size) { wxWindow::Create(parent, id, pos, size); }

void AMSRoad::Update(AMSinfo amsinfo, Caninfo info, int canindex, int maxcan)
{
    if (amsinfo == m_amsinfo && m_info == info && m_canindex == canindex) {
        return;
    }

    m_amsinfo = amsinfo;
    m_info     = info;
    m_canindex = canindex;

    if (m_canindex == 0 && maxcan == 1) {
        m_rode_mode = AMSRoadMode::AMS_ROAD_MODE_END_ONLY;
    } else if (m_canindex == 0 && maxcan > 1) {
        m_rode_mode = AMSRoadMode::AMS_ROAD_MODE_END;
    } else if (m_canindex < (maxcan - 1)) {
        m_rode_mode = AMSRoadMode::AMS_ROAD_MODE_LEFT_RIGHT;
    } else if (m_canindex == (maxcan - 1)) {
        m_rode_mode = AMSRoadMode::AMS_ROAD_MODE_LEFT;
    }
    m_pass_rode_mode.push_back(AMSPassRoadMode::AMS_ROAD_MODE_NONE);
    Refresh();
}

void AMSRoad::OnVamsLoading(bool load, wxColour col /*= AMS_CONTROL_GRAY500*/)
{
    if (m_vams_loading != load) {
        m_vams_loading = load;
        if (load) m_road_color = col;
        //m_road_color = col;
        Refresh();
    }
}

void AMSRoad::SetPassRoadColour(wxColour col) { m_road_color = col; }

void AMSRoad::SetMode(AMSRoadMode mode)
{
    if (m_rode_mode != mode) {
        m_rode_mode = mode;
        Refresh();
    }
}

void AMSRoad::paintEvent(wxPaintEvent &evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AMSRoad::render(wxDC &dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({0, 0}, size, &dc, {0, 0});

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void AMSRoad::doRender(wxDC &dc)
{
    wxSize size = GetSize();

    dc.SetPen(wxPen(m_road_def_color, 2, wxSOLID));
    dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
    // left mode
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_LEFT) { dc.DrawRoundedRectangle(-10, -10, size.x / 2 + 10, size.y * 0.6 + 10, 4); }

    // left right mode
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_LEFT_RIGHT) {
        dc.DrawLine(size.x / 2, -1, size.x / 2, size.y * 0.6 - 1);
        dc.DrawLine(0, size.y * 0.6 - 1, size.x, size.y * 0.6 - 1);
    }

    // end mode
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_END) {
        dc.SetBrush(wxBrush(m_road_def_color));
        dc.DrawLine(size.x / 2, -1, size.x / 2, size.y * 0.6 - 1);
        dc.DrawLine(size.x / 2, size.y * 0.6, size.x / 2, size.y);
        dc.DrawLine(size.x / 2, size.y * 0.6 - 1, size.x, size.y * 0.6 - 1);
    }

    // end mode only
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_END_ONLY) {
        dc.SetBrush(wxBrush(m_road_def_color));
        dc.DrawLine(size.x / 2, -1, size.x / 2, size.y * 0.6 - 1);
        dc.DrawLine(size.x / 2, size.y * 0.6, size.x / 2, size.y);
    }

    // end none
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_NONE) {
        dc.SetBrush(wxBrush(m_road_def_color));
        dc.DrawLine(size.x / 2, -1, size.x / 2, size.y * 0.6 - 1);
        dc.DrawLine(size.x / 2, size.y * 0.6, size.x / 2, size.y);
        // dc.DrawLine(size.x / 2, size.y * 0.6 - 1, size.x, size.y * 0.6 - 1);
    }

    //virtual road
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_VIRTUAL_TRAY) {
        dc.SetBrush(wxBrush(m_road_def_color));
        dc.DrawLine(size.x / 2, -1, size.x / 2, size.y - 1);
    }

    // mode none
    // if (m_pass_rode_mode.size() == 1 && m_pass_rode_mode[0] == AMSPassRoadMode::AMS_ROAD_MODE_NONE) return;

    if (m_road_color.Alpha() == 0) {dc.SetPen(wxPen(*wxWHITE, m_passroad_width, wxSOLID));}
    else {dc.SetPen(wxPen(m_road_color, m_passroad_width, wxSOLID));}

    dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));

    // left pass mode
    for (auto pass_mode : m_pass_rode_mode) {
        switch (pass_mode) {
        case AMSPassRoadMode::AMS_ROAD_MODE_LEFT: dc.DrawRoundedRectangle(-10, -10, size.x / 2 + 10, size.y * 0.6 + 10, 4); break;

        case AMSPassRoadMode::AMS_ROAD_MODE_LEFT_RIGHT: dc.DrawLine(0, size.y * 0.6 - 1, size.x, size.y * 0.6 - 1); break;

        case AMSPassRoadMode::AMS_ROAD_MODE_END_TOP: dc.DrawLine(size.x / 2, -1, size.x / 2, size.y * 0.6 - 1); break;

        case AMSPassRoadMode::AMS_ROAD_MODE_END_BOTTOM: dc.DrawLine(size.x / 2, size.y * 0.6, size.x / 2, size.y); break;

        case AMSPassRoadMode::AMS_ROAD_MODE_END_RIGHT: dc.DrawLine(size.x / 2, size.y * 0.6 - 1, size.x, size.y * 0.6 - 1); break;

        default: break;
        }
    }

    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_VIRTUAL_TRAY && m_vams_loading) {
        dc.DrawLine(size.x / 2, -1, size.x / 2, size.y - 1);
    }

    // end mode
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_END || m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_END_ONLY) {
        dc.SetPen(wxPen(m_road_def_color, 2, wxSOLID));
        dc.SetBrush(wxBrush(m_road_def_color));
        dc.DrawRoundedRectangle(size.x * 0.37 / 2, size.y * 0.6 - size.y / 6, size.x * 0.63, size.y / 3, m_radius);
    }

    if (m_canindex == 3) {

        if (m_amsinfo.ams_humidity >= 1 && m_amsinfo.ams_humidity <= 5) {m_show_humidity = true;}
        else {m_show_humidity = false;}

        if (m_amsinfo.ams_humidity >= 1 && m_amsinfo.ams_humidity <= 5) {

            int hum_index = m_amsinfo.ams_humidity - 1;
            if (wxGetApp().dark_mode()) {
                hum_index += 5;
            }

            if (hum_index >= 0) {
                dc.DrawBitmap(ams_humidity_img[hum_index].bmp(), wxPoint(size.x - FromDIP(33), size.y - FromDIP(33)));
            }
        }
        else {
            //to do ...
        }
    }
}

void AMSRoad::UpdatePassRoad(int tag_index, AMSPassRoadType type, AMSPassRoadSTEP step) {}

void AMSRoad::OnPassRoad(std::vector<AMSPassRoadMode> prord_list)
{
    // AMS_ROAD_MODE_NONE, AMS_ROAD_MODE_LEFT, AMS_ROAD_MODE_LEFT_RIGHT, AMS_ROAD_MODE_END_TOP, AMS_ROAD_MODE_END_BOTTOM, AMS_ROAD_MODE_END_RIGHT,
    // AMS_ROAD_MODE_LEFT, AMS_ROAD_MODE_LEFT_RIGHT, AMS_ROAD_MODE_END,

    m_pass_rode_mode.clear();
    auto left_types       = std::vector<AMSPassRoadMode>{AMSPassRoadMode::AMS_ROAD_MODE_NONE, AMSPassRoadMode::AMS_ROAD_MODE_LEFT};
    auto left_right_types = std::vector<AMSPassRoadMode>{AMSPassRoadMode::AMS_ROAD_MODE_NONE, AMSPassRoadMode::AMS_ROAD_MODE_LEFT, AMSPassRoadMode::AMS_ROAD_MODE_LEFT_RIGHT};
    auto end_types        = std::vector<AMSPassRoadMode>{AMSPassRoadMode::AMS_ROAD_MODE_NONE, AMSPassRoadMode::AMS_ROAD_MODE_END_TOP, AMSPassRoadMode::AMS_ROAD_MODE_END_BOTTOM,
                                                  AMSPassRoadMode::AMS_ROAD_MODE_END_RIGHT};

    // left
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_LEFT) {
        for (auto i = 0; i < prord_list.size(); i++) {
            std::vector<AMSPassRoadMode>::iterator iter = std::find(left_types.begin(), left_types.end(), prord_list[i]);
            if (iter != left_types.end()) m_pass_rode_mode.push_back(prord_list[i]);

            if (prord_list[i] == AMSPassRoadMode::AMS_ROAD_MODE_NONE) {
                m_pass_rode_mode = std::vector<AMSPassRoadMode>{AMSPassRoadMode::AMS_ROAD_MODE_NONE};
                break;
            }
        }
    }

    // left right
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_LEFT_RIGHT) {
        for (auto i = 0; i < prord_list.size(); i++) {
            std::vector<AMSPassRoadMode>::iterator iter = std::find(left_right_types.begin(), left_right_types.end(), prord_list[i]);
            if (iter != left_right_types.end()) m_pass_rode_mode.push_back(prord_list[i]);

            if (prord_list[i] == AMSPassRoadMode::AMS_ROAD_MODE_NONE) {
                m_pass_rode_mode = std::vector<AMSPassRoadMode>{AMSPassRoadMode::AMS_ROAD_MODE_NONE};
                break;
            }
        }
    }

    // left end
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_END || m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_END_ONLY) {
        for (auto i = 0; i < prord_list.size(); i++) {
            std::vector<AMSPassRoadMode>::iterator iter = std::find(end_types.begin(), end_types.end(), prord_list[i]);
            if (iter != end_types.end()) m_pass_rode_mode.push_back(prord_list[i]);

            if (prord_list[i] == AMSPassRoadMode::AMS_ROAD_MODE_NONE) {
                m_pass_rode_mode = std::vector<AMSPassRoadMode>{AMSPassRoadMode::AMS_ROAD_MODE_NONE};
                break;
            }
        }
    }
}

/*************************************************
Description:AMSControl
**************************************************/
AMSItem::AMSItem() {}

AMSItem::AMSItem(wxWindow *parent, wxWindowID id, AMSinfo amsinfo, const wxSize cube_size, const wxPoint &pos, const wxSize &size) : AMSItem()
{
    m_amsinfo   = amsinfo;
    m_cube_size = cube_size;
    create(parent, id, pos, AMS_ITEM_SIZE);
    Bind(wxEVT_PAINT, &AMSItem::paintEvent, this);
    Bind(wxEVT_ENTER_WINDOW, &AMSItem::OnEnterWindow, this);
    Bind(wxEVT_LEAVE_WINDOW, &AMSItem::OnLeaveWindow, this);
}

void AMSItem::Open()
{
    m_open = true;
    Show();
}

void AMSItem::Close()
{
    m_open = false;
    Hide();
}

void AMSItem::Update(AMSinfo amsinfo)
{
    m_amsinfo = amsinfo;
}

void AMSItem::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    m_ts_bitmap_cube = new ScalableBitmap(this, "ts_bitmap_cube", 14);
    wxWindow::Create(parent, id, pos, size);
    SetMinSize(AMS_ITEM_SIZE);
    SetMaxSize(AMS_ITEM_SIZE);
    SetBackgroundColour(AMS_CONTROL_WHITE_COLOUR);
    Refresh();
}

void AMSItem::OnEnterWindow(wxMouseEvent &evt)
{
    // m_hover = true;
    // Refresh();
}

void AMSItem::OnLeaveWindow(wxMouseEvent &evt)
{
    // m_hover = false;
    // Refresh();
}

void AMSItem::OnSelected()
{
    if (!wxWindow::IsEnabled()) { return; }
    m_selected = true;
    Refresh();
}

void AMSItem::UnSelected()
{
    m_selected = false;
    Refresh();
}

bool AMSItem::Enable(bool enable) { return wxWindow::Enable(enable); }

void AMSItem::paintEvent(wxPaintEvent &evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AMSItem::render(wxDC &dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({0, 0}, size, &dc, {0, 0});

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void AMSItem::doRender(wxDC &dc)
{
    wxSize size = GetSize();
    dc.SetPen(wxPen(StateColor::darkModeColorFor(m_background_colour)));
    dc.SetBrush(wxBrush(StateColor::darkModeColorFor(m_background_colour)));
    dc.DrawRoundedRectangle(0, 0, size.x, size.y, 3);

    auto left = m_padding;
    for (std::vector<Caninfo>::iterator iter = m_amsinfo.cans.begin(); iter != m_amsinfo.cans.end(); iter++) {
        dc.SetPen(wxPen(*wxTRANSPARENT_PEN));

        if (wxWindow::IsEnabled()) {
            wxColour color = iter->material_colour;
            change_the_opacity(color);
            dc.SetBrush(wxBrush(color));
        } else {
            dc.SetBrush(AMS_CONTROL_DISABLE_COLOUR);
        }

        if (iter->material_cols.size() > 1) {
            int fleft = left;
            float total_width = AMS_ITEM_CUBE_SIZE.x;
            int gwidth = std::round(total_width / (iter->material_cols.size() - 1));
            if (iter->ctype == 0) {
                for (int i = 0; i < iter->material_cols.size() - 1; i++) {

                    if ((fleft + gwidth) > (AMS_ITEM_CUBE_SIZE.x)) {
                        gwidth = (fleft + AMS_ITEM_CUBE_SIZE.x) - fleft;
                    }

                    auto rect = wxRect(fleft, (size.y - AMS_ITEM_CUBE_SIZE.y) / 2, gwidth, AMS_ITEM_CUBE_SIZE.y);
                    dc.GradientFillLinear(rect, iter->material_cols[i], iter->material_cols[i + 1], wxEAST);
                    fleft += gwidth;
                }
            } else {
                int cols_size = iter->material_cols.size();
                for (int i = 0; i < cols_size; i++) {
                    dc.SetBrush(wxBrush(iter->material_cols[i]));
                    float x = left + total_width * i / cols_size;
                    dc.DrawRoundedRectangle(x, (size.y - AMS_ITEM_CUBE_SIZE.y) / 2, total_width / cols_size, AMS_ITEM_CUBE_SIZE.y , 0);
                }
            }

            dc.SetPen(wxPen(StateColor::darkModeColorFor(m_background_colour)));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRoundedRectangle(left - 1, (size.y - AMS_ITEM_CUBE_SIZE.y) / 2 - 1, AMS_ITEM_CUBE_SIZE.x + 2, AMS_ITEM_CUBE_SIZE.y + 2, 2);

        }else {
            if (iter->material_colour.Alpha() == 0) {
                dc.DrawBitmap(m_ts_bitmap_cube->bmp(),left,(size.y - AMS_ITEM_CUBE_SIZE.y) / 2);
            }
            else {
                wxRect rect(left, (size.y - AMS_ITEM_CUBE_SIZE.y) / 2, AMS_ITEM_CUBE_SIZE.x, AMS_ITEM_CUBE_SIZE.y);
                if(iter->material_state==AMSCanType::AMS_CAN_TYPE_EMPTY){
                    dc.SetPen(wxPen(wxColor(0, 0, 0)));
                    dc.DrawRoundedRectangle(rect, 2);

                    dc.DrawLine(rect.GetRight()-1, rect.GetTop()+1, rect.GetLeft()+1, rect.GetBottom()-1);
                }
                else {
                    dc.DrawRoundedRectangle(rect, 2);
                }
            }

        }


        left += AMS_ITEM_CUBE_SIZE.x;
        left += m_space;
    }

    auto border_colour = AMS_CONTROL_BRAND_COLOUR;
    if (!wxWindow::IsEnabled()) { border_colour = AMS_CONTROL_DISABLE_COLOUR; }

    if (m_hover) {
        dc.SetPen(wxPen(border_colour, 2));
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        dc.DrawRoundedRectangle(1, 1, size.x - 1, size.y - 1, 3);

    }

    if (m_selected) {
        dc.SetPen(wxPen(border_colour, 2));
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        dc.DrawRoundedRectangle(1, 1, size.x-1, size.y-1, 3);
    }
}

void AMSItem::DoSetSize(int x, int y, int width, int height, int sizeFlags /*= wxSIZE_AUTO*/) { wxWindow::DoSetSize(x, y, width, height, sizeFlags); }

/*************************************************
Description:AmsCan
**************************************************/

AmsCans::AmsCans() {}

AmsCans::AmsCans(wxWindow *parent,AMSinfo info,  AMSModel model) : AmsCans()
{
    m_bitmap_extra_framework = ScalableBitmap(this, "ams_extra_framework_mid", 140);

    SetDoubleBuffered(true);
    m_ams_model = model;
    m_info      = info;

    wxWindow::Create(parent, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE);
    create(parent);
    Bind(wxEVT_PAINT, &AmsCans::paintEvent, this);
}

void AmsCans::create(wxWindow *parent)
{
    Freeze();
    SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);

    if (m_ams_model == AMSModel::GENERIC_AMS) {
        sizer_can = new wxBoxSizer(wxHORIZONTAL);
        for (auto it = m_info.cans.begin(); it != m_info.cans.end(); it++) {
            AddCan(*it, m_can_count, m_info.cans.size(), sizer_can);
            m_can_count++;
        }
        SetSizer(sizer_can);
    }
    else if(m_ams_model == AMSModel::EXTRA_AMS) {
        sizer_can = new wxBoxSizer(wxVERTICAL);
        sizer_can_middle = new wxBoxSizer(wxHORIZONTAL);
        sizer_can_left = new wxBoxSizer(wxVERTICAL);
        sizer_can_right = new wxBoxSizer(wxVERTICAL);

        sizer_can_left->Add(0,0,0,wxTOP,FromDIP(8));

        for (auto it = m_info.cans.begin(); it != m_info.cans.end(); it++) {
            if (m_can_count <= 1) {
                AddCan(*it, m_can_count, m_info.cans.size(), sizer_can_left);
                if (m_can_count == 0) {
                    sizer_can_left->Add(0,0,0,wxTOP,FromDIP(25));
                }
            }
            else {
                AddCan(*it, m_can_count, m_info.cans.size(), sizer_can_right);
                if (m_can_count == 2) {
                   sizer_can_right->Prepend(0, 0, 0, wxTOP, FromDIP(25));
                }
            }

            m_can_count++;
        }

        sizer_can_right->Prepend(0,0,0,wxTOP,FromDIP(8));
        sizer_can_middle->Add(0, 0, 0, wxLEFT, FromDIP(8));
        sizer_can_middle->Add(sizer_can_left, 0, wxALL, 0);
        sizer_can_middle->Add( 0, 0, 0, wxLEFT, FromDIP(20) );
        sizer_can_middle->Add(sizer_can_right, 0, wxALL, 0);
        sizer_can->Add(sizer_can_middle, 1, wxALIGN_CENTER, 0);
        SetSizer(sizer_can);
    }

    Layout();
    Fit();
    Thaw();
}

void AmsCans::AddCan(Caninfo caninfo, int canindex, int maxcan, wxBoxSizer* sizer)
{

    auto        amscan = new wxWindow(this, wxID_ANY);
    amscan->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);

    wxBoxSizer* m_sizer_ams = new wxBoxSizer(wxVERTICAL);


    auto m_panel_refresh = new AMSrefresh(amscan, m_can_count, caninfo);
    auto m_panel_lib = new AMSLib(amscan, caninfo);

    m_panel_lib->Bind(wxEVT_LEFT_DOWN, [this, canindex](wxMouseEvent& ev) {
        m_canlib_selection = canindex;
        // m_canlib_id        = caninfo.can_id;

        for (auto i = 0; i < m_can_lib_list.GetCount(); i++) {
            CanLibs* lib = m_can_lib_list[i];
            if (lib->canLib->m_can_index == m_canlib_selection) {
                wxCommandEvent evt(EVT_AMS_UNSELETED_VAMS);
                evt.SetString(m_info.ams_id);
                wxPostEvent(GetParent()->GetParent(), evt);
                lib->canLib->OnSelected();
            }
            else {
                lib->canLib->UnSelected();
            }
        }
        ev.Skip();
        });


    m_panel_lib->m_ams_model = m_ams_model;
    m_panel_lib->m_info.can_id = caninfo.can_id;
    m_panel_lib->m_can_index = canindex;


    auto m_panel_road = new AMSRoad(amscan, wxID_ANY, caninfo, canindex, maxcan, wxDefaultPosition, AMS_CAN_ROAD_SIZE);

    if (m_ams_model == AMSModel::GENERIC_AMS) {
        m_sizer_ams->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(14));
        m_sizer_ams->Add(m_panel_refresh, 0, wxALIGN_CENTER_HORIZONTAL, 0);
        m_sizer_ams->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(2));
        m_sizer_ams->Add(m_panel_lib, 1, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, FromDIP(3));
        m_sizer_ams->Add(m_panel_road, 0, wxALL, 0);
    }
    else if (m_ams_model == AMSModel::EXTRA_AMS)
    {
        m_sizer_ams = new wxBoxSizer(wxHORIZONTAL);
        m_panel_road->Hide();

        if (canindex <= 1) {
            m_sizer_ams->Add(m_panel_refresh, 0, wxALIGN_CENTER, 0);
            m_sizer_ams->Add(m_panel_lib, 0, wxALIGN_CENTER, 0);
        }
        else {
            m_sizer_ams->Add(m_panel_lib, 0, wxALIGN_CENTER, 0);
            m_sizer_ams->Add(m_panel_refresh, 0, wxALIGN_CENTER, 0);
        }
    }


    amscan->SetSizer(m_sizer_ams);
    amscan->Layout();
    amscan->Fit();

    if (m_ams_model == AMSModel::GENERIC_AMS) {
         sizer->Add(amscan, 0, wxALL, 0);
    }
    else if (m_ams_model == AMSModel::EXTRA_AMS)
    {
        if (canindex > 1) {
            sizer->Prepend(amscan, 0, wxALL, 0);
        }
        else {
            sizer->Add(amscan, 0, wxALL, 0);
        }
    }

    Canrefreshs* canrefresh = new Canrefreshs;
    canrefresh->canID = caninfo.can_id;
    canrefresh->canrefresh = m_panel_refresh;
    m_can_refresh_list.Add(canrefresh);

    CanLibs* canlib = new CanLibs;
    canlib->canID = caninfo.can_id;
    canlib->canLib = m_panel_lib;
    m_can_lib_list.Add(canlib);

    CanRoads* canroad = new CanRoads;
    canroad->canID = caninfo.can_id;
    canroad->canRoad = m_panel_road;
    m_can_road_list.Add(canroad);
}

void AmsCans::Update(AMSinfo info)
{
    m_info      = info;
    m_can_count = info.cans.size();

    for (auto i = 0; i < m_can_refresh_list.GetCount(); i++) {
        Canrefreshs *refresh = m_can_refresh_list[i];
        if (i < m_can_count) {
            refresh->canrefresh->Update(info.ams_id, info.cans[i]);
            if (!refresh->canrefresh->IsShown()) { refresh->canrefresh->Show();}

        } else {
            if (refresh->canrefresh->IsShown()) { refresh->canrefresh->Hide();}
        }
    }

    for (auto i = 0; i < m_can_lib_list.GetCount(); i++) {
        CanLibs *lib = m_can_lib_list[i];
        if (i < m_can_count) {
            lib->canLib->Update(info.cans[i]);
            if(!lib->canLib->IsShown()) { lib->canLib->Show();}
        } else {
            if(lib->canLib->IsShown()) { lib->canLib->Hide(); }
        }
    }

    if (m_ams_model == AMSModel::GENERIC_AMS) {
        for (auto i = 0; i < m_can_road_list.GetCount(); i++) {
            CanRoads *road = m_can_road_list[i];
            if (i < m_can_count) {
                road->canRoad->Update(m_info, info.cans[i], i, m_can_count);
                if (!road->canRoad->IsShown()) { road->canRoad->Show(); }
            } else {
                if (road->canRoad->IsShown()) { road->canRoad->Hide(); }
            }
        }
    }
    Layout();
}

void AmsCans::SetDefSelectCan()
{
    if (m_can_lib_list.GetCount() > 0) {
        CanLibs* lib = m_can_lib_list[0];
        m_canlib_selection =lib->canLib->m_can_index;
        m_canlib_id = lib->canLib->m_info.can_id;
        SelectCan(m_canlib_id);
    }
}


void AmsCans::SelectCan(std::string canid)
{
    for (auto i = 0; i < m_can_lib_list.GetCount(); i++) {
        CanLibs *lib = m_can_lib_list[i];
        if (lib->canLib->m_info.can_id == canid) {
            m_canlib_selection = lib->canLib->m_can_index;
        }
    }

    m_canlib_id = canid;

    for (auto i = 0; i < m_can_lib_list.GetCount(); i++) {
        CanLibs *lib = m_can_lib_list[i];
        if (lib->canLib->m_info.can_id == m_canlib_id) {
            wxCommandEvent evt(EVT_AMS_UNSELETED_VAMS);
            evt.SetString(m_info.ams_id);
            wxPostEvent(GetParent()->GetParent(), evt);
            lib->canLib->OnSelected();
        } else {
            lib->canLib->UnSelected();
        }
    }
}

wxColour AmsCans::GetTagColr(wxString canid)
{
    auto tag_colour = *wxWHITE;
    for (auto i = 0; i < m_can_lib_list.GetCount(); i++) {
        CanLibs* lib = m_can_lib_list[i];
        if (canid == lib->canLib->m_info.can_id) tag_colour = lib->canLib->GetLibColour();
    }
    return tag_colour;
}

void AmsCans::SetAmsStepExtra(wxString canid, AMSPassRoadType type, AMSPassRoadSTEP step)
{
    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1) {
        SetAmsStep(canid.ToStdString());
    }else if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2) {
        SetAmsStep(canid.ToStdString());
    }else if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3) {
        SetAmsStep(canid.ToStdString());
    }else if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
        SetAmsStep("");
    }
}

void AmsCans::SetAmsStep(wxString canid, AMSPassRoadType type, AMSPassRoadSTEP step)
{

    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
        for (auto i = 0; i < m_can_road_list.GetCount(); i++) {
            CanRoads *road = m_can_road_list[i];
            auto      pr   = std::vector<AMSPassRoadMode>{};
            pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_NONE);
            road->canRoad->OnPassRoad(pr);
        }

        return;
    }


    auto tag_can_index = -1;
    for (auto i = 0; i < m_can_road_list.GetCount(); i++) {
        CanRoads *road = m_can_road_list[i];
        if (canid == road->canRoad->m_info.can_id) { tag_can_index = road->canRoad->m_canindex; }
    }
    if (tag_can_index == -1) return;

    // get colour
    auto tag_colour = *wxWHITE;
    for (auto i = 0; i < m_can_lib_list.GetCount(); i++) {
        CanLibs *lib = m_can_lib_list[i];
        if (canid == lib->canLib->m_info.can_id) tag_colour = lib->canLib->GetLibColour();
    }

    // unload
    if (type == AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD) {
        for (auto i = 0; i < m_can_road_list.GetCount(); i++) {
            CanRoads *road = m_can_road_list[i];

            auto index = road->canRoad->m_canindex;
            auto pr    = std::vector<AMSPassRoadMode>{};

            pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_BOTTOM);
            if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_2) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_BOTTOM); }

            if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_3) {
                if (index == tag_can_index && index > 0) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_LEFT); }
                if (index < tag_can_index && index > 0) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_LEFT_RIGHT); }
                if (index == 0 && tag_can_index == index) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_TOP); }
                if (index == 0 && tag_can_index > index) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_RIGHT); }
            }

            road->canRoad->SetPassRoadColour(tag_colour);
            road->canRoad->OnPassRoad(pr);
        }
    }

    // load
    if (type == AMSPassRoadType::AMS_ROAD_TYPE_LOAD) {
        for (auto i = 0; i < m_can_road_list.GetCount(); i++) {
            CanRoads *road = m_can_road_list[i];

            auto index = road->canRoad->m_canindex;
            auto pr    = std::vector<AMSPassRoadMode>{};

            if (index == tag_can_index && index > 0) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_LEFT); }
            if (index < tag_can_index && index > 0) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_LEFT_RIGHT); }
            if (index == 0 && tag_can_index == index) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_TOP); }
            if (index == 0 && tag_can_index > index) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_RIGHT); }

            if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_2) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_BOTTOM); }

            road->canRoad->SetPassRoadColour(tag_colour);
            road->canRoad->OnPassRoad(pr);
        }
    }
}

void AmsCans::SetAmsStep(std::string can_id)
{
    if (m_road_canid != can_id) {
        m_road_canid = can_id;
        Refresh();
    }
}

void AmsCans::PlayRridLoading(wxString canid)
{
    for (auto i = 0; i < m_can_refresh_list.GetCount(); i++) {
        Canrefreshs *refresh = m_can_refresh_list[i];
        if (refresh->canrefresh->m_info.can_id == canid) { refresh->canrefresh->PlayLoading(); }
    }
}

std::string AmsCans::GetCurrentCan()
{
    if (m_canlib_selection < 0)
        return "";

    return wxString::Format("%d", m_canlib_selection).ToStdString();
}

void AmsCans::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AmsCans::render(wxDC& dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({ 0, 0 }, size, &dc, { 0, 0 });

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void AmsCans::doRender(wxDC& dc)
{
    wxSize     size = GetSize();
    dc.DrawBitmap(m_bitmap_extra_framework.bmp(), (size.x - m_bitmap_extra_framework.GetBmpSize().x) / 2, (size.y - m_bitmap_extra_framework.GetBmpSize().y) / 2);

    //road for extra
    if (m_ams_model == AMSModel::EXTRA_AMS) {

        auto end_top = size.x / 2 - FromDIP(99);
        auto passroad_width = 6;

        for (auto i = 0; i < m_can_lib_list.GetCount(); i++) {
            CanLibs* lib = m_can_lib_list[i];

            if (m_road_canid.empty()) {
                lib->canLib->on_pass_road(false);
            }
            else {
                if (lib->canLib->m_info.can_id == m_road_canid) {
                    m_road_colour = lib->canLib->m_info.material_colour;
                    lib->canLib->on_pass_road(true);
                }
            }
        }


        // A1
        dc.SetPen(wxPen(AMS_CONTROL_GRAY500, 2, wxSOLID));
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));

        try
        {
            auto a1_top = size.y / 2 - FromDIP(4);
            auto a1_left = m_can_lib_list[0]->canLib->GetScreenPosition().x + m_can_lib_list[0]->canLib->GetSize().x / 2;
            auto local_pos1 = GetScreenPosition().x + GetSize().x / 2;
            a1_left = size.x / 2 + (a1_left - local_pos1);
            dc.DrawLine(a1_left, FromDIP(30), a1_left, a1_top);
            dc.DrawLine(a1_left, a1_top, end_top, a1_top);


            // A2
            auto a2_top = size.y / 2 + FromDIP(8);
            auto a2_left = m_can_lib_list[1]->canLib->GetScreenPosition().x + m_can_lib_list[1]->canLib->GetSize().x / 2;
            auto local_pos2 = GetScreenPosition().x + GetSize().x / 2;
            a2_left = size.x / 2 + (a2_left - local_pos2);
            dc.DrawLine(a2_left, FromDIP(160), a2_left, a2_top);
            dc.DrawLine(a2_left, a2_top, end_top, a2_top);

            // A3
            auto a3_top = size.y / 2 + FromDIP(4);
            auto a3_left = m_can_lib_list[2]->canLib->GetScreenPosition().x + m_can_lib_list[2]->canLib->GetSize().x / 2;
            auto local_pos3 = GetScreenPosition().x + GetSize().x / 2;
            a3_left = size.x / 2 + (a3_left - local_pos3);
            dc.DrawLine(a3_left, FromDIP(160), a3_left, a3_top);
            dc.DrawLine(a3_left, a3_top, end_top, a3_top);


            // A4
            auto a4_top = size.y / 2;
            auto a4_left = m_can_lib_list[3]->canLib->GetScreenPosition().x + m_can_lib_list[3]->canLib->GetSize().x / 2;
            auto local_pos4 = GetScreenPosition().x + GetSize().x / 2;
            a4_left = size.x / 2 + (a4_left - local_pos4);
            dc.DrawLine(a4_left, FromDIP(30), a4_left, a4_top);
            dc.DrawLine(a4_left, a4_top, end_top, a4_top);


            if (!m_road_canid.empty()) {
                if (m_road_canid == "0") {
                    dc.SetPen(wxPen(m_road_colour, passroad_width, wxSOLID));
                    dc.DrawLine(a1_left, FromDIP(30), a1_left, a1_top);
                    dc.DrawLine(a1_left, a1_top, end_top, a1_top);
                }

                if (m_road_canid == "1") {
                    dc.SetPen(wxPen(m_road_colour, passroad_width, wxSOLID));
                    dc.DrawLine(a2_left, FromDIP(160), a2_left, a2_top);
                    dc.DrawLine(a2_left, a2_top, end_top, a2_top);
                }

                if (m_road_canid == "2") {
                    dc.SetPen(wxPen(m_road_colour, passroad_width, wxSOLID));
                    dc.DrawLine(a3_left, FromDIP(160), a3_left, a3_top);
                    dc.DrawLine(a3_left, a3_top, end_top, a3_top);
                }

                if (m_road_canid == "3") {
                    dc.SetPen(wxPen(m_road_colour, passroad_width, wxSOLID));
                    dc.DrawLine(a4_left, FromDIP(30), a4_left, a4_top);
                    dc.DrawLine(a4_left, a4_top, end_top, a4_top);
                }
            }

            //to Extruder
            dc.SetPen(wxPen(AMS_CONTROL_GRAY500, 2, wxSOLID));
            dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));

            dc.DrawLine(end_top, a1_top, end_top, size.y);

            if (!m_road_canid.empty()) {
                if (!m_road_canid.empty()) {
                    if (m_road_canid == "0") {
                        dc.SetPen(wxPen(m_road_colour, passroad_width, wxSOLID));
                        dc.DrawLine(end_top, a1_top, end_top, size.y);
                    }
                    else if (m_road_canid == "1") {
                        dc.SetPen(wxPen(m_road_colour, passroad_width, wxSOLID));
                        dc.DrawLine(end_top, a2_top, end_top, size.y);
                    }
                    else if (m_road_canid == "2") {
                        dc.SetPen(wxPen(m_road_colour, passroad_width, wxSOLID));
                        dc.DrawLine(end_top, a3_top, end_top, size.y);
                    }
                    else if (m_road_canid == "3") {
                        dc.SetPen(wxPen(m_road_colour, passroad_width, wxSOLID));
                        dc.DrawLine(end_top, a4_top, end_top, size.y);
                    }
                }
            }
        }
        catch (...){}
    }
}

void AmsCans::StopRridLoading(wxString canid)
{
    for (auto i = 0; i < m_can_refresh_list.GetCount(); i++) {
        Canrefreshs *refresh = m_can_refresh_list[i];
        if (refresh->canrefresh->m_info.can_id == canid) { refresh->canrefresh->StopLoading(); }
    }
}

void AmsCans::msw_rescale()
{
    for (auto i = 0; i < m_can_refresh_list.GetCount(); i++) {
        Canrefreshs *refresh = m_can_refresh_list[i];
        refresh->canrefresh->msw_rescale();
    }

    for (auto i = 0; i < m_can_lib_list.GetCount(); i++) {
        CanLibs* lib = m_can_lib_list[i];
        lib->canLib->msw_rescale();
    }
}

void AmsCans::show_sn_value(bool show)
{
    for (auto i = 0; i < m_can_lib_list.GetCount(); i++) {
        CanLibs* lib = m_can_lib_list[i];
        lib->canLib->show_kn_value(show);
    }
}


/*************************************************
Description:AMSControl
**************************************************/
// WX_DEFINE_OBJARRAY(AmsItemsHash);
#define AMS_CANS_SIZE wxSize(FromDIP(284), -1)
#define AMS_CANS_WINDOW_SIZE wxSize(FromDIP(264), -1)
#define SINGLE_SLOT_AMS_PANEL_SIZE wxSize(FromDIP(264), FromDIP(160))


AMSControl::AMSControl(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
    : wxSimplebook(parent, wxID_ANY, pos, size)
    , m_Humidity_tip_popup(AmsHumidityTipPopup(this))
    , m_percent_humidity_dry_popup(new uiAmsPercentHumidityDryPopup(this))
    , m_ams_introduce_popup(AmsIntroducePopup(this))
{
    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (dev) {
        MachineObject *obj = dev->get_selected_machine();
        parse_object(obj);
    }

    m_extder_data.total_extder_count = 1;
    SetBackgroundColour(*wxWHITE);
    // normal mode
    //Freeze();
    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxVERTICAL);
    m_amswin                 = new wxWindow(this, wxID_ANY);
    m_amswin->SetBackgroundColour(*wxWHITE);
    //m_amswin->SetBackgroundColour(wxColour(0x00CED1));
    m_amswin->SetSize(wxSize(FromDIP(578), -1));
    m_amswin->SetMinSize(wxSize(FromDIP(578), -1));


    m_sizer_ams_items = new wxBoxSizer(wxHORIZONTAL);

    /*right items*/
    m_panel_prv_left = new wxScrolledWindow(m_amswin, wxID_ANY);
    m_panel_prv_left->SetScrollRate(10, 0);
    m_panel_prv_left->SetSize(AMS_ITEMS_PANEL_SIZE);
    m_panel_prv_left->SetMinSize(AMS_ITEMS_PANEL_SIZE);
    //m_panel_prv_left->SetBackgroundColour(0x4169E1);
    m_panel_prv_left->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    m_sizer_prv_left = new wxBoxSizer(wxHORIZONTAL);
    m_panel_prv_left->SetSizer(m_sizer_prv_left);
    m_panel_prv_left->Layout();
    //m_sizer_items_left->Fit(m_panel_prv_left);

    /*right items*/
    m_panel_prv_right = new wxScrolledWindow(m_amswin, wxID_ANY);
    m_panel_prv_right->SetScrollRate(10, 0);
    m_panel_prv_right->SetSize(AMS_ITEMS_PANEL_SIZE);
    m_panel_prv_right->SetMinSize(AMS_ITEMS_PANEL_SIZE);
    //m_panel_prv_right->SetBackgroundColour(0x4169E1);
    m_panel_prv_right->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    m_sizer_prv_right = new wxBoxSizer(wxHORIZONTAL);
    m_panel_prv_right->SetSizer(m_sizer_prv_right);
    m_panel_prv_right->Layout();
    //m_sizer_items_right->Fit(m_panel_prv_right);

    /*m_sizer_ams_items->Add(m_panel_prv_left, 0, wxALIGN_CENTER|wxLEFT|wxRIGHT, FromDIP(5));
    m_sizer_ams_items->Add(m_panel_prv_right, 0, wxALIGN_CENTER|wxLEFT|wxRIGHT, FromDIP(5));*/
    m_sizer_ams_items->Add(m_panel_prv_left, 0, wxLEFT | wxRIGHT, FromDIP(5));
    m_sizer_ams_items->Add(m_panel_prv_right, 0, wxLEFT | wxRIGHT, FromDIP(5));

    //m_panel_prv_right->Hide();

    //m_sizer_ams_body = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_ams_body = new wxBoxSizer(wxHORIZONTAL);

    //ams area
    m_sizer_ams_area_left = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_ams_area_right = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_down_road = new wxBoxSizer(wxHORIZONTAL);

    m_simplebook_ams_left = new wxSimplebook(m_amswin, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    m_simplebook_ams_left->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    //m_sizer_ams_area_left->Add(m_simplebook_ams_left, 0, wxLEFT | wxRIGHT, FromDIP(5));
    m_sizer_ams_area_left->Add(m_simplebook_ams_left, 0, wxALIGN_CENTER, 0);

    m_simplebook_ams_right = new wxSimplebook(m_amswin, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    m_simplebook_ams_right->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    //m_sizer_ams_area_right->Add(m_simplebook_ams_right, 0, wxLEFT | wxRIGHT, FromDIP(5));
    m_sizer_ams_area_right->Add(m_simplebook_ams_right, 0, wxALIGN_CENTER, 0);

    m_panel_down_road = new wxPanel(m_amswin, wxID_ANY, wxDefaultPosition, AMS_DOWN_ROAD_SIZE, 0);
    m_panel_down_road->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);

    m_down_road = new AMSRoadDownPart(m_panel_down_road, wxID_ANY, wxDefaultPosition, AMS_DOWN_ROAD_SIZE);
    m_sizer_down_road->Add(m_panel_down_road, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 0);

    // ams mode
    //
    m_simplebook_ams_right->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);


    m_sizer_ams_area_left->Layout();
    m_sizer_ams_area_right->Layout();


    m_sizer_ams_option = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_option_left = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_option_mid = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_option_right = new wxBoxSizer(wxHORIZONTAL);

    auto m_panel_option_left    = new wxPanel(m_amswin);
    auto m_panel_option_right   = new wxPanel(m_amswin);

    m_panel_option_left->SetBackgroundColour(*wxWHITE);
    m_panel_option_right->SetBackgroundColour(*wxWHITE);

    m_panel_option_left->SetSizer(m_sizer_option_left);
    m_panel_option_right->SetSizer(m_sizer_option_right);

    m_panel_option_left->SetMinSize(wxSize(FromDIP(180), -1));
    m_panel_option_left->SetMaxSize(wxSize(FromDIP(180), -1));

    m_panel_option_right->SetMinSize(wxSize(FromDIP(180), -1));
    m_panel_option_right->SetMaxSize(wxSize(FromDIP(180), -1));

    StateColor btn_bg_green(std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled),
        std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Pressed),
        std::pair<wxColour, int>(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Normal));

    StateColor btn_bd_green(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Enabled));

    StateColor btn_bd_white(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));

    StateColor btn_text_green(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Enabled));

    StateColor btn_text_white(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));


    /*option left*/
    m_button_auto_refill = new Button(m_panel_option_left, _L("Auto-refill"));
    m_button_auto_refill->SetBackgroundColor(btn_bg_white);
    m_button_auto_refill->SetBorderColor(btn_bd_white);
    m_button_auto_refill->SetTextColor(btn_text_white);
    m_button_auto_refill->SetFont(Label::Body_13);
    m_button_auto_refill->SetMinSize(wxSize(FromDIP(80), FromDIP(34)));
    m_button_auto_refill->SetMaxSize(wxSize(FromDIP(80), FromDIP(34)));

    m_button_ams_setting_normal = ScalableBitmap(this, "ams_setting_normal", 24);
    m_button_ams_setting_hover = ScalableBitmap(this, "ams_setting_hover", 24);
    m_button_ams_setting_press = ScalableBitmap(this, "ams_setting_press", 24);

    m_button_ams_setting = new wxStaticBitmap(m_panel_option_left, wxID_ANY, m_button_ams_setting_normal.bmp(), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)));
    m_sizer_option_left->Add(m_button_auto_refill, 0, wxALIGN_CENTER, 0);
    m_sizer_option_left->Add(0, 0, 0, wxLEFT, FromDIP(20));
    m_sizer_option_left->Add(m_button_ams_setting, 0, wxALIGN_CENTER, 0);


    /*option mid*/
    m_extruder = new AMSextruder(m_amswin, wxID_ANY, m_extder_data.total_extder_count, wxDefaultPosition, AMS_EXTRUDER_SIZE);
    m_sizer_option_mid->Add( m_extruder, 0, wxALIGN_CENTER, 0 );


    /*option right*/
    m_button_extruder_feed = new Button(m_panel_option_right, _L("Load"));
    m_button_extruder_feed->SetFont(Label::Body_13);
    m_button_extruder_feed->SetBackgroundColor(btn_bg_green);
    m_button_extruder_feed->SetBorderColor(btn_bd_green);
    m_button_extruder_feed->SetTextColor(btn_text_green);
    m_button_extruder_feed->SetMinSize(wxSize(FromDIP(80),FromDIP(34)));
    m_button_extruder_feed->SetMaxSize(wxSize(FromDIP(80),FromDIP(34)));


    if (wxGetApp().app_config->get("language") == "de_DE") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "fr_FR") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ru_RU") m_button_extruder_feed->SetLabel("Load");
    if (wxGetApp().app_config->get("language") == "nl_NL") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "hu_HU") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ja_JP") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "sv_SE") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "cs_CZ") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "uk_UA") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "pt_BR") m_button_extruder_feed->SetLabel("Load");

    m_button_extruder_back = new Button(m_panel_option_right, _L("Unload"));
    m_button_extruder_back->SetBackgroundColor(btn_bg_white);
    m_button_extruder_back->SetBorderColor(btn_bd_white);
    m_button_extruder_back->SetTextColor(btn_text_white);
    m_button_extruder_back->SetFont(Label::Body_13);
    m_button_extruder_back->SetMinSize(wxSize(FromDIP(80), FromDIP(34)));
    m_button_extruder_back->SetMaxSize(wxSize(FromDIP(80), FromDIP(34)));

    if (wxGetApp().app_config->get("language") == "de_DE") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "fr_FR") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ru_RU") m_button_extruder_back->SetLabel("Unload");
    if (wxGetApp().app_config->get("language") == "nl_NL") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "hu_HU") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ja_JP") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "sv_SE") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "cs_CZ") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "uk_UA") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "pt_BR") m_button_extruder_back->SetLabel("Unload");


    //m_sizer_option_right->Add(0, 0, 1, wxEXPAND, 0);
    m_sizer_option_right->Add(m_button_extruder_back, 0, wxLEFT, FromDIP(0));
    m_sizer_option_right->Add(m_button_extruder_feed, 0, wxLEFT, FromDIP(20));

    m_panel_option_left->Layout();
    m_panel_option_right->Layout();

    m_sizer_ams_option->Add(m_panel_option_left, 0, wxALIGN_LEFT, 0);
    m_sizer_ams_option->Add( 0, 0, 1, wxEXPAND, 0);
    m_sizer_ams_option->Add(m_sizer_option_mid, 0, wxALIGN_RIGHT, 0);
    m_sizer_ams_option->Add( 0, 0, 1, wxEXPAND, 0);
    m_sizer_ams_option->Add(m_panel_option_right, 0, wxALIGN_RIGHT, 0);


    m_sizer_ams_body->Add(m_sizer_ams_area_left, wxALIGN_CENTER, 0);
    m_sizer_ams_body->AddSpacer(FromDIP(10));
    m_sizer_ams_body->Add(m_sizer_ams_area_right, wxALIGN_CENTER, 0);

    m_sizer_body->Add(m_sizer_ams_items, 0, wxALIGN_CENTER, 0);
    m_sizer_body->Add(0, 0, 1, wxEXPAND | wxTOP, FromDIP(10));
    m_sizer_body->Add(m_sizer_ams_body, 0, wxALIGN_CENTER, 0);
    m_sizer_body->Add(m_sizer_down_road, 0, wxALIGN_CENTER, 0);
    m_sizer_body->Add(m_sizer_ams_option, 0, wxEXPAND, 0);

    m_amswin->SetSizer(m_sizer_body);
    m_amswin->Layout();
    m_amswin->Fit();
    //Thaw();

    SetSize(m_amswin->GetSize());
    SetMinSize(m_amswin->GetSize());


    AddPage(m_amswin, wxEmptyString, false);


    m_button_extruder_feed->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AMSControl::on_filament_load), NULL, this);
    m_button_extruder_back->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AMSControl::on_filament_unload), NULL, this);
    m_button_auto_refill->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AMSControl::auto_refill), NULL, this);

    m_button_ams_setting->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& e) {
        m_button_ams_setting->SetBitmap(m_button_ams_setting_hover.bmp());
        e.Skip();
    });
    m_button_ams_setting->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        m_button_ams_setting->SetBitmap(m_button_ams_setting_press.bmp());
        on_ams_setting_click(e);
        e.Skip();
    });

    m_button_ams_setting->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e) {
        m_button_ams_setting->SetBitmap(m_button_ams_setting_normal.bmp());
        e.Skip();
    });

    Bind(EVT_AMS_SHOW_HUMIDITY_TIPS, [this](wxCommandEvent& evt) {
        uiAmsHumidityInfo *info    = (uiAmsHumidityInfo *) evt.GetClientData();
        if (info)
        {
            if (info->humidity_percent >= 0)
            {
                m_percent_humidity_dry_popup->Update(info);

                wxPoint img_pos = ClientToScreen(wxPoint(0, 0));
                wxPoint popup_pos(img_pos.x - m_percent_humidity_dry_popup->GetSize().GetWidth() + FromDIP(150), img_pos.y - FromDIP(80));
                m_percent_humidity_dry_popup->Position(popup_pos, wxSize(0, 0));
                m_percent_humidity_dry_popup->Popup();
            }
            else
            {
                wxPoint img_pos = ClientToScreen(wxPoint(0, 0));
                wxPoint popup_pos(img_pos.x - m_Humidity_tip_popup.GetSize().GetWidth() + FromDIP(150), img_pos.y - FromDIP(80));
                m_Humidity_tip_popup.Position(popup_pos, wxSize(0, 0));

                int humidity_value = info->humidity_level;
                if (humidity_value > 0 && humidity_value <= 5) { m_Humidity_tip_popup.set_humidity_level(humidity_value); }
                m_Humidity_tip_popup.Popup();
            }
        }

        delete info;
    });
    Bind(EVT_AMS_ON_SELECTED, &AMSControl::AmsSelectedSwitch, this);
}

void AMSControl::on_retry()
{
    post_event(wxCommandEvent(EVT_AMS_RETRY));
}

AMSControl::~AMSControl() {}

std::string AMSControl::GetCurentAms() {
    return m_current_ams;
}
std::string AMSControl::GetCurentShowAms(AMSPanelPos pos) {
    if (pos == AMSPanelPos::RIGHT_PANEL){
        return m_current_show_ams_right;
    }
    else{
        return m_current_show_ams_left;
    }
}

std::string AMSControl::GetCurrentCan(std::string amsid)
{
    std::string current_can;
    for (auto ams_item : m_ams_item_list) {
        AmsItem* item = ams_item.second;
        if (item == nullptr){
            continue;
        }
        if (item->get_ams_id() == amsid) {
            current_can = item->GetCurrentCan();
            return current_can;
        }
    }
    return current_can;
}

bool AMSControl::IsAmsInRightPanel(std::string ams_id) {
    if (m_extder_data.total_extder_count == 2){
        if (m_ams_item_list.find(ams_id) != m_ams_item_list.end() && m_ams_item_list[ams_id]->get_nozzle_id() == MAIN_NOZZLE_ID) {
            return true;
        }
        else{
            return false;
        }
    }
    else{
        for (auto id : m_item_ids[MAIN_NOZZLE_ID]){
            if (id == ams_id){
                return true;
            }
        }
        return false;
    }
}

void AMSControl::AmsSelectedSwitch(wxCommandEvent& event) {
    std::string ams_id_selected = std::to_string(event.GetInt());
    if (m_current_ams != ams_id_selected){
        m_current_ams = ams_id_selected;
    }
    if (m_current_show_ams_left != ams_id_selected && m_current_show_ams_left != "") {
        auto item = m_ams_item_list[m_current_show_ams_left];
        if (!item) return;
        try{
            const auto& can_lib_list = item->get_can_lib_list();
            for (auto can : can_lib_list) {
                can.second->UnSelected();
            }
        }
        catch (...){
            ;
        }
    }
    else if (m_current_show_ams_right != ams_id_selected && m_current_show_ams_right != "") {
        auto item = m_ams_item_list[m_current_show_ams_right];
        if (!item) return;
        try {
            const auto &can_lib_list = item->get_can_lib_list();
            for (auto can : can_lib_list) {
                can.second->UnSelected();
            }
        }
        catch (...) {
            ;
        }
    }
}

wxColour AMSControl::GetCanColour(std::string amsid, std::string canid)
{
    wxColour col = *wxWHITE;
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == amsid) {
            for (auto o = 0; o < m_ams_info[i].cans.size(); o++) {
                if (m_ams_info[i].cans[o].can_id == canid) {
                    col = m_ams_info[i].cans[o].material_colour;
                }
            }
        }
    }
    return col;
}

void AMSControl::SetActionState(bool button_status[])
{
    if (button_status[ActionButton::ACTION_BTN_LOAD]) m_button_extruder_feed->Enable();
    else m_button_extruder_feed->Disable();

    if (button_status[ActionButton::ACTION_BTN_UNLOAD]) m_button_extruder_back->Enable();
    else m_button_extruder_back->Disable();
}

void AMSControl::EnterNoneAMSMode()
{
    //m_vams_lib->m_ams_model = m_ext_model;
    if(m_is_none_ams_mode == AMSModel::EXT_AMS) return;
    m_panel_prv_left->Hide();

    m_simplebook_ams_left->SetSelection(0);
    m_extruder->no_ams_mode(true);
    //m_button_ams_setting->Hide();
    //m_button_extruder_feed->Show();
    //m_button_extruder_back->Show();

    ShowFilamentTip(false);
    m_amswin->Layout();
    m_amswin->Fit();
    Layout();
    m_is_none_ams_mode = AMSModel::EXT_AMS;
}

void AMSControl::EnterGenericAMSMode()
{
    if(m_is_none_ams_mode == AMSModel::GENERIC_AMS) return;
    m_extruder->no_ams_mode(false);
    m_amswin->Layout();
    m_amswin->Fit();
    Layout();
    m_is_none_ams_mode = AMSModel::GENERIC_AMS;
}

void AMSControl::EnterExtraAMSMode()
{
    //m_vams_lib->m_ams_model = m_ext_model;
    if(m_is_none_ams_mode == AMSModel::AMS_LITE) return;
    m_panel_prv_left->Hide();

    m_simplebook_ams_left->SetSelection(2);
    m_extruder->no_ams_mode(false);
    m_amswin->Layout();
    m_amswin->Fit();
    Layout();
    Refresh(true);
    m_is_none_ams_mode = AMSModel::AMS_LITE;

}

void AMSControl::PlayRridLoading(wxString amsid, wxString canid)
{
    auto iter = m_ams_item_list.find(amsid.ToStdString());

    if (iter != m_ams_item_list.end()) {
        AmsItem* cans = iter->second;
        cans->PlayRridLoading(canid);
    }
}

void AMSControl::StopRridLoading(wxString amsid, wxString canid)
{
    auto iter = m_ams_item_list.find(amsid.ToStdString());

    if (iter != m_ams_item_list.end()) {
        AmsItem* cans = iter->second;
        cans->StopRridLoading(canid);
    }
}

void AMSControl::msw_rescale()
{
    m_button_ams_setting_normal.msw_rescale();
    m_button_ams_setting_hover.msw_rescale();
    m_button_ams_setting_press.msw_rescale();
    m_button_ams_setting->SetBitmap(m_button_ams_setting_normal.bmp());

    m_extruder->msw_rescale();

    if (m_button_extruder_feed) m_button_extruder_feed->SetMinSize(wxSize(FromDIP(80), FromDIP(34)));
    if (m_button_extruder_feed) m_button_extruder_feed->SetMaxSize(wxSize(FromDIP(80), FromDIP(34)));
    if (m_button_extruder_back) m_button_extruder_back->SetMinSize(wxSize(FromDIP(80), FromDIP(34)));
    if (m_button_extruder_back) m_button_extruder_back->SetMaxSize(wxSize(FromDIP(80), FromDIP(34)));
    if (m_button_auto_refill) m_button_auto_refill->SetMinSize(wxSize(FromDIP(80), FromDIP(34)));
    if (m_button_auto_refill) m_button_auto_refill->SetMaxSize(wxSize(FromDIP(80), FromDIP(34)));
    if (m_button_ams_setting) m_button_ams_setting->SetMinSize(wxSize(FromDIP(25), FromDIP(24)));


    for (auto ams_item : m_ams_item_list) {
        if (ams_item.second){
            ams_item.second->msw_rescale();
        }
    }
    for (auto ams_prv : m_ams_preview_list) {
        if (ams_prv.second){
            ams_prv.second->msw_rescale();
        }
    }
    for (auto ext_img : m_ext_image_list) {
        if (ext_img.second) {
            ext_img.second->msw_rescale();
        }
    }
    if (m_down_road){
        m_down_road->msw_rescale();
    }

    if (m_percent_humidity_dry_popup){
        m_percent_humidity_dry_popup->msw_rescale();
    }

    Layout();
    Refresh();
}

void AMSControl::CreateAms()
{
    auto caninfo0_0 = Caninfo{"def_can_0", (""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_VIRTUAL};
    auto caninfo0_1 = Caninfo{"def_can_1", (""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_VIRTUAL };
    auto caninfo0_2 = Caninfo{"def_can_2", (""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_VIRTUAL };
    auto caninfo0_3 = Caninfo{"def_can_3", (""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_VIRTUAL };

    auto caninfo1_0 = Caninfo{ "def_can_0", (""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_VIRTUAL };
    auto caninfo1_1 = Caninfo{ "def_can_1", (""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_VIRTUAL };
    auto caninfo1_2 = Caninfo{ "def_can_2", (""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_VIRTUAL };
    auto caninfo1_3 = Caninfo{ "def_can_3", (""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_VIRTUAL };

    AMSinfo                        ams1 = AMSinfo{"0", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}, 0};
    AMSinfo                        ams2 = AMSinfo{"1", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}, 0 };
    AMSinfo                        ams3 = AMSinfo{"2", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}, 0 };
    AMSinfo                        ams4 = AMSinfo{"3", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}, 0 };

    AMSinfo                        ams5 = AMSinfo{ "4", std::vector<Caninfo>{caninfo1_0, caninfo1_1, caninfo1_2, caninfo1_3}, 1 };
    AMSinfo                        ams6 = AMSinfo{ "5", std::vector<Caninfo>{caninfo1_0, caninfo1_1, caninfo1_2, caninfo1_3}, 1 };
    AMSinfo                        ams7 = AMSinfo{ "6", std::vector<Caninfo>{caninfo1_0, caninfo1_1, caninfo1_2, caninfo1_3}, 1 };
    AMSinfo                        ams8 = AMSinfo{ "7", std::vector<Caninfo>{caninfo1_0, caninfo1_1, caninfo1_2, caninfo1_3}, 1 };
    std::vector<AMSinfo>           ams_info{ams1, ams2, ams3, ams4, ams5, ams6, ams7, ams8 };
    std::vector<AMSinfo>::iterator it;
    //Freeze();
    for (it = ams_info.begin(); it != ams_info.end(); it++) {
        AddAmsPreview(*it, AMSModel::GENERIC_AMS);
        AddAms(*it);
        //AddExtraAms(*it);
        m_ams_info.push_back(*it);
    }
    if (m_single_nozzle_no_ams)
    {
        m_simplebook_ams_left->Hide();
    }
    else {
        m_sizer_prv_left->Layout();
        m_sizer_prv_right->Layout();
    }
    //Thaw();
}


void AMSControl::ClearAms() {
    m_simplebook_ams_right->DeleteAllPages();
    m_simplebook_ams_left->DeleteAllPages();
    m_simplebook_ams_right->DestroyChildren();
    m_simplebook_ams_left->DestroyChildren();
    m_simplebook_ams_right->Layout();
    m_simplebook_ams_left->Layout();
    m_simplebook_ams_right->Refresh();
    m_simplebook_ams_left->Refresh();

    for (auto it : m_ams_preview_list) {
        delete it.second;
    }
    m_ams_preview_list.clear();
    m_ext_image_list.clear();

    m_left_page_index = 0;
    m_right_page_index = 0;

    m_ams_item_list.clear();
    m_sizer_prv_right->Clear();
    m_sizer_prv_left->Clear();
    m_item_ids = { {}, {} };
    pair_id.clear();
}

void AMSControl::CreateAmsDoubleNozzle()
{
    std::vector<AMSinfo> single_info_left;
    std::vector<AMSinfo> single_info_right;

    //Freeze();
    for (auto ams_info = m_ams_info.begin(); ams_info != m_ams_info.end(); ams_info++){
        if (ams_info->cans.size() == GENERIC_AMS_SLOT_NUM){
            ams_info->nozzle_id == MAIN_NOZZLE_ID ? m_item_ids[MAIN_NOZZLE_ID].push_back(ams_info->ams_id) : m_item_ids[DEPUTY_NOZZLE_ID].push_back(ams_info->ams_id);
            AddAmsPreview(*ams_info, ams_info->ams_type);
            AddAms(*ams_info);
        }
        else if (ams_info->cans.size() == 1){

            if (ams_info->nozzle_id == MAIN_NOZZLE_ID){
                single_info_right.push_back(*ams_info);
                if (single_info_right.size() == 2){
                    single_info_right[0].nozzle_id == MAIN_NOZZLE_ID ? m_item_ids[MAIN_NOZZLE_ID].push_back(single_info_right[0].ams_id) : m_item_ids[DEPUTY_NOZZLE_ID].push_back(single_info_right[0].ams_id);
                    single_info_right[1].nozzle_id == MAIN_NOZZLE_ID ? m_item_ids[MAIN_NOZZLE_ID].push_back(single_info_right[1].ams_id) : m_item_ids[DEPUTY_NOZZLE_ID].push_back(single_info_right[1].ams_id);
                    AddAms(single_info_right);
                    AddAmsPreview(single_info_right, AMSPanelPos::RIGHT_PANEL);
                    pair_id.push_back(std::make_pair(single_info_right[0].ams_id, single_info_right[1].ams_id));
                    single_info_right.clear();
                }
            }
            else if (ams_info->nozzle_id == 1){
                single_info_left.push_back(*ams_info);
                if (single_info_left.size() == 2){
                    single_info_left[0].nozzle_id == MAIN_NOZZLE_ID ? m_item_ids[MAIN_NOZZLE_ID].push_back(single_info_left[0].ams_id) : m_item_ids[DEPUTY_NOZZLE_ID].push_back(single_info_left[0].ams_id);
                    single_info_left[1].nozzle_id == MAIN_NOZZLE_ID ? m_item_ids[MAIN_NOZZLE_ID].push_back(single_info_left[1].ams_id) : m_item_ids[DEPUTY_NOZZLE_ID].push_back(single_info_left[1].ams_id);
                    AddAms(single_info_left);
                    AddAmsPreview(single_info_left, AMSPanelPos::LEFT_PANEL);
                    pair_id.push_back(std::make_pair(single_info_left[0].ams_id, single_info_left[1].ams_id));
                    single_info_left.clear();
                }
            }
        }
    }
    if (m_ext_info.size() <= 1) {
        BOOST_LOG_TRIVIAL(trace) << "vt_slot empty!";
        assert(0);
        return;
    }
    AMSinfo ext_info;
    for (auto info : m_ext_info){
        if (info.ams_id == std::to_string(VIRTUAL_TRAY_MAIN_ID)){
            ext_info = info;
            single_info_right.push_back(ext_info);
            break;
        }
    }
    //wait add


    single_info_right[0].nozzle_id == MAIN_NOZZLE_ID ? m_item_ids[MAIN_NOZZLE_ID].push_back(single_info_right[0].ams_id) : m_item_ids[DEPUTY_NOZZLE_ID].push_back(single_info_right[0].ams_id);
    if (single_info_right.size() == 2){
        single_info_right[1].nozzle_id == MAIN_NOZZLE_ID ? m_item_ids[MAIN_NOZZLE_ID].push_back(single_info_right[1].ams_id) : m_item_ids[DEPUTY_NOZZLE_ID].push_back(single_info_right[1].ams_id);
        pair_id.push_back(std::make_pair(single_info_right[0].ams_id, single_info_right[1].ams_id));
    }
    AddAms(single_info_right);
    AddAmsPreview(single_info_right, AMSPanelPos::RIGHT_PANEL);
    single_info_right.clear();

    for (auto info : m_ext_info) {
        if (info.ams_id == std::to_string(VIRTUAL_TRAY_DEPUTY_ID)) {
            ext_info = info;
            single_info_left.push_back(ext_info);
            break;
        }
    }
    //wait add
    single_info_left[0].nozzle_id == MAIN_NOZZLE_ID ? m_item_ids[MAIN_NOZZLE_ID].push_back(single_info_left[0].ams_id) : m_item_ids[DEPUTY_NOZZLE_ID].push_back(single_info_left[0].ams_id);
    if (single_info_left.size() == 2){
        single_info_left[1].nozzle_id == MAIN_NOZZLE_ID ? m_item_ids[MAIN_NOZZLE_ID].push_back(single_info_left[1].ams_id) : m_item_ids[DEPUTY_NOZZLE_ID].push_back(single_info_left[1].ams_id);
        pair_id.push_back(std::make_pair(single_info_left[0].ams_id, single_info_left[1].ams_id));
    }
    AddAmsPreview(single_info_left, AMSPanelPos::LEFT_PANEL);
    AddAms(single_info_left);
    single_info_left.clear();

    m_sizer_prv_left->Layout();
    m_sizer_prv_right->Layout();
    m_simplebook_ams_left->Show();
    m_simplebook_ams_right->Show();
    if (m_ams_info.size() > 0){
        m_panel_prv_left->Show();
        m_panel_prv_right->Show();
    }
    else{
        m_panel_prv_left->Hide();
        m_panel_prv_right->Hide();
    }
    m_simplebook_ams_left->SetSelection(0);
    m_simplebook_ams_right->SetSelection(0);

    auto left_init_mode = findFirstMode(AMSPanelPos::LEFT_PANEL);
    auto right_init_mode = findFirstMode(AMSPanelPos::RIGHT_PANEL);


    m_down_road->UpdateLeft(m_extder_data.total_extder_count, left_init_mode);
    m_down_road->UpdateRight(m_extder_data.total_extder_count, right_init_mode);

    m_extruder->updateNozzleNum(m_extder_data.total_extder_count);

    m_current_show_ams_left = m_item_ids[DEPUTY_NOZZLE_ID].size() > 0 ? m_item_ids[DEPUTY_NOZZLE_ID][0] : "";
    m_current_show_ams_right = m_item_ids[MAIN_NOZZLE_ID].size() > 0 ? m_item_ids[MAIN_NOZZLE_ID][0] : "";

    m_current_ams = "";
    m_down_road->UpdatePassRoad(AMSPanelPos::LEFT_PANEL, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    m_extruder->OnAmsLoading(false, DEPUTY_NOZZLE_ID);
    m_down_road->UpdatePassRoad(AMSPanelPos::RIGHT_PANEL, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    m_extruder->OnAmsLoading(false, MAIN_NOZZLE_ID);

    m_amswin->Layout();
    m_amswin->Fit();

    //Thaw();
}

void AMSControl::CreateAmsSingleNozzle()
{
    std::vector<int>m_item_nums{0,0};
    std::vector<AMSinfo> single_info;

    //Freeze();

    //add ams data
    for (auto ams_info = m_ams_info.begin(); ams_info != m_ams_info.end(); ams_info++) {
        if (ams_info->cans.size() == GENERIC_AMS_SLOT_NUM) {
            m_item_ids[DEPUTY_NOZZLE_ID].push_back(ams_info->ams_id);
            AddAmsPreview(*ams_info, ams_info->ams_type);
            AddAms(*ams_info, AMSPanelPos::LEFT_PANEL);
            //AddExtraAms(*ams_info);
        }
        else if (ams_info->cans.size() == 1) {
            single_info.push_back(*ams_info);
            if (single_info.size() == MAX_AMS_NUM_IN_PANEL) {
                m_item_ids[DEPUTY_NOZZLE_ID].push_back(single_info[0].ams_id);
                m_item_ids[DEPUTY_NOZZLE_ID].push_back(single_info[1].ams_id);
                m_item_nums[DEPUTY_NOZZLE_ID]++;
                pair_id.push_back(std::make_pair(single_info[0].ams_id, single_info[1].ams_id));
                AddAmsPreview(single_info, AMSPanelPos::LEFT_PANEL);
                AddAms(single_info, AMSPanelPos::LEFT_PANEL);
                single_info.clear();
            }
        }
    }
    if (single_info.size() > 0){
        m_item_ids[DEPUTY_NOZZLE_ID].push_back(single_info[0].ams_id);
        m_item_nums[DEPUTY_NOZZLE_ID]++;
        AddAms(single_info, AMSPanelPos::LEFT_PANEL);
        AddAmsPreview(single_info, AMSPanelPos::LEFT_PANEL);
        single_info.clear();
    }

    // data ext data
    if (m_ext_info.size() <= 0){
        BOOST_LOG_TRIVIAL(trace) << "vt_slot empty!";
        return;
    }

    single_info.push_back(m_ext_info[0]);
    m_item_ids[MAIN_NOZZLE_ID].push_back(single_info[0].ams_id);
    AddAms(single_info, AMSPanelPos::RIGHT_PANEL);
    auto left_init_mode = findFirstMode(AMSPanelPos::LEFT_PANEL);
    auto right_init_mode = findFirstMode(AMSPanelPos::RIGHT_PANEL);

    m_panel_prv_right->Hide();
    m_panel_prv_left->Hide();
    if (m_ams_info.size() > 0){
        m_simplebook_ams_left->Show();
        m_simplebook_ams_right->Show();
        m_simplebook_ams_left->SetSelection(0);
        m_simplebook_ams_right->SetSelection(0);

        if (m_ams_info.size() > 1){
            m_sizer_prv_right->Layout();
            m_panel_prv_right->Show();
        }
        m_down_road->UpdateLeft(1, left_init_mode);
        m_down_road->UpdateRight(1, right_init_mode);
    }
    else {
        m_panel_prv_left->Hide();
        m_panel_prv_right->Hide();
        m_simplebook_ams_left->Hide();
        m_simplebook_ams_right->Show();

        m_simplebook_ams_right->SetSelection(0);
        m_down_road->UpdateLeft(1, left_init_mode);
        m_down_road->UpdateRight(1, right_init_mode);
    }
    m_current_show_ams_left = m_item_ids[DEPUTY_NOZZLE_ID].size() > 0 ? m_item_ids[DEPUTY_NOZZLE_ID][0] : "";
    m_current_show_ams_right = m_item_ids[MAIN_NOZZLE_ID].size() > 0 ? m_item_ids[MAIN_NOZZLE_ID][0] : "";
    m_current_ams = "";

    m_down_road->UpdatePassRoad(AMSPanelPos::LEFT_PANEL, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    m_down_road->UpdatePassRoad(AMSPanelPos::RIGHT_PANEL, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    m_extruder->updateNozzleNum(1);
    m_extruder->OnAmsLoading(false, MAIN_NOZZLE_ID);

    m_amswin->Layout();
    m_amswin->Fit();

    //Refresh();
    //Thaw();
}

void AMSControl::Reset()
{
    /*auto caninfo0_0 = Caninfo{"0", "", *wxWHITE, AMSCanType::AMS_CAN_TYPE_NONE};
    auto caninfo0_1 = Caninfo{"1", "", *wxWHITE, AMSCanType::AMS_CAN_TYPE_NONE};
    auto caninfo0_2 = Caninfo{"2", "", *wxWHITE, AMSCanType::AMS_CAN_TYPE_NONE};
    auto caninfo0_3 = Caninfo{"3", "", *wxWHITE, AMSCanType::AMS_CAN_TYPE_NONE};

    AMSinfo ams1 = AMSinfo{"0", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}};
    AMSinfo ams2 = AMSinfo{"1", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}};
    AMSinfo ams3 = AMSinfo{"2", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}};
    AMSinfo ams4 = AMSinfo{"3", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}};

    std::vector<AMSinfo>           ams_info{ams1, ams2, ams3, ams4};
    std::vector<AMSinfo>::iterator it;*/

    /*Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (dev) {
        MachineObject* obj = dev->get_selected_machine();
        parse_object(obj);
    }

    UpdateAms(m_ams_info, true);
    m_current_show_ams  = "";
    m_current_ams       = "";
    m_current_select    = "";*/

    m_ams_info.clear();
    m_ext_info.clear();
    m_dev_id.clear();
    ClearAms();

    Layout();
}

void AMSControl::show_noams_mode()
{
    EnterGenericAMSMode();
}

void AMSControl::show_auto_refill(bool show)
{
    if (m_button_auto_refill->IsShown() == show)
    {
        return;
    }

    m_button_auto_refill->Show(show);
    m_amswin->Layout();
    m_amswin->Fit();
}

void AMSControl::enable_ams_setting(bool en)
{
    m_button_ams_setting->Enable(en);
}

void AMSControl::show_vams_kn_value(bool show)
{
    //m_vams_lib->show_kn_value(show);
}

std::vector<AMSinfo> AMSControl::GenerateSimulateData() {
    auto caninfo0_0 = Caninfo{ "0", (""), *wxRED, AMSCanType::AMS_CAN_TYPE_VIRTUAL };
    auto caninfo0_1 = Caninfo{ "1", (""), *wxGREEN, AMSCanType::AMS_CAN_TYPE_VIRTUAL };
    auto caninfo0_2 = Caninfo{ "2", (""), *wxBLUE, AMSCanType::AMS_CAN_TYPE_VIRTUAL };
    auto caninfo0_3 = Caninfo{ "3", (""), *wxYELLOW, AMSCanType::AMS_CAN_TYPE_VIRTUAL };

    auto caninfo1_0 = Caninfo{ "0", (""), wxColour(255, 255, 0), AMSCanType::AMS_CAN_TYPE_VIRTUAL };
    auto caninfo1_1 = Caninfo{ "1", (""), wxColour(255, 0, 255), AMSCanType::AMS_CAN_TYPE_VIRTUAL };
    auto caninfo1_2 = Caninfo{ "2", (""), wxColour(0, 255, 255), AMSCanType::AMS_CAN_TYPE_VIRTUAL };
    auto caninfo1_3 = Caninfo{ "3", (""), wxColour(200, 80, 150), AMSCanType::AMS_CAN_TYPE_VIRTUAL };

    AMSinfo                        ams1 = AMSinfo{ "0", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}, 0 };
    AMSinfo                        ams2 = AMSinfo{ "1", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}, 0 };
    AMSinfo                        ams3 = AMSinfo{ "2", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}, 0 };
    AMSinfo                        ams4 = AMSinfo{ "3", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}, 0 };

    AMSinfo                        singleams1 = AMSinfo{ "0", std::vector<Caninfo>{caninfo0_0}, 0 };
    AMSinfo                        singleams2 = AMSinfo{ "1", std::vector<Caninfo>{caninfo0_0}, 0 };
    AMSinfo                        singleams3 = AMSinfo{ "2", std::vector<Caninfo>{caninfo0_0}, 0 };
    AMSinfo                        singleams4 = AMSinfo{ "3", std::vector<Caninfo>{caninfo0_0}, 0 };
    singleams1.ams_type = AMSModel::N3S_AMS;
    singleams2.ams_type = AMSModel::N3S_AMS;
    singleams3.ams_type = AMSModel::N3S_AMS;
    singleams4.ams_type = AMSModel::N3S_AMS;

    AMSinfo                        ams5 = AMSinfo{ "4", std::vector<Caninfo>{caninfo1_0, caninfo1_1, caninfo1_2, caninfo1_3}, 1 };
    AMSinfo                        ams6 = AMSinfo{ "5", std::vector<Caninfo>{caninfo1_0, caninfo1_1, caninfo1_2, caninfo1_3}, 1 };
    AMSinfo                        ams7 = AMSinfo{ "6", std::vector<Caninfo>{caninfo1_0, caninfo1_1, caninfo1_2, caninfo1_3}, 1 };
    AMSinfo                        ams8 = AMSinfo{ "7", std::vector<Caninfo>{caninfo1_0, caninfo1_1, caninfo1_2, caninfo1_3}, 1 };

    AMSinfo                        singleams5 = AMSinfo{ "4", std::vector<Caninfo>{caninfo1_0}, 1 };
    AMSinfo                        singleams6 = AMSinfo{ "5", std::vector<Caninfo>{caninfo1_0}, 1 };
    AMSinfo                        singleams7 = AMSinfo{ "6", std::vector<Caninfo>{caninfo1_0}, 1 };
    AMSinfo                        singleams8 = AMSinfo{ "7", std::vector<Caninfo>{caninfo1_0}, 1 };
    AMSinfo                        singleams9 = AMSinfo{ "8", std::vector<Caninfo>{caninfo1_0}, 1 };
    singleams5.ams_type = AMSModel::N3S_AMS;
    singleams6.ams_type = AMSModel::N3S_AMS;
    singleams7.ams_type = AMSModel::N3S_AMS;
    singleams8.ams_type = AMSModel::N3S_AMS;
    singleams9.ams_type = AMSModel::N3S_AMS;

    ams3.current_can_id = "2";
    ams3.current_step = AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2;
    ams5.current_can_id = "2";
    ams5.current_step = AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2;
    std::vector<AMSinfo>generic_ams = { ams1, ams2, ams3, ams4, ams5, ams6, ams7, ams8 };
    std::vector<AMSinfo>single_ams = { singleams1, singleams2, singleams3, singleams4, singleams5, singleams6, singleams7, singleams8, singleams9 };
    std::vector<AMSinfo>ams_info = { ams1, singleams2, ams3, singleams4, ams5, singleams6, ams7, singleams8, singleams9 };
    return ams_info;
}


void AMSControl::UpdateAms(const std::string& series_name, std::vector<AMSinfo> ams_info, std::vector<AMSinfo>ext_info, ExtderData data, std::string dev_id, bool is_reset, bool test)
{
    if (!test){
        // update item
        bool fresh = false;

        // basic check
        if (m_ams_info.size() == ams_info.size() && m_extder_data.total_extder_count == data.total_extder_count && m_dev_id == dev_id && m_ext_info.size() == ext_info.size()) {
            for (int i = 0; i < m_ams_info.size(); i++){
                if (m_ams_info[i].ams_id != ams_info[i].ams_id){
                    fresh = true;
                }

                if (m_ams_info[i].nozzle_id != ams_info[i].nozzle_id) {
                    fresh = true;
                }
            }
        }
        else{
            fresh = true;
        }

        m_ams_info.clear();
        m_ams_info = ams_info;
        m_ext_info.clear();
        m_ext_info = ext_info;
        m_extder_data = data;
        m_dev_id = dev_id;
        if (fresh){
            ClearAms();
            if (m_extder_data.total_extder_count >= 2){
                CreateAmsDoubleNozzle();
            }else{
                CreateAmsSingleNozzle();
            }
            SetSize(wxSize(FromDIP(578), -1));
            SetMinSize(wxSize(FromDIP(578), -1));
            Layout();
        }
        // update cans

        for (auto ams_item : m_ams_item_list) {
            if (ams_item.second == nullptr){
                continue;
            }
            std::string ams_id = ams_item.second->get_ams_id();
            AmsItem* cans = ams_item.second;
            if (cans->get_ams_id() == std::to_string(VIRTUAL_TRAY_MAIN_ID) || cans->get_ams_id() == std::to_string(VIRTUAL_TRAY_DEPUTY_ID)) {
                for (auto ifo : m_ext_info) {
                    if (ifo.ams_id == ams_id) {
                        cans->Update(ifo);
                        cans->show_sn_value(m_ams_model == AMSModel::AMS_LITE ? false : true);
                    }
                }
            }
            else{
                for (auto ifo : m_ams_info) {
                    if (ifo.ams_id == ams_id) {
                        cans->Update(ifo);
                        cans->show_sn_value(m_ams_model == AMSModel::AMS_LITE ? false : true);
                    }
                }
            }
        }

        for (auto ams_prv : m_ams_preview_list) {
            std::string id = ams_prv.second->get_ams_id();
            auto item = m_ams_item_list.find(id);
            if (item != m_ams_item_list.end())
            { ams_prv.second->Update(item->second->get_ams_info());
            }
        }

        /*if (m_current_show_ams.empty() && !is_reset) {
            if (ext_info.size() > 0) {
                SwitchAms(ext_info[0].ams_id);
            }
        }*/

        //m_simplebook_ams_left->SetSelection(m_simplebook_ams_left->m_first);
    }
    else
    {
        static bool first_time = true;
        bool fresh = false;
        static std::vector<AMSinfo>ams_info;
        int nozzle_num = 2;
        if (first_time){
            ams_info = GenerateSimulateData();
            fresh = true;
            first_time = false;
        }

        //Freeze();

        // update item
        m_ams_info.clear();
        m_ams_info = ams_info;
        m_ext_info.clear();
        m_ext_info.push_back(ext_info[0]);
        m_ext_info.push_back(ext_info[0]);
        m_ext_info[0].ams_id = std::to_string(VIRTUAL_TRAY_MAIN_ID);
        m_ext_info[0].nozzle_id = MAIN_NOZZLE_ID;
        m_ext_info[1].ams_id = std::to_string(VIRTUAL_TRAY_DEPUTY_ID);
        m_ext_info[1].nozzle_id = DEPUTY_NOZZLE_ID;
        m_extder_data = data;
        if (fresh){
            ClearAms();
            if (m_extder_data.total_extder_count >= 2) {
                CreateAmsDoubleNozzle();
            }
            else {
                CreateAmsSingleNozzle();
            }
            SetSize(wxSize(FromDIP(578), -1));
            SetMinSize(wxSize(FromDIP(578), -1));
            Layout();
        }
        //Thaw();

        // update cans

        for (auto ams_item : m_ams_item_list) {
            std::string ams_id = ams_item.first;
            AmsItem* cans = ams_item.second;
            if (atoi(cans->get_ams_id().c_str()) >= VIRTUAL_TRAY_DEPUTY_ID) {
                for (auto ifo : m_ext_info) {
                    if (ifo.ams_id == ams_id) {
                        cans->Update(ifo);
                        cans->show_sn_value(m_ams_model == AMSModel::AMS_LITE ? false : true);
                    }
                }
            }
            else {
                for (auto ifo : m_ams_info) {
                    if (ifo.ams_id == ams_id) {
                        cans->Update(ifo);
                        cans->show_sn_value(m_ams_model == AMSModel::AMS_LITE ? false : true);
                    }
                }
            }
        }

        /*if (m_current_show_ams.empty() && !is_reset) {
            if (ams_info.size() > 0) {
                SwitchAms(ams_info[0].ams_id);
            }
        }*/
    }

    /*update humidity popup*/
    if (m_percent_humidity_dry_popup->IsShown())
    {
        string target_id = m_percent_humidity_dry_popup->get_owner_ams_id();
        for (const auto& the_info : ams_info)
        {
            if (target_id == the_info.ams_id)
            {
                uiAmsHumidityInfo humidity_info;
                humidity_info.ams_id = the_info.ams_id;
                humidity_info.humidity_level = the_info.ams_humidity;
                humidity_info.humidity_percent = the_info.humidity_raw;
                humidity_info.left_dry_time = the_info.left_dray_time;
                humidity_info.current_temperature = the_info.current_temperature;
                m_percent_humidity_dry_popup->Update(&humidity_info);
                break;
            }
        }
    }

    /*update ams extruder*/
    if (m_extruder->updateNozzleNum(m_extder_data.total_extder_count, series_name))
    {
        m_amswin->Layout();
    }
}

void AMSControl::AddAmsPreview(AMSinfo info, AMSModel type)
{
    AMSPreview *ams_prv = nullptr;

    if (info.nozzle_id == MAIN_NOZZLE_ID)
    {
        ams_prv = new AMSPreview(m_panel_prv_right, wxID_ANY, info, type);
        m_sizer_prv_right->Add(ams_prv, 0, wxALIGN_CENTER | wxLEFT, FromDIP(6));
    }
    else if (info.nozzle_id == DEPUTY_NOZZLE_ID)
    {
        ams_prv = new AMSPreview(m_panel_prv_left, wxID_ANY, info, type);
        m_sizer_prv_left->Add(ams_prv, 0, wxALIGN_CENTER | wxLEFT, FromDIP(6));
    }

    if (ams_prv){
        ams_prv->Bind(wxEVT_LEFT_DOWN, [this, ams_prv](wxMouseEvent &e) {
            SwitchAms(ams_prv->get_ams_id());
            e.Skip();
        });
        m_ams_preview_list[info.ams_id] = ams_prv;
    }
}

void AMSControl::createAms(wxSimplebook* parent, int& idx, AMSinfo info, AMSPanelPos pos) {
    auto ams_item = new AmsItem(parent, info, info.ams_type, pos);
    parent->InsertPage(idx, ams_item, wxEmptyString, true);
    ams_item->set_selection(idx);
    idx++;

    m_ams_item_list[info.ams_id] = ams_item;
}

AMSRoadShowMode AMSControl::findFirstMode(AMSPanelPos pos) {
    auto init_mode = AMSRoadShowMode::AMS_ROAD_MODE_NONE;
    std::string ams_id = "";
    if (pos == AMSPanelPos::LEFT_PANEL && m_item_ids[DEPUTY_NOZZLE_ID].size() > 0){
        ams_id = m_item_ids[DEPUTY_NOZZLE_ID][0];
    }
    else if (pos == AMSPanelPos::RIGHT_PANEL && m_item_ids[MAIN_NOZZLE_ID].size() > 0){
        ams_id = m_item_ids[MAIN_NOZZLE_ID][0];
    }

    auto item = m_ams_item_list.find(ams_id);
    if (ams_id.empty() || item == m_ams_item_list.end()) return init_mode;

    if (item->second->get_can_count() == GENERIC_AMS_SLOT_NUM) {
        if (item->second->get_ams_model() == AMSModel::AMS_LITE) return AMSRoadShowMode::AMS_ROAD_MODE_AMS_LITE;
        if (item->second->get_ams_model() == AMSModel::EXT_AMS && item->second->get_ext_type() == AMSModelOriginType::LITE_EXT) return AMSRoadShowMode::AMS_ROAD_MODE_AMS_LITE;
        return AMSRoadShowMode::AMS_ROAD_MODE_FOUR;
    }
    else{
        for (auto ids : pair_id){
            if (ids.first == ams_id || ids.second == ams_id){
                return AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
            }
        }
        if (item->second->get_ams_model() == AMSModel::EXT_AMS && item->second->get_ext_type() == AMSModelOriginType::LITE_EXT) return AMSRoadShowMode::AMS_ROAD_MODE_AMS_LITE;
        return AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
    }
}

void AMSControl::createAmsPanel(wxSimplebook *parent, int &idx, std::vector<AMSinfo> infos, AMSPanelPos pos, int total_ext_num)
{
    if (infos.size() <= 0) return;

    wxPanel* book_panel = new wxPanel(parent);
    wxBoxSizer* book_sizer = new wxBoxSizer(wxHORIZONTAL);
    book_panel->SetBackgroundColour(StateColor::darkModeColorFor(AMS_CONTROL_DEF_LIB_BK_COLOUR));
    book_panel->SetSize(AMS_PANEL_SIZE);
    book_panel->SetMinSize(AMS_PANEL_SIZE);

    AmsItem* ams1 = nullptr, * ams2 = nullptr;
    ams1 = new AmsItem(book_panel, infos[0], infos[0].ams_type, pos);
    if (ams1->get_ext_image()) { ams1->get_ext_image()->setTotalExtNum(total_ext_num); }

    if (infos.size() == MAX_AMS_NUM_IN_PANEL) {    //n3s and ? in a panel
        ams2 = new AmsItem(book_panel, infos[1], infos[1].ams_type, pos);
        if (ams2->get_ext_image()) { ams2->get_ext_image()->setTotalExtNum(total_ext_num); }

        if (pos == AMSPanelPos::LEFT_PANEL) {
            book_sizer->Add(ams1, 0, wxLEFT, FromDIP(4));
            book_sizer->Add(ams2, 0, wxLEFT, FromDIP(30));
        }
        else {
            book_sizer->Add(ams1, 0, wxLEFT, FromDIP(72));
            book_sizer->Add(ams2, 0, wxLEFT, FromDIP(30));
        }
    }
    else {   //only an ext in a panel
        if (ams1->get_ext_image()) { ams1->get_ext_image()->setShowAmsExt(false);}

        if (ams1->get_ams_model() == AMSModel::EXT_AMS) {
            if (ams1->get_ext_type() == LITE_EXT) {
                //book_sizer->Add(ams1, 0, wxALIGN_CENTER_HORIZONTAL, 0);
                book_sizer->Add(ams1, 0, wxLEFT, (book_panel->GetSize().x - ams1->GetSize().x) / 2);
            }
            else{
                auto ext_image = new AMSExtImage(book_panel, pos, &m_extder_data);
                book_sizer->Add(ams1, 0, wxLEFT, FromDIP(30));
                book_sizer->Add(ext_image, 0, wxEXPAND | wxLEFT | wxALIGN_CENTER_VERTICAL, FromDIP(30));
                m_ext_image_list[infos[0].ams_id] = ext_image;
            }
        }
    }

    book_panel->SetSizer(book_sizer);
    book_panel->Layout();
    book_panel->Fit();

    parent->InsertPage(idx, book_panel, wxEmptyString, true);
    ams1->SetBackgroundColour(StateColor::darkModeColorFor(AMS_CONTROL_DEF_LIB_BK_COLOUR));
    ams1->set_selection(idx);
    m_ams_item_list[infos[0].ams_id] = ams1;
    if (ams2) {
        ams2->SetBackgroundColour(StateColor::darkModeColorFor(AMS_CONTROL_DEF_LIB_BK_COLOUR));
        ams2->set_selection(idx);
        m_ams_item_list[infos[1].ams_id] = ams2;
    }
    idx++;
}

void AMSControl::AddAms(AMSinfo info, AMSPanelPos pos)
{
    if (m_extder_data.total_extder_count > 1){
        if (info.nozzle_id == MAIN_NOZZLE_ID){
            createAms(m_simplebook_ams_right, m_right_page_index, info, AMSPanelPos::RIGHT_PANEL);
        }
        else if (info.nozzle_id == DEPUTY_NOZZLE_ID){
            createAms(m_simplebook_ams_left, m_left_page_index, info, AMSPanelPos::LEFT_PANEL);
        }
    }
    else if (m_extder_data.total_extder_count == 1){
        createAms(m_simplebook_ams_left, m_left_page_index, info, AMSPanelPos::LEFT_PANEL);
    }
    m_simplebook_ams_left->Layout();
    m_simplebook_ams_right->Layout();
    m_simplebook_ams_left->Refresh();
    m_simplebook_ams_right->Refresh();

}

//void AMSControl::AddExtraAms(AMSinfo info)
//{
//    auto ams_item = new AmsItem(m_simplebook_extra_cans_left, info, AMSModel::EXTRA_AMS);
//    m_ams_item_list[info.ams_id] = ams_item;
//
//    if (info.nozzle_id == 1)
//    {
//        m_simplebook_extra_cans_left->AddPage(ams_item, wxEmptyString, false);
//        ams_item->m_selection = m_simplebook_extra_cans_left->GetPageCount() - 1;
//    }
//    else if (info.nozzle_id == 0)
//    {
//        m_simplebook_extra_cans_right->AddPage(ams_item, wxEmptyString, false);
//        ams_item->m_selection = m_simplebook_extra_cans_right->GetPageCount() - 1;
//    }
//
//}

void AMSControl::AddAms(std::vector<AMSinfo>single_info, AMSPanelPos pos) {
     if (single_info.size() <= 0){
        return;
    }
    if (m_extder_data.total_extder_count == 2) {
        if (single_info[0].nozzle_id == MAIN_NOZZLE_ID) {
            createAmsPanel(m_simplebook_ams_right, m_right_page_index, single_info, AMSPanelPos::RIGHT_PANEL, m_extder_data.total_extder_count);
        }
        else if (single_info[0].nozzle_id == DEPUTY_NOZZLE_ID) {
            createAmsPanel(m_simplebook_ams_left, m_left_page_index, single_info, AMSPanelPos::LEFT_PANEL, m_extder_data.total_extder_count);
        }
    }
    else if (m_extder_data.total_extder_count == 1) {
        if (pos == AMSPanelPos::RIGHT_PANEL) {
            createAmsPanel(m_simplebook_ams_right, m_right_page_index, single_info, AMSPanelPos::RIGHT_PANEL, m_extder_data.total_extder_count);
        }
        else {
            createAmsPanel(m_simplebook_ams_left, m_left_page_index, single_info, AMSPanelPos::LEFT_PANEL, m_extder_data.total_extder_count);
        }
    }

    m_simplebook_ams_left->Layout();
    m_simplebook_ams_right->Layout();
    m_simplebook_ams_left->Refresh();
    m_simplebook_ams_right->Refresh();
}

//void AMSControl::AddExtAms(int ams_id) {
//    if (m_ams_item_list.find(std::to_string(ams_id)) != m_ams_item_list.end())
//    {
//        //mode = AMSModel::EXTRA_AMS;
//        AmsItem* ams_item;
//        AMSinfo ext_info;
//
//        if (ams_id == VIRTUAL_TRAY_MAIN_ID)
//        {
//            ext_info.ams_id = std::to_string(VIRTUAL_TRAY_MAIN_ID);
//            ext_info.nozzle_id = 0;
//            ams_item = new AmsItem(m_simplebook_ams_right, ext_info, AMSModel::EXTRA_AMS);
//            m_simplebook_ams_right->AddPage(ams_item, wxEmptyString, false);
//            ams_item->m_selection = m_simplebook_ams_right->GetPageCount() - 1;
//        }
//        else if (ams_id == VIRTUAL_TRAY_DEPUTY_ID)
//        {
//            ext_info.ams_id = std::to_string(VIRTUAL_TRAY_DEPUTY_ID);
//            ext_info.nozzle_id = 1;
//            ams_item = new AmsItem(m_simplebook_ams_left, ext_info, AMSModel::EXTRA_AMS);
//            m_simplebook_ams_left->AddPage(ams_item, wxEmptyString, false);
//            ams_item->m_selection = m_simplebook_ams_left->GetPageCount() - 1;
//        }
//        m_ams_generic_item_list[std::to_string(ams_id)] = ams_item;
//    }
//}

void AMSControl::AddAmsPreview(std::vector<AMSinfo>single_info, AMSPanelPos pos) {
    if (single_info.size() <= 0) return;

    AMSPreview* ams_prv = nullptr;
    AMSPreview* ams_prv2 = nullptr;
    if (pos == AMSPanelPos::RIGHT_PANEL){
        ams_prv = new AMSPreview(m_panel_prv_right, wxID_ANY, single_info[0], single_info[0].ams_type);
        m_sizer_prv_right->Add(ams_prv, 0, wxALIGN_CENTER | wxLEFT, FromDIP(6));
        if (single_info.size() == 2)
        {
            ams_prv2 = new AMSPreview(m_panel_prv_right, wxID_ANY, single_info[1], single_info[1].ams_type);
            m_sizer_prv_right->Add(ams_prv2, 0, wxALIGN_CENTER | wxLEFT, 0);
        }
    }
    else
    {
        ams_prv = new AMSPreview(m_panel_prv_left, wxID_ANY, single_info[0], single_info[0].ams_type);
        m_sizer_prv_left->Add(ams_prv, 0, wxALIGN_CENTER | wxLEFT, FromDIP(6));
        if (single_info.size() == 2)
        {
            ams_prv2 = new AMSPreview(m_panel_prv_left, wxID_ANY, single_info[1], single_info[1].ams_type);
            m_sizer_prv_left->Add(ams_prv2, 0, wxALIGN_CENTER | wxLEFT, 0);
        }
    }

    if (ams_prv) {
        ams_prv->Bind(wxEVT_LEFT_DOWN, [this, ams_prv](wxMouseEvent& e) {
            SwitchAms(ams_prv->get_ams_id());
            e.Skip();
            });
        m_ams_preview_list[single_info[0].ams_id] = ams_prv;
    }
    if (ams_prv2) {
        ams_prv2->Bind(wxEVT_LEFT_DOWN, [this, ams_prv2](wxMouseEvent& e) {
            SwitchAms(ams_prv2->get_ams_id());
            e.Skip();
            });
        m_ams_preview_list[single_info[1].ams_id] = ams_prv2;
    }
}

void AMSControl::SwitchAms(std::string ams_id)
{
    if(ams_id == m_current_show_ams_left || ams_id == m_current_show_ams_right){return;}

    bool is_in_right = IsAmsInRightPanel(ams_id);
    if (is_in_right){
        m_current_show_ams_right = ams_id;
        m_down_road->UpdatePassRoad(AMSPanelPos::RIGHT_PANEL, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    }
    else{
        m_current_show_ams_left = ams_id;
        m_down_road->UpdatePassRoad(AMSPanelPos::LEFT_PANEL, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    }


    for (auto prv_it : m_ams_preview_list) {
        AMSPreview* prv = prv_it.second;
        if (prv->get_ams_id() == m_current_show_ams_left || prv->get_ams_id() == m_current_show_ams_right) {
            prv->OnSelected();
            m_current_select = ams_id;

            bool ready_selected = false;
            for (auto item_it : m_ams_item_list) {
                AmsItem* item = item_it.second;
                if (item->get_ams_id() == ams_id) {
                    for (auto lib_it : item->get_can_lib_list()) {
                        AMSLib* lib = lib_it.second;
                        if (lib->is_selected()) {
                            ready_selected = true;
                        }
                    }
                }
            }
            if (is_in_right){
                m_current_show_ams_right = ams_id;
            }
            else{
                m_current_show_ams_left = ams_id;
            }

        } else {
            prv->UnSelected();
        }
    }

    for (auto ams_item : m_ams_item_list) {
        AmsItem* item = ams_item.second;
        if (item->get_ams_id() == ams_id) {
            auto ids = item->get_panel_pos() == AMSPanelPos::LEFT_PANEL ? m_item_ids[DEPUTY_NOZZLE_ID] : m_item_ids[MAIN_NOZZLE_ID];
            auto pos = item->get_panel_pos();
            for (auto id : ids) {
                if (id == item->get_ams_id()) {
                    pos == AMSPanelPos::LEFT_PANEL ? m_simplebook_ams_left->SetSelection(item->get_selection()) : m_simplebook_ams_right->SetSelection(item->get_selection());
                    if (item->get_can_count() == GENERIC_AMS_SLOT_NUM) {
                        if (item->get_ams_model() == AMSModel::AMS_LITE) {
                            if (pos == AMSPanelPos::LEFT_PANEL) {
                                m_down_road->UpdateLeft(m_extder_data.total_extder_count, AMSRoadShowMode::AMS_ROAD_MODE_AMS_LITE);
                            } else {
                                m_down_road->UpdateRight(m_extder_data.total_extder_count, AMSRoadShowMode::AMS_ROAD_MODE_AMS_LITE);
                            }
                        }
                        else {
                            if (pos == AMSPanelPos::LEFT_PANEL) {
                                m_down_road->UpdateLeft(m_extder_data.total_extder_count, AMSRoadShowMode::AMS_ROAD_MODE_FOUR);
                            } else {
                                m_down_road->UpdateRight(m_extder_data.total_extder_count, AMSRoadShowMode::AMS_ROAD_MODE_FOUR);
                            }
                        }
                    }
                    else {
                        AMSRoadShowMode mode = AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
                        for (auto it : pair_id) {
                            if (it.first == ams_id || it.second == ams_id) {
                                mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
                                break;
                            }
                        }
                        pos == AMSPanelPos::LEFT_PANEL ? m_down_road->UpdateLeft(m_extder_data.total_extder_count, mode)
                            : m_down_road->UpdateRight(m_extder_data.total_extder_count, mode);
                        if (pos == AMSPanelPos::LEFT_PANEL) {
                            m_down_road->UpdatePassRoad(AMSPanelPos::LEFT_PANEL, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
                        } else {
                            m_down_road->UpdatePassRoad(AMSPanelPos::RIGHT_PANEL, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
                        }
                    }
                }
            }
        }
    }

    post_event(SimpleEvent(EVT_AMS_SWITCH));
}

void AMSControl::ShowFilamentTip(bool hasams)
{
    //m_simplebook_right->SetSelection(0);
    if (hasams) {
        m_tip_right_top->Show();
        m_tip_load_info->SetLabelText(_L("Choose an AMS slot then press \"Load\" or \"Unload\" button to automatically load or unload filaments."));
    } else {
        // m_tip_load_info->SetLabelText(_L("Before loading, please make sure the filament is pushed into toolhead."));
        m_tip_right_top->Hide();
        m_tip_load_info->SetLabelText(wxEmptyString);
    }

    m_tip_load_info->SetMinSize(AMS_STEP_SIZE);
    m_tip_load_info->Wrap(AMS_STEP_SIZE.x - FromDIP(5));
    m_sizer_right_tip->Layout();
}

bool AMSControl::Enable(bool enable)
{
    for (auto prv_it : m_ams_preview_list) {
        AMSPreview* prv = prv_it.second;
        prv->Enable(enable);
    }

    for (auto item_it : m_ams_item_list) {
        AmsItem* item = item_it.second;
        item->Enable(enable);
    }

    m_button_extruder_feed->Enable(enable);
    m_button_extruder_back->Enable(enable);
    m_button_auto_refill->Enable(enable);
    m_button_ams_setting->Enable(enable);

    m_filament_load_step->Enable(enable);
    return wxWindow::Enable(enable);
}

void AMSControl::SetExtruder(bool on_off, std::string ams_id, std::string slot_id)
{
    AmsItem *item = nullptr;
    if (m_ams_item_list.find(ams_id) != m_ams_item_list.end()) { item = m_ams_item_list[ams_id]; }

    if (!item) {
        return;
    }
    if (!on_off) {
        m_extruder->OnAmsLoading(false, item->get_nozzle_id());
    } else {
        auto col = item->GetTagColr(slot_id);
        m_extruder->OnAmsLoading(true, item->get_nozzle_id(), col);
    }
}

void AMSControl::SetAmsStep(std::string ams_id, std::string canid, AMSPassRoadType type, AMSPassRoadSTEP step)
{
    AmsItem* ams = nullptr;
    auto amsit = m_ams_item_list.find(ams_id);
    bool in_same_page = false;

    if (amsit != m_ams_item_list.end()) {ams = amsit->second;}
    else {return;}
    if (ams == nullptr) return;

    m_last_ams_id = ams_id;
    m_last_tray_id = canid;
    int can_index = atoi(canid.c_str());

    std::vector<std::string> cur_left_ams;
    std::vector<std::string> cur_right_ams;

    std::string ams_id_left = GetCurentShowAms(AMSPanelPos::LEFT_PANEL);
    std::string ams_id_right = GetCurentShowAms(AMSPanelPos::RIGHT_PANEL);

    for (auto it : pair_id) {
        if ((ams_id_left == it.first || ams_id_left == it.second)) {
            cur_left_ams.push_back(it.first);
            cur_left_ams.push_back(it.second);
        }
        else if ((ams_id_right == it.first || ams_id_right == it.second)) {
            cur_right_ams.push_back(it.first);
            cur_right_ams.push_back(it.second);
        }
    }

    auto left = !IsAmsInRightPanel(ams_id);
    auto length = -1;
    auto model = AMSModel::AMS_LITE;
    auto in_pair = false;

    if (std::find(cur_left_ams.begin(), cur_left_ams.end(), ams_id) != cur_left_ams.end()) {
        in_same_page = true;
    }

    if (std::find(cur_right_ams.begin(), cur_right_ams.end(), ams_id) != cur_right_ams.end()) {
        in_same_page = true;
    }

    //Set path length in different case
    if (ams->get_can_count() == GENERIC_AMS_SLOT_NUM) {
        length = left ? 129 : 145;
        model  = ams->get_ams_model();
    } else if (ams->get_can_count() == 1) {
        for (auto it : pair_id){
            if (it.first == ams_id){
                length = left ? 218 : 124;
                in_pair = true;
                break;
            }
            else if (it.second == ams_id){
                length = left ? 124 : 232;
                in_pair = true;
                break;
            }
        }
        model = ams->get_ams_model();
    }
    if (model == AMSModel::AMS_LITE){
        length = left ? 145 : 45;
    }
    if (model == EXT_AMS && ams->get_ext_type() == AMSModelOriginType::LITE_EXT) {

       if (m_ams_info.size() == 0 && m_ext_info.size() == 1) {
           length = 13;
       } else {
           length = 145;
       }
    }

    if (model == EXT_AMS && ams->get_ext_type() == AMSModelOriginType::GENERIC_EXT) {
        if (m_ams_info.size() == 0 && m_ext_info.size() == 1) {
            left = true;
            length = 50;
        } else {
            /*check in pair*/
            if (in_pair) {
                length = left ? 110 : 232;
            } else {
                length = left ? 192 : 82;
            }
        }
    }

    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == ams_id) {
            m_ams_info[i].current_step = step;
            m_ams_info[i].current_can_id = canid;
        }
    }
    for (auto i = 0; i < m_ext_info.size(); i++) {
        if (m_ext_info[i].ams_id == ams_id) {
            m_ext_info[i].current_step = step;
            m_ext_info[i].current_can_id = canid;
        }
    }


    AMSinfo info;
    if (m_ams_item_list.find(ams_id) != m_ams_item_list.end()) {
        info = m_ams_item_list[ams_id]->get_ams_info();
    }
    else{
        return;
    }
    if (can_index >= 0 && can_index < info.cans.size())
    {
        m_down_road->SetPassRoadColour(left, info.cans[can_index].material_colour);
    }

    AMSPanelPos pos = left ? AMSPanelPos::LEFT_PANEL : AMSPanelPos::RIGHT_PANEL;

    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
        //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
        ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
        if (ams_id_left == ams_id || ams_id_right == ams_id || in_same_page) {
            m_down_road->UpdatePassRoad(pos, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
            m_extruder->OnAmsLoading(false, ams->get_nozzle_id());
        }
    }

    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1) {
        ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
        if (ams_id_left == ams_id || ams_id_right == ams_id || in_same_page) {
            m_down_road->UpdatePassRoad(pos, length, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
            m_extruder->OnAmsLoading(false, ams->get_nozzle_id());
        }
        else
        {
            m_down_road->UpdatePassRoad(pos, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
        }
    }

    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2) {
        ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
        if (ams_id_left == ams_id || ams_id_right == ams_id || in_same_page) {
            m_down_road->UpdatePassRoad(pos, length, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
            m_extruder->OnAmsLoading(true, ams->get_nozzle_id(), ams->GetTagColr(canid));
        }
        else
        {
            m_down_road->UpdatePassRoad(pos, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
        }
    }
    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3) {
        ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
        if (ams_id_left == ams_id || ams_id_right == ams_id || in_same_page)
        {
            m_down_road->UpdatePassRoad(pos, length, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
            m_extruder->OnAmsLoading(true, ams->get_nozzle_id(), ams->GetTagColr(canid));
        }
        else
        {
            m_down_road->UpdatePassRoad(pos, -1, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
        }
    }
}

void AMSControl::on_filament_load(wxCommandEvent &event)
{
    m_button_extruder_back->Disable();
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == m_current_ams) { m_ams_info[i].current_action = AMSAction::AMS_ACTION_LOAD; }
    }
    post_event(SimpleEvent(EVT_AMS_LOAD));
}

void AMSControl::on_extrusion_cali(wxCommandEvent &event)
{
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == m_current_ams) { m_ams_info[i].current_action = AMSAction::AMS_ACTION_CALI; }
    }
    post_event(SimpleEvent(EVT_AMS_EXTRUSION_CALI));
}

void AMSControl::on_filament_unload(wxCommandEvent &event)
{
    m_button_extruder_feed->Disable();
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == m_current_ams) { m_ams_info[i].current_action = AMSAction::AMS_ACTION_UNLOAD; }
    }
    post_event(SimpleEvent(EVT_AMS_UNLOAD));
}

void AMSControl::auto_refill(wxCommandEvent& event)
{
    post_event(SimpleEvent(EVT_AMS_FILAMENT_BACKUP));
}

void AMSControl::on_ams_setting_click(wxMouseEvent &event)
{
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == m_current_ams) { m_ams_info[i].current_action = AMSAction::AMS_ACTION_CALI; }
    }
    post_event(SimpleEvent(EVT_AMS_SETTINGS));
}

void AMSControl::parse_object(MachineObject* obj) {
    if (!obj || obj->amsList.size() == 0)
    {
        return;
    }
    m_ams_info.clear();
    for (auto ams : obj->amsList)
    {
        AMSinfo info;
        info.parse_ams_info(obj, ams.second);
        m_ams_info.push_back(info);
    }
}

void AMSControl::on_clibration_again_click(wxMouseEvent &event) { post_event(SimpleEvent(EVT_AMS_CLIBRATION_AGAIN)); }

void AMSControl::on_clibration_cancel_click(wxMouseEvent &event) { post_event(SimpleEvent(EVT_AMS_CLIBRATION_CANCEL)); }

void AMSControl::post_event(wxEvent &&event)
{
    event.SetEventObject(m_parent);
    wxPostEvent(m_parent, event);
}

}} // namespace Slic3r::GUI
