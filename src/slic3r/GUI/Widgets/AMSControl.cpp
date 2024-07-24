#include "AMSControl.hpp"
#include "Label.hpp"
#include "../BitmapCache.hpp"
#include "../I18N.hpp"
#include "../GUI_App.hpp"

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
#define SINGLE_SLOT_AMS_PANEL_SIZE wxSize(FromDIP(264), (160))
AMSControl::AMSControl(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
    : wxSimplebook(parent, wxID_ANY, pos, size)
    , m_Humidity_tip_popup(AmsHumidityTipPopup(this))
    , m_ams_introduce_popup(AmsIntroducePopup(this))
{
    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (dev) {
        MachineObject *obj = dev->get_selected_machine();
        parse_object(obj);
    }

    SetBackgroundColour(*wxWHITE);
    // normal mode
    Freeze();
    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxVERTICAL);
    m_amswin                 = new wxWindow(this, wxID_ANY);
    m_amswin->SetBackgroundColour(*wxWHITE);
    //m_amswin->SetBackgroundColour(wxColour(0x00CED1));
    m_amswin->SetSize(wxSize(FromDIP(578), -1));
    m_amswin->SetMinSize(wxSize(FromDIP(578), -1));


    m_sizer_ams_items = new wxBoxSizer(wxHORIZONTAL);

    /*right items*/
    m_panel_items_left = new wxPanel(m_amswin, wxID_ANY);
    m_panel_items_left->SetSize(AMS_ITEMS_PANEL_SIZE);
    m_panel_items_left->SetMinSize(AMS_ITEMS_PANEL_SIZE);
    //m_panel_items_left->SetBackgroundColour(0x4169E1);
    m_panel_items_left->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    m_sizer_items_left = new wxBoxSizer(wxHORIZONTAL);
    m_panel_items_left->SetSizer(m_sizer_items_left);
    m_panel_items_left->Layout();
    //m_sizer_items_left->Fit(m_panel_items_left);

    /*right items*/
    m_panel_items_right = new wxPanel(m_amswin, wxID_ANY);
    m_panel_items_right->SetSize(AMS_ITEMS_PANEL_SIZE);
    m_panel_items_right->SetMinSize(AMS_ITEMS_PANEL_SIZE);
    //m_panel_items_right->SetBackgroundColour(0x4169E1);
    m_panel_items_right->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    m_sizer_items_right = new wxBoxSizer(wxHORIZONTAL);
    m_panel_items_right->SetSizer(m_sizer_items_right);
    m_panel_items_right->Layout();
    //m_sizer_items_right->Fit(m_panel_items_right);

    /*m_sizer_ams_items->Add(m_panel_items_left, 0, wxALIGN_CENTER|wxLEFT|wxRIGHT, FromDIP(5));
    m_sizer_ams_items->Add(m_panel_items_right, 0, wxALIGN_CENTER|wxLEFT|wxRIGHT, FromDIP(5));*/
    m_sizer_ams_items->Add(m_panel_items_left, 0, wxLEFT, FromDIP(5));
    m_sizer_ams_items->Add(m_panel_items_right, 0, wxLEFT, FromDIP(5));

    //m_panel_items_right->Hide();

    //m_sizer_ams_body = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_ams_body = new wxBoxSizer(wxHORIZONTAL);

    //ams tip
//    m_sizer_ams_tips = new wxBoxSizer(wxHORIZONTAL);
//    m_ams_tip = new Label(m_amswin, _L("AMS"));
//    m_ams_tip->SetFont(::Label::Body_12);
//    m_ams_tip->SetBackgroundColour(*wxWHITE);
//    m_img_amsmapping_tip = new wxStaticBitmap(m_amswin, wxID_ANY, create_scaled_bitmap("enable_ams", this, 16), wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)), 0);
//    m_img_amsmapping_tip->SetBackgroundColour(*wxWHITE);
//
//    m_sizer_ams_tips->Add(m_ams_tip, 0, wxTOP, FromDIP(5));
//    m_sizer_ams_tips->Add(m_img_amsmapping_tip, 0, wxALL, FromDIP(3));
//
//    m_img_amsmapping_tip->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
//         wxPoint img_pos = m_img_amsmapping_tip->ClientToScreen(wxPoint(0, 0));
//         wxPoint popup_pos(img_pos.x, img_pos.y + m_img_amsmapping_tip->GetRect().height);
//         m_ams_introduce_popup.set_mode(true);
//         m_ams_introduce_popup.Position(popup_pos, wxSize(0, 0));
//         m_ams_introduce_popup.Popup();
//
//#ifdef __WXMSW__
//         wxCommandEvent close_event(EVT_CLEAR_SPEED_CONTROL);
//         wxPostEvent(this, close_event);
//#endif // __WXMSW__
//    });
//    m_img_amsmapping_tip->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {
//         m_ams_introduce_popup.Dismiss();
//    });
//
//
   

    //ams area
    /*m_panel_left_ams_area = new StaticBox(m_amswin, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_panel_left_ams_area->SetMaxSize(AMS_CANS_SIZE);
    m_panel_left_ams_area->SetMinSize(AMS_CANS_SIZE);
    m_panel_left_ams_area->SetCornerRadius(FromDIP(10));
    m_panel_left_ams_area->SetBackgroundColor(StateColor(std::pair<wxColour, int>(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, StateColor::Normal)));

    m_panel_right_ams_area = new StaticBox(m_amswin, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_panel_right_ams_area->SetMaxSize(AMS_CANS_SIZE);
    m_panel_right_ams_area->SetMinSize(AMS_CANS_SIZE);
    m_panel_right_ams_area->SetCornerRadius(FromDIP(10));
    m_panel_right_ams_area->SetBackgroundColor(StateColor(std::pair<wxColour, int>(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, StateColor::Normal)));*/

    m_sizer_ams_area_left = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_ams_area_right = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_down_road = new wxBoxSizer(wxHORIZONTAL);

    m_simplebook_ams_left = new wxSimplebook(m_amswin, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    m_simplebook_ams_left->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    m_sizer_ams_area_left->Add(m_simplebook_ams_left, 0, wxLEFT | wxRIGHT, FromDIP(10));

    m_simplebook_ams_right = new wxSimplebook(m_amswin, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    m_simplebook_ams_right->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    m_sizer_ams_area_right->Add(m_simplebook_ams_right, 0, wxLEFT | wxRIGHT, FromDIP(10));

    m_panel_down_road = new wxPanel(m_amswin, wxID_ANY, wxDefaultPosition, AMS_DOWN_ROAD_SIZE, 0);
    m_panel_down_road->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);

    m_down_road = new AMSRoadDownPart(m_panel_down_road, wxID_ANY, wxDefaultPosition, AMS_DOWN_ROAD_SIZE);
    m_sizer_down_road->Add(m_panel_down_road, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 0);

    // ams mode
    //m_simplebook_ams_left = new wxSimplebook(m_simplebook_ams_left, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    //m_simplebook_ams_left->SetBackgroundColour(*wxGREEN);
    //m_simplebook_ams_left->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    //m_simplebook_ams_right = new wxSimplebook(m_simplebook_ams_right, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    m_simplebook_ams_right->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);


    //extra ams mode
    //m_simplebook_extra_cans_left = new wxSimplebook(m_simplebook_ams_left, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    //m_simplebook_extra_cans_left->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
   // m_simplebook_extra_cans_right = new wxSimplebook(m_simplebook_ams_right, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    //m_simplebook_extra_cans_right->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);

    //m_simplebook_ams_left->AddPage(m_simplebook_ams_left, wxEmptyString, false);
    //m_simplebook_ams_left->AddPage(m_simplebook_extra_cans_left, wxEmptyString, false);
    //m_simplebook_ams_right->AddPage(m_simplebook_ams_right, wxEmptyString, false);
    //m_simplebook_ams_right->AddPage(m_simplebook_extra_cans_right, wxEmptyString, false);

    m_sizer_ams_area_left->Layout();
    m_sizer_ams_area_right->Layout();


    m_sizer_ams_option = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_option_left = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_option_mid = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_option_right = new wxBoxSizer(wxHORIZONTAL);

    /*m_sizer_option_left->SetMinSize( wxSize( AMS_CANS_SIZE.x,-1 ) );
    m_sizer_option_right->SetMinSize( wxSize( AMS_CANS_SIZE.x,-1 ) );*/
    /*m_sizer_option_left->SetMinSize(wxSize(FromDIP(239), -1));
    m_sizer_option_right->SetMinSize(wxSize(FromDIP(239), -1));*/
    m_sizer_option_left->SetMinSize(wxSize(FromDIP(140), -1));
    m_sizer_option_right->SetMinSize(wxSize(FromDIP(140), -1));

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
    m_button_auto_refill = new Button(m_amswin, _L("Auto-refill"));
    m_button_auto_refill->SetBackgroundColor(btn_bg_white);
    m_button_auto_refill->SetBorderColor(btn_bd_white);
    m_button_auto_refill->SetTextColor(btn_text_white);
    m_button_auto_refill->SetFont(Label::Body_13);
    // m_img_ams_backup = new wxStaticBitmap(m_amswin, wxID_ANY, create_scaled_bitmap("automatic_material_renewal", this, 16), wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)), 0);
    // m_img_ams_backup->SetBackgroundColour(*wxWHITE);
    // m_img_ams_backup->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_HAND); });
    // m_img_ams_backup->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_ARROW); });
    // m_img_ams_backup->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {post_event(SimpleEvent(EVT_AMS_FILAMENT_BACKUP)); });
    m_sizer_option_left->Add(m_button_auto_refill, 0, wxALIGN_CENTER, 0);

    m_button_ams_setting_normal = ScalableBitmap(this, "ams_setting_normal", 24);
    m_button_ams_setting_hover = ScalableBitmap(this, "ams_setting_hover", 24);
    m_button_ams_setting_press = ScalableBitmap(this, "ams_setting_press", 24);

    m_button_ams_setting = new wxStaticBitmap(m_amswin, wxID_ANY, m_button_ams_setting_normal.bmp(), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)));
    m_button_ams_setting->SetBackgroundColour(m_amswin->GetBackgroundColour());
    m_sizer_option_left->Add(m_button_ams_setting, 0, wxALIGN_CENTER|wxLEFT, FromDIP(5));


    /*option mid*/
    m_extruder = new AMSextruder(m_amswin, wxID_ANY, wxDefaultPosition, AMS_EXTRUDER_SIZE);
    m_sizer_option_mid->Add( m_extruder, 0, wxALIGN_CENTER, 0 );


    /*option right*/



    m_button_extruder_feed = new Button(m_amswin, _L("Load"));
    m_button_extruder_feed->SetFont(Label::Body_13);

    m_button_extruder_feed->SetBackgroundColor(btn_bg_green);
    m_button_extruder_feed->SetBorderColor(btn_bd_green);
    m_button_extruder_feed->SetTextColor(btn_text_green);


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

    m_button_extruder_back = new Button(m_amswin, _L("Unload"));
    m_button_extruder_back->SetBackgroundColor(btn_bg_white);
    m_button_extruder_back->SetBorderColor(btn_bd_white);
    m_button_extruder_back->SetTextColor(btn_text_white);
    m_button_extruder_back->SetFont(Label::Body_13);

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
    m_sizer_option_right->Add(m_button_extruder_feed, 0, wxLEFT, FromDIP(5));


    m_sizer_ams_option->Add(m_sizer_option_left, 0, wxLEFT, 0);
    //m_sizer_ams_option->Add(m_sizer_option_mid, 1, wxEXPAND, 0);
    m_sizer_ams_option->Add(m_sizer_option_mid, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT, FromDIP(60));
    //m_sizer_ams_option->Add(m_sizer_option_right, 0, wxEXPAND, 0);
    m_sizer_ams_option->Add(m_sizer_option_right, 0, wxALIGN_CENTER, 0);



    //virtual ams
    //m_panel_virtual = new StaticBox(m_amswin, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    //m_panel_virtual->SetBackgroundColor(StateColor(std::pair<wxColour, int>(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, StateColor::Normal)));
    //m_panel_virtual->SetMinSize(wxSize(AMS_CAN_LIB_SIZE.x + FromDIP(16), AMS_CANS_SIZE.y));
    //m_panel_virtual->SetMaxSize(wxSize(AMS_CAN_LIB_SIZE.x + FromDIP(16), AMS_CANS_SIZE.y));

    //m_vams_info.material_state = AMSCanType::AMS_CAN_TYPE_VIRTUAL;
    //m_vams_info.can_id = wxString::Format("%d", VIRTUAL_TRAY_MAIN_ID).ToStdString();

    //auto vams_panel = new wxWindow(m_panel_virtual, wxID_ANY);
    //vams_panel->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);

    //m_vams_lib = new AMSLib(vams_panel, m_vams_info);
    //m_vams_road = new AMSRoad(vams_panel, wxID_ANY, m_vams_info, -1, -1, wxDefaultPosition, AMS_CAN_ROAD_SIZE);

    //m_vams_lib->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
    //    //clear all selected
    //    m_current_ams = m_vams_info.can_id;
    //    m_vams_lib->OnSelected();

    //    SwitchAms(m_current_ams);
    //    for (auto i = 0; i < m_ams_item_list.GetCount(); i++) {
    //        AmsCansWindow* cans = m_ams_item_list[i];
    //        cans->amsCans->SelectCan(m_current_ams);
    //    }

    //    e.Skip();
    //    });

    //Bind(EVT_AMS_UNSELETED_VAMS, [this](wxCommandEvent& e) {
    //    /*if (m_current_ams == e.GetString().ToStdString()) {
    //        return;
    //    }*/
    //    m_current_ams = e.GetString().ToStdString();
    //    SwitchAms(m_current_ams);
    //    m_vams_lib->UnSelected();
    //    e.Skip();
    //});

    //wxBoxSizer* m_vams_top_sizer = new wxBoxSizer(wxVERTICAL);

    //m_vams_top_sizer->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(14));
    //m_vams_top_sizer->Add(0, 0, 0, wxEXPAND | wxTOP, AMS_REFRESH_SIZE.y);
    //m_vams_top_sizer->Add(m_vams_lib, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, FromDIP(4));
    //m_vams_top_sizer->Add(m_vams_road, 0, wxALL, 0);

    //Bind(EVT_AMS_UNSELETED_VAMS, [this](wxCommandEvent& e) {
    //    /*if (m_current_ams == e.GetString().ToStdString()) {
    //        return;
    //    }*/
    //    m_current_ams = e.GetString().ToStdString();
    //    SwitchAms(m_current_ams);
    //    m_vams_lib->UnSelected();
    //    e.Skip();
    //});

    ////extra road

    //vams_panel->SetSizer(m_vams_top_sizer);
    //vams_panel->Layout();
    //vams_panel->Fit();

    //wxBoxSizer* m_sizer_vams_panel = new wxBoxSizer(wxVERTICAL);

    //m_sizer_vams_panel->Add(vams_panel, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    //m_panel_virtual->SetSizer(m_sizer_vams_panel);
    //m_panel_virtual->Layout();
    //m_panel_virtual->Fit();

    //m_vams_sizer =  new wxBoxSizer(wxVERTICAL);
    //m_sizer_vams_tips = new wxBoxSizer(wxHORIZONTAL);

//    auto m_vams_tip = new wxStaticText(m_amswin, wxID_ANY, _L("Ext Spool"), wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
//    m_vams_tip->SetMaxSize(wxSize(FromDIP(66), -1));
//    m_vams_tip->SetFont(::Label::Body_12);
//    m_vams_tip->SetBackgroundColour(*wxWHITE);
//    m_img_vams_tip = new wxStaticBitmap(m_amswin, wxID_ANY, create_scaled_bitmap("enable_ams", this, 16), wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)), 0);
//    m_img_vams_tip->SetBackgroundColour(*wxWHITE);
//    m_img_vams_tip->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
//        wxPoint img_pos = m_img_vams_tip->ClientToScreen(wxPoint(0, 0));
//        wxPoint popup_pos(img_pos.x, img_pos.y + m_img_vams_tip->GetRect().height);
//        m_ams_introduce_popup.set_mode(false);
//        m_ams_introduce_popup.Position(popup_pos, wxSize(0, 0));
//        m_ams_introduce_popup.Popup();
//
//#ifdef __WXMSW__
//        wxCommandEvent close_event(EVT_CLEAR_SPEED_CONTROL);
//        wxPostEvent(this, close_event);
//#endif // __WXMSW__
//    });
//
//    m_img_vams_tip->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e) {
//        m_ams_introduce_popup.Dismiss();
//    });
//
//    m_sizer_vams_tips->Add(m_vams_tip, 0, wxTOP, FromDIP(5));
//    m_sizer_vams_tips->Add(m_img_vams_tip, 0, wxALL, FromDIP(3));

    //m_vams_extra_road = new AMSVirtualRoad(m_amswin, wxID_ANY);
    //m_vams_extra_road->SetMinSize(wxSize(m_panel_virtual->GetSize().x + FromDIP(16), -1));

    //m_vams_sizer->Add(m_sizer_vams_tips, 0, wxALIGN_CENTER, 0);
    //m_vams_sizer->Add(m_panel_virtual, 0, wxALIGN_CENTER, 0);
    //m_vams_sizer->Add(m_vams_extra_road, 1, wxEXPAND, 0);


    //Right
    /*
        addaddaddaddaddaaddaddaddadd
    */

     /*
    wxBoxSizer *m_sizer_right = new wxBoxSizer(wxVERTICAL);
    m_simplebook_right        = new wxSimplebook(m_amswin, wxID_ANY);
    m_simplebook_right->SetMinSize(wxSize(AMS_STEP_SIZE.x, AMS_STEP_SIZE.y + FromDIP(19)));
    m_simplebook_right->SetMaxSize(wxSize(AMS_STEP_SIZE.x, AMS_STEP_SIZE.y + FromDIP(19)));
    m_simplebook_right->SetBackgroundColour(*wxWHITE);

    m_sizer_right->Add(m_simplebook_right, 0, wxALL, 0);

    auto tip_right    = new wxPanel(m_simplebook_right, wxID_ANY, wxDefaultPosition, AMS_STEP_SIZE, wxTAB_TRAVERSAL);
    m_sizer_right_tip = new wxBoxSizer(wxVERTICAL);

    m_tip_right_top   = new wxStaticText(tip_right, wxID_ANY, _L("Tips"), wxDefaultPosition, wxDefaultSize, 0);
    m_tip_right_top->SetFont(::Label::Head_13);
    m_tip_right_top->SetForegroundColour(AMS_CONTROL_BRAND_COLOUR);
    m_tip_right_top->Wrap(AMS_STEP_SIZE.x);


    m_tip_load_info = new ::Label(tip_right, wxEmptyString);
    m_tip_load_info->SetFont(::Label::Body_13);
    m_tip_load_info->SetBackgroundColour(*wxWHITE);
    m_tip_load_info->SetForegroundColour(AMS_CONTROL_GRAY700);

    m_sizer_right_tip->Add(m_tip_right_top, 0, 0, 0);
    m_sizer_right_tip->Add(0, 0, 0, wxEXPAND, FromDIP(10));
    m_sizer_right_tip->Add(m_tip_load_info, 0, 0, 0);

    tip_right->SetSizer(m_sizer_right_tip);
    tip_right->Layout();

    m_filament_load_step = new ::StepIndicator(m_simplebook_right, wxID_ANY);
    m_filament_load_step->SetMinSize(AMS_STEP_SIZE);
    m_filament_load_step->SetMaxSize(AMS_STEP_SIZE);
    m_filament_load_step->SetBackgroundColour(*wxWHITE);

    m_filament_unload_step = new ::StepIndicator(m_simplebook_right, wxID_ANY);
    m_filament_unload_step->SetMinSize(AMS_STEP_SIZE);
    m_filament_unload_step->SetMaxSize(AMS_STEP_SIZE);
    m_filament_unload_step->SetBackgroundColour(*wxWHITE);

    m_filament_vt_load_step = new ::StepIndicator(m_simplebook_right, wxID_ANY);
    m_filament_vt_load_step->SetMinSize(AMS_STEP_SIZE);
    m_filament_vt_load_step->SetMaxSize(AMS_STEP_SIZE);
    m_filament_vt_load_step->SetBackgroundColour(*wxWHITE);

    m_simplebook_right->AddPage(tip_right, wxEmptyString, false);
    m_simplebook_right->AddPage(m_filament_load_step, wxEmptyString, false);
    m_simplebook_right->AddPage(m_filament_unload_step, wxEmptyString, false);
    m_simplebook_right->AddPage(m_filament_vt_load_step, wxEmptyString, false);




    m_button_guide = new Button(m_amswin, _L("Guide"));
    m_button_guide->SetFont(Label::Body_13);
    if (wxGetApp().app_config->get("language") == "de_DE") m_button_guide->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "fr_FR") m_button_guide->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ru_RU") m_button_guide->SetLabel("Guide");
    if (wxGetApp().app_config->get("language") == "nl_NL") m_button_guide->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "hu_HU") m_button_guide->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ja_JP") m_button_guide->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "sv_SE") m_button_guide->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "cs_CZ") m_button_guide->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "uk_UA") m_button_guide->SetFont(Label::Body_9);

    m_button_guide->SetCornerRadius(FromDIP(12));
    m_button_guide->SetBorderColor(btn_bd_white);
    m_button_guide->SetTextColor(btn_text_white);
    m_button_guide->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_guide->SetBackgroundColor(btn_bg_white);

    m_button_retry = new Button(m_amswin, _L("Retry"));
    m_button_retry->SetFont(Label::Body_13);

    if (wxGetApp().app_config->get("language") == "de_DE") m_button_retry->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "fr_FR") m_button_retry->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ru_RU") m_button_retry->SetLabel("Retry");
    if (wxGetApp().app_config->get("language") == "nl_NL") m_button_retry->SetLabel("Retry");
    if (wxGetApp().app_config->get("language") == "tr_TR") m_button_retry->SetLabel("Retry");
    if (wxGetApp().app_config->get("language") == "hu_HU") m_button_retry->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ja_JP") m_button_retry->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "sv_SE") m_button_retry->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "cs_CZ") m_button_retry->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "uk_UA") m_button_retry->SetFont(Label::Body_9);

    m_button_retry->SetCornerRadius(FromDIP(12));
    m_button_retry->SetBorderColor(btn_bd_white);
    m_button_retry->SetTextColor(btn_text_white);
    m_button_retry->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_retry->SetBackgroundColor(btn_bg_white);

    m_sizer_right_bottom->Add(m_button_ams_setting, 0);
    m_sizer_right_bottom->Add(m_button_guide, 0, wxLEFT, FromDIP(10));
    m_sizer_right_bottom->Add(m_button_retry, 0, wxLEFT, FromDIP(10));
    m_sizer_right->Add(m_sizer_right_bottom, 0, wxEXPAND | wxTOP, FromDIP(20));*/


    /*
        addaddaddaddaddaaddaddaddadd
    */

    m_sizer_ams_body->Add(m_sizer_ams_area_left, wxLEFT|wxRIGHT, FromDIP(5));
    //m_sizer_ams_body->Add(0, 0, 0, wxLEFT, FromDIP(15));
    m_sizer_ams_body->Add(m_sizer_ams_area_right, wxLEFT|wxRIGHT, FromDIP(5));

    //m_sizer_ams_body->Add(m_sizer_right, 0, wxEXPAND, FromDIP(0));

    m_sizer_body->Add(m_sizer_ams_items, 0, wxALIGN_CENTER, 0);
    m_sizer_body->Add(0, 0, 1, wxEXPAND | wxTOP, FromDIP(10));
    m_sizer_body->Add(m_sizer_ams_body, 0, wxALIGN_CENTER, 0);
    m_sizer_body->Add(m_sizer_down_road, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 0);
    //m_sizer_body->Add(m_sizer_ams_body, 0, wxEXPAND, 0);
    //m_sizer_body->Add(m_sizer_ams_option, 0, wxEXPAND, 0);
    m_sizer_body->Add(m_sizer_ams_option, 0, wxALIGN_CENTER, 0);

    m_amswin->SetSizer(m_sizer_body);
    m_amswin->Layout();
    m_amswin->Fit();
    Thaw();

    SetSize(m_amswin->GetSize());
    SetMinSize(m_amswin->GetSize());



    AddPage(m_amswin, wxEmptyString, false);

    UpdateStepCtrl(false);

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

        wxPoint img_pos = ClientToScreen(wxPoint(0, 0));
        wxPoint popup_pos(img_pos.x - m_Humidity_tip_popup.GetSize().GetWidth() + FromDIP(150), img_pos.y - FromDIP(80));
        m_Humidity_tip_popup.Position(popup_pos, wxSize(0, 0));
        if (m_ams_info.size() > 0) {
            for (auto i = 0; i < m_ams_info.size(); i++) {
                if (m_ams_info[i].ams_id == m_current_show_ams_left || m_ams_info[i].ams_id == m_current_show_ams_right) {
                    m_Humidity_tip_popup.set_humidity_level(m_ams_info[i].ams_humidity);
                }
            }

        }
        m_Humidity_tip_popup.Popup();
    });
    Bind(EVT_AMS_ON_SELECTED, &AMSControl::AmsSelectedSwitch, this);


    /* m_button_guide->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
         post_event(wxCommandEvent(EVT_AMS_GUIDE_WIKI));
         });
     m_button_retry->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
         post_event(wxCommandEvent(EVT_AMS_RETRY));
         });*/

    //CreateAms();
    //CreateAmsNew();
    //EnterNoneAMSMode();

}

void AMSControl::on_retry()
{
    post_event(wxCommandEvent(EVT_AMS_RETRY));
}

AMSControl::~AMSControl() {
    /*m_simplebook_ams_left->DeleteAllPages();
    m_simplebook_ams_right->DeleteAllPages();*/
}

std::string AMSControl::GetCurentAms() {
    return m_current_ams;
}
std::string AMSControl::GetCurentShowAms(bool right_panel) {
    if (right_panel){
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
        if (item->m_info.ams_id == amsid) {
            current_can = item->GetCurrentCan();
            return current_can;
        }
    }
    return current_can;
}

bool AMSControl::IsAmsInRightPanel(std::string ams_id) {
    if (m_nozzle_num == 2){
        if (m_ams_item_list[ams_id]->m_info.nozzle_id == 0){
            return true;
        }
        else{
            return false;
        }
    }
    else{
        for (auto id : m_item_ids[1]){
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
    if (m_current_show_ams_left != ams_id_selected){
        auto item = m_ams_item_list[m_current_show_ams_left];
        if (!item) return;
        try{
            for (auto can : item->m_can_lib_list){
                can.second->UnSelected();
            }
        }
        catch (...){
            ;
        }
    }
    else if (m_current_show_ams_right != ams_id_selected){
        auto item = m_ams_item_list[m_current_show_ams_right];
        if (!item) return;
        try {
            for (auto can : item->m_can_lib_list) {
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
    m_panel_items_left->Hide();

    m_simplebook_ams_left->SetSelection(0);
    m_extruder->no_ams_mode(true);
    //m_button_ams_setting->Hide();
    //m_button_guide->Hide();
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
    //m_vams_lib->m_ams_model = m_ext_model;
    if(m_is_none_ams_mode == AMSModel::GENERIC_AMS) return;
    //m_panel_items_left->Show();

    //m_vams_lib->m_ams_model = AMSModel::GENERIC_AMS;
    //m_ams_tip->SetLabel(_L("AMS"));
    //m_img_vams_tip->SetBitmap(create_scaled_bitmap("enable_ams", this, 16));
    //m_img_vams_tip->Enable();
    //m_img_amsmapping_tip->SetBitmap(create_scaled_bitmap("enable_ams", this, 16));
    //m_img_amsmapping_tip->Enable();

    //m_simplebook_ams_left->SetSelection(0);
    m_extruder->no_ams_mode(false);
    /*m_button_ams_setting->Show();
    m_button_guide->Show();
    m_button_retry->Show();
    m_button_extruder_feed->Show();
    m_button_extruder_back->Show();
    ShowFilamentTip(true);*/
    m_amswin->Layout();
    m_amswin->Fit();
    Layout();
    m_is_none_ams_mode = AMSModel::GENERIC_AMS;
}

void AMSControl::EnterExtraAMSMode()
{
    //m_vams_lib->m_ams_model = m_ext_model;
    if(m_is_none_ams_mode == AMSModel::AMS_LITE) return;
    m_panel_items_left->Hide();


    //m_vams_lib->m_ams_model = AMSModel::EXTRA_AMS;
    //m_ams_tip->SetLabel(wxEmptyString);
    //m_img_vams_tip->SetBitmap(create_scaled_bitmap("enable_ams_disable", this, 16));
    //m_img_vams_tip->Disable();
    //m_img_amsmapping_tip->SetBitmap(create_scaled_bitmap("enable_ams_disable", this, 16));
    //m_img_amsmapping_tip->Disable();

    m_simplebook_ams_left->SetSelection(2);
    m_extruder->no_ams_mode(false);
    /*m_button_ams_setting->Show();
    m_button_guide->Show();
    m_button_retry->Show();
    m_button_extruder_feed->Show();
    m_button_extruder_back->Show();
    ShowFilamentTip(true);*/
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
    if (m_vams_extra_road) m_vams_extra_road->msw_rescale();


    if (m_button_extruder_feed) m_button_extruder_feed->SetMinSize(wxSize(-1, FromDIP(24)));
    if (m_button_extruder_back) m_button_extruder_back->SetMinSize(wxSize(-1, FromDIP(24)));
    if (m_button_auto_refill) m_button_auto_refill->SetMinSize(wxSize(-1, FromDIP(24)));
    if (m_button_ams_setting) m_button_ams_setting->SetMinSize(wxSize(FromDIP(25), FromDIP(24)));
    if (m_button_guide) m_button_guide->SetMinSize(wxSize(-1, FromDIP(24)));
    if (m_button_retry) m_button_retry->SetMinSize(wxSize(-1, FromDIP(24)));
    if (m_vams_lib) m_vams_lib->msw_rescale();


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
    if (m_down_road){
        m_down_road->msw_rescale();
    }

    Layout();
    Refresh();
}

void AMSControl::UpdateStepCtrl(bool is_extrusion)
{
    /*wxString FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_COUNT] = {
            _L("Idling..."),
            _L("Heat the nozzle"),
            _L("Cut filament"),
            _L("Pull back current filament"),
            _L("Push new filament into extruder"),
            _L("Purge old filament"),
            _L("Feed Filament"),
            _L("Confirm extruded"),
            _L("Check filament location")
    };

    m_filament_load_step->DeleteAllItems();
    m_filament_unload_step->DeleteAllItems();
    m_filament_vt_load_step->DeleteAllItems();

    if (m_ams_model == AMSModel::GENERIC_AMS || m_ext_model == AMSModel::GENERIC_AMS) {
        if (is_extrusion) {
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CUT_FILAMENT]);
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PULL_CURR_FILAMENT]);
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PUSH_NEW_FILAMENT]);
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PURGE_OLD_FILAMENT]);
        }
        else {
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PUSH_NEW_FILAMENT]);
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PURGE_OLD_FILAMENT]);
        }

        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PUSH_NEW_FILAMENT]);
        m_filament_vt_load_step->AppendItem(_L("Grab new filament"));
        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PURGE_OLD_FILAMENT]);

        m_filament_unload_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_unload_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CUT_FILAMENT]);
        m_filament_unload_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PULL_CURR_FILAMENT]);
    }


    if (m_ams_model == AMSModel::EXTRA_AMS || m_ext_model == AMSModel::EXTRA_AMS) {
        m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CHECK_POSITION]);
        m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CUT_FILAMENT]);
        m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PULL_CURR_FILAMENT]);
        m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PUSH_NEW_FILAMENT]);
        m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PURGE_OLD_FILAMENT]);

        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CHECK_POSITION]);
        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CUT_FILAMENT]);
        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PULL_CURR_FILAMENT]);
        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PUSH_NEW_FILAMENT]);
        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PURGE_OLD_FILAMENT]);

        m_filament_unload_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_unload_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CHECK_POSITION]);
        m_filament_unload_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CUT_FILAMENT]);
        m_filament_unload_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PULL_CURR_FILAMENT]);
    }*/
}

void AMSControl::UpdatePassRoad(string ams_id, AMSPassRoadType type, AMSPassRoadSTEP step) {
    bool left = false;
    int len = -1;
    for (auto id : m_item_ids[0]){
        if (ams_id == id){
            left = true;
            break;
        }
    }
    if (m_ams_item_list[ams_id]->m_info.cans.size() == 4){
        len = 133;
    }
    else{
        for (auto pairId : pair_id){
            if (pairId.first == ams_id) {
                len = 72;
                break;
            }
            if (pairId.second == ams_id){
                len = 188;
                break;
            }
        }
    }
    if (len == -1){
        if (left){
            len = 213;
        }
        else{
            len = 72;
        }
    }

    //std::vector<int>                 m_item_nums = { 0, 0 };
    //std::vector<std::vector<string>> m_item_ids = { {},{} };
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
    Freeze();
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
        m_sizer_items_left->Layout();
        m_sizer_items_right->Layout();
    }
    Thaw();
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

    m_ams_item_list.clear();
    m_sizer_items_right->Clear();
    m_sizer_items_left->Clear();
}

void AMSControl::CreateAmsNew()
{
    /*m_ams_item_list.clear();
    m_ams_generic_item_list.clear();
    m_ams_extra_item_list.clear();*/
    AMSRoadShowMode left_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
    AMSRoadShowMode right_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
    bool first_left_page = true, first_right_page = true;

    std::vector<AMSinfo> single_info_left;
    std::vector<AMSinfo> single_info_right;

    Freeze();
    for (auto ams_info = m_ams_info.begin(); ams_info != m_ams_info.end(); ams_info++)
    {
        if (first_left_page && ams_info->nozzle_id == 1) {
            first_left_page = false;
            left_init_mode = ams_info->cans.size() == 4 ? AMSRoadShowMode::AMS_ROAD_MODE_FOUR : AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
        }
        if (ams_info->cans.size() == 4)
        {
            if (first_right_page && ams_info->nozzle_id == 0) {
                first_right_page = false;
                right_init_mode = ams_info->ams_type == AMSModel::AMS_LITE ? AMSRoadShowMode::AMS_ROAD_MODE_EXTRA_AMS : AMSRoadShowMode::AMS_ROAD_MODE_FOUR;
            }
            if (first_left_page && ams_info->nozzle_id == 1) {
                first_left_page = false;
                left_init_mode = ams_info->ams_type == AMSModel::AMS_LITE ? AMSRoadShowMode::AMS_ROAD_MODE_EXTRA_AMS : AMSRoadShowMode::AMS_ROAD_MODE_FOUR;
            }
            AddAmsPreview(*ams_info, ams_info->ams_type);
            AddAms(*ams_info);
        }
        else if (ams_info->cans.size() == 1)
        {
            AddAmsPreview(*ams_info, ams_info->ams_type);
            if (ams_info->nozzle_id == 0)
            {
                single_info_right.push_back(*ams_info);
                if (single_info_right.size() == 2)
                {
                    if (first_right_page) {
                        first_right_page = false;
                        right_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
                    }
                    AddAms(single_info_right);
                    pair_id.push_back(std::make_pair(single_info_right[0].ams_id, single_info_right[1].ams_id));
                    single_info_right.clear();
                }
            }
            else if (ams_info->nozzle_id == 1)
            {
                single_info_left.push_back(*ams_info);
                if (single_info_left.size() == 2)
                {
                    if (first_left_page) {
                        first_left_page = false;
                        left_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
                    }
                    AddAms(single_info_left);
                    pair_id.push_back(std::make_pair(single_info_left[0].ams_id, single_info_left[1].ams_id));
                    single_info_left.clear();
                }
            }
        }
    }
    if (m_ext_info.size() <= 1) {
        BOOST_LOG_TRIVIAL(trace) << "vt_slot empty!";
        return;
    }
    AMSinfo ext_info;
    for (auto info : m_ext_info){
        if (info.ams_id == std::to_string(VIRTUAL_TRAY_MAIN_ID)){
            ext_info = info;
        }
    }
    single_info_right.push_back(ext_info);
    //wait add
    if (single_info_right.size() == 2)
    {
        if (first_right_page) {
            first_right_page = false;
            right_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
        }
        pair_id.push_back(std::make_pair(single_info_right[0].ams_id, single_info_right[1].ams_id));
    }
    AddAmsPreview(ext_info, AMSModel::EXT_AMS);
    AddAms(single_info_right);
    single_info_right.clear();

    for (auto info : m_ext_info) {
        if (info.ams_id == std::to_string(VIRTUAL_TRAY_DEPUTY_ID)) {
            ext_info = info;
        }
    }
    single_info_left.push_back(ext_info);
    //wait add
    if (single_info_left.size() == 2)
    {
        if (first_left_page) {
            first_left_page = false;
            left_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
        }
        pair_id.push_back(std::make_pair(single_info_left[0].ams_id, single_info_left[1].ams_id));
    }
    AddAmsPreview(ext_info, AMSModel::EXT_AMS);
    AddAms(single_info_left);
    single_info_left.clear();

    if (m_nozzle_num <= 1)
    {
        m_simplebook_ams_left->Hide();
        m_panel_items_left->Hide();
    }

    else if(m_nozzle_num > 1) {
        m_sizer_items_left->Layout();
        m_sizer_items_right->Layout();
        m_simplebook_ams_left->Show();
        m_panel_items_left->Show();
        m_simplebook_ams_right->Show();
        m_panel_items_right->Show();
        m_simplebook_ams_left->SetSelection(0);
        m_simplebook_ams_right->SetSelection(0);
        m_down_road->UpdateLeft(2, left_init_mode);
        m_down_road->UpdateRight(2, right_init_mode);
    }
    m_extruder->update(2);
    auto it = m_ams_item_list.begin();

    m_current_show_ams_left = "-1";
    m_current_show_ams_right = "-1";
    for (auto it = m_ams_item_list.begin(); it != m_ams_item_list.end(); it++)
    {
        if (it->second->m_info.nozzle_id == 1 && m_current_show_ams_left == "-1"){
            m_current_show_ams_left = it->second->m_info.ams_id;
        }
        if (it->second->m_info.nozzle_id == 0 && m_current_show_ams_right == "-1"){
            m_current_show_ams_right = it->second->m_info.ams_id;
        }
    }
    m_current_ams = "-1";
    m_down_road->UpdatePassRoad("0", true, -1, it->second->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    m_extruder->OnAmsLoading(false, 1);
    m_down_road->UpdatePassRoad("0", false, -1, (++it)->second->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    m_extruder->OnAmsLoading(false);

    //Refresh();
    //Freeze();
    Thaw();
}

void AMSControl::CreateAmsSingleNozzle()
{
    std::vector<int>m_item_nums{0,0};
    std::vector<AMSinfo> single_info;

    Freeze();

    bool left = true;
    AMSRoadShowMode left_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_NONE;
    AMSRoadShowMode right_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_NONE;
    for (auto ams_info = m_ams_info.begin(); ams_info != m_ams_info.end(); ams_info++){
        if (ams_info->cans.size() == 4){
            if (m_item_nums[0] <= m_item_nums[1]){
                if (m_item_nums[0] == 0) left_init_mode = ams_info->ams_type == AMSModel::AMS_LITE ? AMSRoadShowMode::AMS_ROAD_MODE_EXTRA_AMS : AMSRoadShowMode::AMS_ROAD_MODE_FOUR;
                left = true;
                m_item_ids[0].push_back(ams_info->ams_id);
                m_item_nums[0]++;
            }
            else{
                if (m_item_nums[1] == 0) right_init_mode = ams_info->ams_type == AMSModel::AMS_LITE ? AMSRoadShowMode::AMS_ROAD_MODE_EXTRA_AMS : AMSRoadShowMode::AMS_ROAD_MODE_FOUR;
                left = false;
                m_item_ids[1].push_back(ams_info->ams_id);
                m_item_nums[1]++;
            }
            AddAmsPreview(*ams_info, ams_info->ams_type);
            AddAms(*ams_info, left);
            //AddExtraAms(*ams_info);
        }
        else if (ams_info->cans.size() == 1){
            AddAmsPreview(*ams_info, ams_info->ams_type);
            single_info.push_back(*ams_info);
            if (single_info.size() == 2){
                if (m_item_nums[0] <= m_item_nums[1]){
                    if (m_item_nums[0] == 0) left_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
                    left = true;
                    m_item_ids[0].push_back(single_info[0].ams_id);
                    m_item_ids[0].push_back(single_info[1].ams_id);
                    m_item_nums[0]++;
                }
                else{
                    if (m_item_nums[1] == 0) right_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
                    left = false;
                    m_item_ids[1].push_back(single_info[0].ams_id);
                    m_item_ids[1].push_back(single_info[1].ams_id);
                    m_item_nums[1]++;
                }
                pair_id.push_back(std::make_pair(single_info[0].ams_id, single_info[1].ams_id));
                AddAms(single_info, left);
                single_info.clear();
            }
        }
    }
    //ext_info.cans[0].material_colour =
    if (m_ext_info.size() <= 0){
        BOOST_LOG_TRIVIAL(trace) << "vt_slot empty!";
        return;
    }
    if (m_ams_info.size() > 0 && m_ams_info[0].ams_type == AMSModel::AMS_LITE){
        for (auto ext = m_ext_info.begin(); ext != m_ext_info.end(); ext++){
            ext->ext_type = AMSModelOriginType::LITE_EXT;
        }
    }
    single_info.push_back(m_ext_info[0]);
    if (m_item_nums[0] <= m_item_nums[1]){
        if (m_item_nums[0] == 0){
            if (single_info.size() == 2){
                left_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
            }
            else{
                left_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
            }
        }
        left = true;
        for (auto it : single_info){
            m_item_ids[0].push_back(it.ams_id);
        }
        m_item_nums[0]++;
    }
    else{
        if (m_item_nums[1] == 0){
            if (single_info.size() == 2){
                right_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
            }
            else{
                right_init_mode = AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
            }
        }
        left = false;
        for (auto it : single_info){
            m_item_ids[1].push_back(it.ams_id);
        }
        m_item_nums[1]++;
    }
    //wait add
    if (single_info.size() == 2){
        pair_id.push_back(std::make_pair(single_info[0].ams_id, single_info[1].ams_id));
    }
    AddAmsPreview(m_ext_info[0], AMSModel::EXT_AMS);
    AddAms(single_info, left);

    m_panel_items_left->Hide();
    if (!m_item_nums[0] || !m_item_nums[1]){
        /*m_simplebook_ams_right->Hide();
        m_panel_items_right->Hide();*/
        //m_simplebook_ams_left->Hide();

        m_simplebook_ams_right->Hide();
        m_panel_items_right->Hide();
        m_simplebook_ams_left->SetSelection(0);
        m_down_road->UpdateLeft(1, left_init_mode);
        m_down_road->UpdateRight(1, right_init_mode);
    }
    else {
        m_sizer_items_left->Layout();
        if (m_item_nums[0] <= 1 && m_item_nums[1] <= 1){
            m_panel_items_right->Hide();
        }
        else{
            m_panel_items_right->Show();
        }
        /*m_simplebook_ams_left->Show();
        m_simplebook_ams_right->Show();*/
        m_simplebook_ams_left->Show();
        m_simplebook_ams_right->Show();

        m_simplebook_ams_left->SetSelection(0);
        m_simplebook_ams_right->SetSelection(0);
        m_down_road->UpdateLeft(1, left_init_mode);
        m_down_road->UpdateRight(1, right_init_mode);
    }

    m_current_show_ams_left = "-1";
    m_current_show_ams_right = "-1";
    for (auto it = m_ams_item_list.begin(); it != m_ams_item_list.end(); it++)
    {
        if (!IsAmsInRightPanel(it->second->m_info.ams_id) && m_current_show_ams_left == "-1"){
            m_current_show_ams_left = it->second->m_info.ams_id;
        }
        if (IsAmsInRightPanel(it->second->m_info.ams_id) && m_current_show_ams_right == "-1"){
            m_current_show_ams_right = it->second->m_info.ams_id;
        }
    }
    m_current_ams = "-1";
    m_extruder->update(1);
    auto it = m_ams_item_list.begin();
    if (it != m_ams_item_list.end()){
        m_down_road->UpdatePassRoad("0", true, -1, it->second->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    }

    if ((++it) != m_ams_item_list.end()) {
        m_down_road->UpdatePassRoad("0", false, -1, it->second->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    }

    //Refresh();
    Thaw();
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
}

void AMSControl::show_noams_mode()
{
    show_vams(true);
    //m_sizer_ams_tips->Show(true);

    /*if (m_ams_model == AMSModel::NO_AMS) {
        EnterNoneAMSMode();
    } else if(m_ams_model == AMSModel::GENERIC_AMS){
        EnterGenericAMSMode();
    } else if (m_ams_model == AMSModel::EXTRA_AMS) {
        EnterExtraAMSMode();
    }*/
    EnterGenericAMSMode();
}

void AMSControl::show_auto_refill(bool show)
{
    //m_img_ams_backup->Show(show);
    m_amswin->Layout();
    m_amswin->Fit();
}

void AMSControl::show_vams(bool show)
{
    //m_panel_virtual->Show(show);
    //m_vams_sizer->Show(show);
    //m_vams_extra_road->Show(show);
    /*m_extruder->has_ams(show);
    show_vams_kn_value(show);
    Layout();

    if (show && m_is_none_ams_mode) {
        if (m_current_ams == "") {
            wxMouseEvent event(wxEVT_LEFT_DOWN);
            event.SetEventObject(m_vams_lib);
            wxPostEvent(m_vams_lib, event);
        }
    }*/
}

void AMSControl::show_vams_kn_value(bool show)
{
    //m_vams_lib->show_kn_value(show);
}

void AMSControl::update_vams_kn_value(AmsTray tray, MachineObject* obj)
{
    //m_vams_lib->m_obj = obj;
    //if (obj->cali_version >= 0) {
    //    float k_value = 0;
    //    float n_value = 0;
    //    CalibUtils::get_pa_k_n_value_by_cali_idx(obj, tray.cali_idx, k_value, n_value);
    //    m_vams_info.k        = k_value;
    //    m_vams_info.n        = n_value;
    //    m_vams_lib->m_info.k = k_value;
    //    m_vams_lib->m_info.n = n_value;
    //}
    //else { // the remaining printer types
    //    m_vams_info.k        = tray.k;
    //    m_vams_info.n        = tray.n;
    //    m_vams_lib->m_info.k = tray.k;
    //    m_vams_lib->m_info.n = tray.n;
    //}
    //m_vams_info.material_name = tray.get_display_filament_type();
    //m_vams_info.material_colour = tray.get_color();
    //m_vams_lib->m_info.material_name = tray.get_display_filament_type();
    //auto col= tray.get_color();
    //if (col.Alpha() != 0 && col.Alpha() != 255 && col.Alpha() != 254 && m_vams_lib->m_info.material_colour != col) {
    //    m_vams_lib->transparent_changed = true;
    //}
    //m_vams_lib->m_info.material_colour = tray.get_color();
    //m_vams_lib->Refresh();
}

void AMSControl::reset_vams()
{
    /*m_vams_lib->m_info.k = 0;
    m_vams_lib->m_info.n = 0;
    m_vams_lib->m_info.material_name = wxEmptyString;
    m_vams_lib->m_info.material_colour = AMS_CONTROL_WHITE_COLOUR;
    m_vams_lib->m_info.cali_idx = -1;
    m_vams_lib->m_info.filament_id = "";
    m_vams_info.material_name = wxEmptyString;
    m_vams_info.material_colour = AMS_CONTROL_WHITE_COLOUR;
    m_vams_lib->Refresh();*/
}


void AMSControl::ReadExtInfo(MachineObject* obj) {
    m_ext_info.clear();
    if (!obj){
        return;
    }
    AMSinfo ext_info;
    for (auto slot : obj->vt_slot){
        ext_info.ams_id = slot.id;
        Caninfo can;
        can.can_id = std::to_string(0);
        can.material_name = slot.filament_setting_id;
        ext_info.cans.push_back(can);
        if (slot.id == std::to_string(VIRTUAL_TRAY_MAIN_ID)){
            ext_info.nozzle_id = 0;
        }
        else{
            ext_info.nozzle_id = 1;
        }
        ext_info.cans[0].material_state = AMSCanType::AMS_CAN_TYPE_VIRTUAL;
        ext_info.cans[0].material_colour = slot.decode_color(slot.color);
        ext_info.cans[0].material_remain = slot.remain;
        ext_info.cans[0].material_name = slot.type;

        m_ext_info.push_back(ext_info);
    }
}

void AMSControl::UpdateAms(std::vector<AMSinfo> ams_info, std::vector<AMSinfo>ext_info, std::string dev_id, bool is_reset, bool test)
{
    if (!test){
        /*std::string curr_ams_id = GetCurentAms();
        std::string curr_can_id = GetCurrentCan(curr_ams_id);*/

        int nozzle_num = ext_info.size();

        // update item
        bool fresh = false;
        if (m_ams_info.size() == ams_info.size() && m_nozzle_num == nozzle_num && m_dev_id == dev_id){
            for (int i = 0; i < m_ams_info.size(); i++){
                if (m_ams_info[i].ams_id != ams_info[i].ams_id){
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
        m_nozzle_num = nozzle_num;
        m_dev_id = dev_id;
        if (fresh){
            //m_ams_generic_item_list.clear();
            for (auto it : m_ams_preview_list){
                delete it.second;
            }
            m_ams_preview_list.clear();
            ClearAms();
            m_left_page_index = 0;
            m_right_page_index = 0;
            if (m_nozzle_num >= 2){
                CreateAmsNew();
            }else{
                /*m_panel_items_right->ClearBackground();
                m_panel_items_left->ClearBackground();*/
                m_item_ids = { {}, {} };
                pair_id.clear();
                CreateAmsSingleNozzle();
            }
            /*m_amswin->Layout();
            m_amswin->Fit();
            */
            SetSize(wxSize(FromDIP(578), -1));
            SetMinSize(wxSize(FromDIP(578), -1));


        }
        // update cans

        for (auto ams_item : m_ams_item_list) {
            std::string ams_id = ams_item.second->m_info.ams_id;
            AmsItem* cans = ams_item.second;
            if (cans->m_info.ams_id == std::to_string(VIRTUAL_TRAY_MAIN_ID) || cans->m_info.ams_id == std::to_string(VIRTUAL_TRAY_DEPUTY_ID)){
                for (auto ifo : m_ext_info) {
                    if (ifo.ams_id == ams_id) {
                        cans->m_info = ifo;
                        cans->Update(ifo);
                        cans->show_sn_value(m_ams_model == AMSModel::AMS_LITE ? false : true);
                    }
                }
            }
            else{
                for (auto ifo : m_ams_info) {
                    if (ifo.ams_id == ams_id) {
                        cans->m_info = ifo;
                        cans->Update(ifo);
                        cans->show_sn_value(m_ams_model == AMSModel::AMS_LITE ? false : true);
                    }
                }
            }
        }

        for (auto ams_prv : m_ams_preview_list) {
            std::string id = ams_prv.second->m_amsinfo.ams_id;
            auto item = m_ams_item_list.find(id);
            if (item != m_ams_item_list.end())
            {
                ams_prv.second->Update(item->second->m_info);
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
        static std::vector<AMSinfo>ams_info;
        int nozzle_num = 2;
        if (first_time)
        {
            auto caninfo0_0 = Caninfo{ "def_can_0", (""), *wxRED, AMSCanType::AMS_CAN_TYPE_VIRTUAL };
            auto caninfo0_1 = Caninfo{ "def_can_1", (""), *wxGREEN, AMSCanType::AMS_CAN_TYPE_VIRTUAL };
            auto caninfo0_2 = Caninfo{ "def_can_2", (""), *wxBLUE, AMSCanType::AMS_CAN_TYPE_VIRTUAL };
            auto caninfo0_3 = Caninfo{ "def_can_3", (""), *wxYELLOW, AMSCanType::AMS_CAN_TYPE_VIRTUAL };

            auto caninfo1_0 = Caninfo{ "def_can_0", (""), wxColour(255, 255, 0), AMSCanType::AMS_CAN_TYPE_VIRTUAL };
            auto caninfo1_1 = Caninfo{ "def_can_1", (""), wxColour(255, 0, 255), AMSCanType::AMS_CAN_TYPE_VIRTUAL };
            auto caninfo1_2 = Caninfo{ "def_can_2", (""), wxColour(0, 255, 255), AMSCanType::AMS_CAN_TYPE_VIRTUAL };
            auto caninfo1_3 = Caninfo{ "def_can_3", (""), wxColour(200, 80, 150), AMSCanType::AMS_CAN_TYPE_VIRTUAL };

            AMSinfo                        ams1 = AMSinfo{ "0", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}, 0 };
            AMSinfo                        ams2 = AMSinfo{ "1", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}, 0 };
            AMSinfo                        ams3 = AMSinfo{ "2", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}, 0 };
            AMSinfo                        ams4 = AMSinfo{ "3", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}, 0 };

            AMSinfo                        singleams1 = AMSinfo{ "0", std::vector<Caninfo>{caninfo0_0}, 0 };
            AMSinfo                        singleams2 = AMSinfo{ "1", std::vector<Caninfo>{caninfo0_1}, 0 };
            AMSinfo                        singleams3 = AMSinfo{ "2", std::vector<Caninfo>{caninfo0_2}, 0 };
            AMSinfo                        singleams4 = AMSinfo{ "3", std::vector<Caninfo>{caninfo0_3}, 0 };

            AMSinfo                        ams5 = AMSinfo{ "4", std::vector<Caninfo>{caninfo1_0, caninfo1_1, caninfo1_2, caninfo1_3}, 1 };
            AMSinfo                        ams6 = AMSinfo{ "5", std::vector<Caninfo>{caninfo1_0, caninfo1_1, caninfo1_2, caninfo1_3}, 1 };
            AMSinfo                        ams7 = AMSinfo{ "6", std::vector<Caninfo>{caninfo1_0, caninfo1_1, caninfo1_2, caninfo1_3}, 1 };
            AMSinfo                        ams8 = AMSinfo{ "7", std::vector<Caninfo>{caninfo1_0, caninfo1_1, caninfo1_2, caninfo1_3}, 1 };

            AMSinfo                        singleams5 = AMSinfo{ "4", std::vector<Caninfo>{caninfo1_0}, 1 };
            AMSinfo                        singleams6 = AMSinfo{ "5", std::vector<Caninfo>{caninfo1_1}, 1 };
            AMSinfo                        singleams7 = AMSinfo{ "6", std::vector<Caninfo>{caninfo1_2}, 1 };
            AMSinfo                        singleams8 = AMSinfo{ "7", std::vector<Caninfo>{caninfo1_3}, 1 };
            std::vector<AMSinfo>generic_ams = { ams1, ams2, ams3, ams4, ams5, ams6, ams7, ams8 };
            std::vector<AMSinfo>single_ams = { singleams1, singleams2, singleams3, singleams4, singleams5, singleams6, singleams7, singleams8 };
            ams_info = { ams1, singleams1, ams3, singleams3, ams5, singleams5, ams7, singleams7 };
            first_time = false;
        }

        Freeze();

        // update item
        bool fresh = false;
        if (m_ams_info.size() == ams_info.size() && m_nozzle_num == nozzle_num){
            for (int i = 0; i < m_ams_info.size(); i++)
            {
                if (m_ams_info[i].ams_id != ams_info[i].ams_id){
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
        m_ext_info.push_back(ext_info[0]);
        m_ext_info.push_back(ext_info[0]);
        m_ext_info[0].ams_id = std::to_string(VIRTUAL_TRAY_MAIN_ID);
        m_ext_info[1].ams_id = std::to_string(VIRTUAL_TRAY_DEPUTY_ID);
        m_nozzle_num = nozzle_num;
        if (fresh){

            for (auto it : m_ams_preview_list) {
                delete it.second;
            }
            m_ams_preview_list.clear();
            ClearAms();
            m_left_page_index = 0;
            m_right_page_index = 0;
            if (m_nozzle_num >= 2) {
                CreateAmsNew();
            }
            else {
                /*m_panel_items_right->ClearBackground();
                m_panel_items_left->ClearBackground();*/
                m_item_ids = { {}, {} };
                pair_id.clear();
                CreateAmsSingleNozzle();
            }
            /*m_amswin->Layout();
            m_amswin->Fit();
            */
            SetSize(wxSize(FromDIP(578), -1));
            SetMinSize(wxSize(FromDIP(578), -1));
        }
        Thaw();

        // update cans

        for (auto ams_item : m_ams_item_list) {
            std::string ams_id = ams_item.first;
            AmsItem* cans = ams_item.second;
            if (atoi(cans->m_info.ams_id.c_str()) >= VIRTUAL_TRAY_DEPUTY_ID) {
                for (auto ifo : m_ext_info) {
                    if (ifo.ams_id == ams_id) {
                        cans->m_info = ifo;
                        cans->Update(ifo);
                        cans->show_sn_value(m_ams_model == AMSModel::AMS_LITE ? false : true);
                    }
                }
            }
            else {
                for (auto ifo : m_ams_info) {
                    if (ifo.ams_id == ams_id) {
                        cans->m_info = ifo;
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


}

void AMSControl::AddAmsPreview(AMSinfo info, AMSModel type)
{
    AMSPreview *ams_prv = nullptr;

    if (info.nozzle_id == 0)
    {
        ams_prv = new AMSPreview(m_panel_items_right, wxID_ANY, info, type);
        m_sizer_items_right->Add(ams_prv, 0, wxALIGN_CENTER | wxRIGHT, 6);
    }
    else if (info.nozzle_id == 1)
    {
        ams_prv = new AMSPreview(m_panel_items_left, wxID_ANY, info, type);
        m_sizer_items_left->Add(ams_prv, 0, wxALIGN_CENTER | wxRIGHT, 6);
    }

    if (ams_prv){
        ams_prv->Bind(wxEVT_LEFT_DOWN, [this, ams_prv](wxMouseEvent &e) {
            SwitchAms(ams_prv->m_amsinfo.ams_id);
            e.Skip();
        });
        m_ams_preview_list[info.ams_id] = ams_prv;
    }
}

void AMSControl::AddAms(AMSinfo info, bool left)
{
    AmsItem* ams_item;
    if (m_nozzle_num > 1)
    {
        if (info.nozzle_id == 0)
        {
            ams_item = new AmsItem(m_simplebook_ams_right, info, info.ams_type, AMSPanelPos::RIGHT_PANEL);
            //m_simplebook_ams_right->RemovePage(m_right_page_index);
            m_simplebook_ams_right->InsertPage(m_right_page_index, ams_item, wxEmptyString, true);
            ams_item->m_selection = m_right_page_index;
            m_right_page_index++;
        }
        else if (info.nozzle_id == 1)
        {
            ams_item = new AmsItem(m_simplebook_ams_left, info, info.ams_type, AMSPanelPos::LEFT_PANEL);
            //m_simplebook_ams_left->RemovePage(m_left_page_index);
            m_simplebook_ams_left->InsertPage(m_left_page_index, ams_item, wxEmptyString, true);
            ams_item->m_selection = m_left_page_index;
            m_left_page_index++;
        }
    }
    else if (m_nozzle_num == 1)
    {
        if (left)
        {
            ams_item = new AmsItem(m_simplebook_ams_left, info, info.ams_type, AMSPanelPos::LEFT_PANEL);
            //m_simplebook_ams_left->RemovePage(m_left_page_index);
            m_simplebook_ams_left->InsertPage(m_left_page_index, ams_item, wxEmptyString, true);
            ams_item->m_selection = m_left_page_index;
            m_left_page_index++;
        }
        else {
            ams_item = new AmsItem(m_simplebook_ams_right, info, info.ams_type, AMSPanelPos::RIGHT_PANEL);
            //m_simplebook_ams_right->RemovePage(m_right_page_index);
            m_simplebook_ams_right->InsertPage(m_right_page_index, ams_item, wxEmptyString, true);
            ams_item->m_selection = m_right_page_index;
            m_right_page_index++;
            //if (m_item_nums[1] == 1) m_simplebook_ams_right->SetSelection(m_simplebook_ams_left->GetSelection());
        }
    }
    m_ams_item_list[info.ams_id] = ams_item;
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

void AMSControl::AddAms(std::vector<AMSinfo>single_info, bool left) {
    AmsItem* ams_item;
    AMSModel mode;
    wxBoxSizer *sizer = new wxBoxSizer(wxHORIZONTAL);

    if (single_info.size() == 0){
        return;
    }
    else if (single_info.size() == 1){
        mode = AMSModel::EXT_AMS;
        int w = 30;
        if (m_nozzle_num == 2)
        {
            if (single_info[0].nozzle_id == 0)
            {
                wxPanel*  book_panel = new wxPanel(m_simplebook_ams_right);
                book_panel->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);
                ams_item = new AmsItem(book_panel, single_info[0], mode, AMSPanelPos::RIGHT_PANEL);
                sizer->Add(ams_item, 0, wxLEFT, FromDIP(30));
                book_panel->SetSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetMinSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetSizer(sizer);
                book_panel->Layout();
                book_panel->Fit();
                //m_simplebook_ams_right->RemovePage(m_right_page_index);
                m_simplebook_ams_right->InsertPage(m_right_page_index, book_panel, wxEmptyString, true);
                ams_item->m_selection = m_right_page_index;
                m_right_page_index++;
            }
            else if (single_info[0].nozzle_id == 1)
            {
                wxPanel*  book_panel = new wxPanel(m_simplebook_ams_left);
                book_panel->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);
                ams_item = new AmsItem(book_panel, single_info[0], mode, AMSPanelPos::LEFT_PANEL);
                sizer->Add(ams_item, 0, wxLEFT, FromDIP(30));
                book_panel->SetSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetMinSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetSizer(sizer);
                book_panel->Layout();
                book_panel->Fit();
                //m_simplebook_ams_left->RemovePage(m_left_page_index);
                m_simplebook_ams_left->InsertPage(m_left_page_index, book_panel, wxEmptyString, true);
                ams_item->m_selection = m_left_page_index;
                m_left_page_index++;
            }
        }
        else if (m_nozzle_num == 1){
            if (!left){
                wxPanel*  book_panel = new wxPanel(m_simplebook_ams_right);
                book_panel->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);
                ams_item = new AmsItem(book_panel, single_info[0], mode, AMSPanelPos::RIGHT_PANEL);
                sizer->Add(ams_item, 0, wxLEFT, FromDIP(30));
                book_panel->SetSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetMinSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetSizer(sizer);
                book_panel->Layout();
                book_panel->Fit();
                //m_simplebook_ams_right->RemovePage(m_right_page_index);
                m_simplebook_ams_right->InsertPage(m_right_page_index, book_panel, wxEmptyString, true);
                ams_item->m_selection = m_right_page_index;
                m_right_page_index++;
            }
            else
            {
                wxPanel* book_panel = new wxPanel(m_simplebook_ams_left);
                book_panel->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);
                ams_item = new AmsItem(book_panel, single_info[0], mode, AMSPanelPos::LEFT_PANEL);
                sizer->Add(ams_item, 0, wxLEFT, FromDIP(30));
                book_panel->SetSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetMinSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetSizer(sizer);
                book_panel->Layout();
                book_panel->Fit();
                //m_simplebook_ams_left->RemovePage(m_left_page_index);
                m_simplebook_ams_left->InsertPage(m_left_page_index, book_panel, wxEmptyString, true);
                ams_item->m_selection = m_left_page_index;
                m_left_page_index++;
            }
        }

        ams_item->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);
        m_ams_item_list[single_info[0].ams_id] = ams_item;
    }
    else if (single_info.size() == 2)
    {
        AmsItem* ext_item;
        wxBoxSizer* book_sizer = new wxBoxSizer(wxVERTICAL);
        if (single_info[1].ams_id == std::to_string(VIRTUAL_TRAY_MAIN_ID) || single_info[1].ams_id == std::to_string(VIRTUAL_TRAY_DEPUTY_ID))
        {
            mode = AMSModel::EXT_AMS;
        }
        else
        {
            mode = AMSModel::N3S_AMS;
        }
        if (m_nozzle_num == 2)
        {
            if (single_info[1].nozzle_id == 0)
            {
                wxPanel* book_panel = new wxPanel(m_simplebook_ams_right);
                book_panel->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);
                book_panel->SetSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetMinSize(wxSize(FromDIP(264), FromDIP(150)));
                ams_item = new AmsItem(book_panel, single_info[0], AMSModel::N3S_AMS, AMSPanelPos::RIGHT_PANEL);
                ext_item = new AmsItem(book_panel, single_info[1], mode, AMSPanelPos::RIGHT_PANEL);
                book_sizer->Add(ams_item);
                book_sizer->Add(ext_item);
                book_panel->SetSizer(book_sizer);
                book_panel->Layout();
                book_panel->Fit();
                //m_simplebook_ams_right->RemovePage(m_right_page_index);
                m_simplebook_ams_right->InsertPage(m_right_page_index, book_panel, wxEmptyString, true);
                ams_item->m_selection = m_right_page_index;
                ext_item->m_selection = m_right_page_index;
                m_right_page_index++;
            }
            else if (single_info[1].nozzle_id == 1)
            {
                wxPanel* book_panel = new wxPanel(m_simplebook_ams_left);
                book_panel->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);
                book_panel->SetSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetMinSize(wxSize(FromDIP(264), FromDIP(150)));
                ams_item = new AmsItem(book_panel, single_info[0], AMSModel::N3S_AMS, AMSPanelPos::LEFT_PANEL);
                ext_item = new AmsItem(book_panel, single_info[1], mode, AMSPanelPos::LEFT_PANEL);
                book_sizer->Add(ams_item);
                book_sizer->Add(ext_item);
                book_panel->SetSizer(book_sizer);
                //m_simplebook_ams_left->RemovePage(m_left_page_index);
                m_simplebook_ams_left->InsertPage(m_left_page_index, book_panel, wxEmptyString, true);
                ams_item->m_selection = m_left_page_index;
                ext_item->m_selection = m_left_page_index;
                m_left_page_index++;
            }
        }
        else {
            if (!left)
            {
                wxPanel* book_panel = new wxPanel(m_simplebook_ams_right);
                book_panel->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);
                book_panel->SetSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetMinSize(wxSize(FromDIP(264), FromDIP(150)));
                ams_item = new AmsItem(book_panel, single_info[0], AMSModel::N3S_AMS, AMSPanelPos::RIGHT_PANEL);
                ext_item = new AmsItem(book_panel, single_info[1], mode, AMSPanelPos::RIGHT_PANEL);
                book_sizer->Add(ams_item);
                book_sizer->Add(ext_item);
                book_panel->SetSizer(book_sizer);
                book_panel->Layout();
                book_panel->Fit();
                //m_simplebook_ams_right->RemovePage(m_right_page_index);
                m_simplebook_ams_right->InsertPage(m_right_page_index, book_panel, wxEmptyString, true);
                ams_item->m_selection = m_right_page_index;
                ext_item->m_selection = m_right_page_index;
                m_right_page_index++;
            }
            else
            {
                wxPanel* book_panel = new wxPanel(m_simplebook_ams_left);
                book_panel->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);
                book_panel->SetSize(wxSize(FromDIP(264), FromDIP(150)));
                book_panel->SetMinSize(wxSize(FromDIP(264), FromDIP(150)));
                ams_item = new AmsItem(book_panel, single_info[0], AMSModel::N3S_AMS, AMSPanelPos::LEFT_PANEL);
                ext_item = new AmsItem(book_panel, single_info[1], mode, AMSPanelPos::LEFT_PANEL);
                book_sizer->Add(ams_item);
                book_sizer->Add(ext_item);
                book_panel->SetSizer(book_sizer);
                //m_simplebook_ams_left->RemovePage(m_left_page_index);
                m_simplebook_ams_left->InsertPage(m_left_page_index, book_panel, wxEmptyString, true);
                ams_item->m_selection = m_left_page_index;
                ext_item->m_selection = m_left_page_index;
                m_left_page_index++;
            }
        }

        m_ams_item_list[single_info[0].ams_id] = ams_item;
        m_ams_item_list[single_info[1].ams_id] = ext_item;
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

void AMSControl::AddAmsPreview(std::vector<AMSinfo>single_info) {

}

void AMSControl::SwitchAms(std::string ams_id)
{
    if(ams_id == m_current_show_ams_left || ams_id == m_current_show_ams_right){return;}

    bool is_in_right = IsAmsInRightPanel(ams_id);
    if (is_in_right){
        if (m_current_show_ams_right != ams_id) {
            m_current_show_ams_right = ams_id;
            //m_extruder->OnAmsLoading(false);
        }
    }
    else{
        m_current_show_ams_left = ams_id;
        /*if (m_nozzle_num > 1) m_extruder->OnAmsLoading(false, 1);
        else m_extruder->OnAmsLoading(false);*/
    }


    for (auto prv_it : m_ams_preview_list) {
        AMSPreview* prv = prv_it.second;
        if (prv->m_amsinfo.ams_id == m_current_show_ams_left || prv->m_amsinfo.ams_id == m_current_show_ams_right) {
            prv->OnSelected();
            m_current_select = ams_id;

            bool ready_selected = false;
            for (auto item_it : m_ams_item_list) {
                AmsItem* item = item_it.second;
                if (item->m_info.ams_id == ams_id) {
                    for (auto lib_it : item->m_can_lib_list) {
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

        if (prv->m_amsinfo.nozzle_id == 1) {
            m_sizer_items_left->Layout();
            m_panel_items_left->Fit();
        }
        else if (prv->m_amsinfo.nozzle_id == 0)
        {
            m_sizer_items_right->Layout();
            m_panel_items_right->Fit();
        }

    }

    for (auto ams_item : m_ams_item_list) {
        AmsItem* item = ams_item.second;
        if (item->m_info.ams_id == ams_id) {
            if (m_nozzle_num == 2) {
                if (item->m_info.nozzle_id == 1){
                    m_simplebook_ams_left->SetSelection(item->m_selection);
                    if (item->m_info.cans.size() == 4){
                        if (item->m_info.ams_type == AMSModel::AMS_LITE){
                            m_down_road->UpdateLeft(m_nozzle_num, AMSRoadShowMode::AMS_ROAD_MODE_EXTRA_AMS);
                        }
                        else{
                            m_down_road->UpdateLeft(m_nozzle_num, AMSRoadShowMode::AMS_ROAD_MODE_FOUR);
                        }

                    }
                    else {
                        AMSRoadShowMode mode = AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
                        for (auto it : pair_id){
                            if (it.first == ams_id || it.second == ams_id){
                                mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
                                break;
                            }
                        }
                        m_down_road->UpdateLeft(m_nozzle_num, mode);
                        m_down_road->UpdatePassRoad(item->m_info.current_can_id, true, -1, item->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
                    }
                }
                else if (item->m_info.nozzle_id == 0){
                    m_simplebook_ams_right->SetSelection(item->m_selection);
                    if (item->m_info.cans.size() == 4){
                        if (item->m_info.ams_type == AMSModel::AMS_LITE){
                            m_down_road->UpdateRight(m_nozzle_num, AMSRoadShowMode::AMS_ROAD_MODE_EXTRA_AMS);
                        }
                        else{
                            m_down_road->UpdateRight(m_nozzle_num, AMSRoadShowMode::AMS_ROAD_MODE_FOUR);
                        }

                    }
                    else {
                        AMSRoadShowMode mode = AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
                        for (auto it : pair_id){
                            if (it.first == ams_id || it.second == ams_id){
                                mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
                                break;
                            }
                        }
                        m_down_road->UpdateRight(m_nozzle_num, mode);
                        m_down_road->UpdatePassRoad(item->m_info.current_can_id, false, -1, item->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
                    }
                }

            }
            else if (m_nozzle_num == 1) {
                for (auto id : m_item_ids[0]){
                    if (id == item->m_info.ams_id){
                        m_simplebook_ams_left->SetSelection(item->m_selection);
                        if (item->m_info.cans.size() == 4){
                            if (item->m_info.ams_type == AMSModel::AMS_LITE){
                                m_down_road->UpdateLeft(m_nozzle_num, AMSRoadShowMode::AMS_ROAD_MODE_EXTRA_AMS);
                            }
                            else{
                                m_down_road->UpdateLeft(m_nozzle_num, AMSRoadShowMode::AMS_ROAD_MODE_FOUR);
                            }

                        }
                        else {
                            AMSRoadShowMode mode = AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
                            for (auto it : pair_id){
                                if (it.first == ams_id || it.second == ams_id){
                                    mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
                                    break;
                                }
                            }
                            m_down_road->UpdateLeft(m_nozzle_num, mode);
                            m_down_road->UpdatePassRoad(item->m_info.current_can_id, true, -1, item->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
                        }
                    }
                }
                for (auto id : m_item_ids[1])
                {
                    if (id == item->m_info.ams_id){
                        m_simplebook_ams_right->SetSelection(item->m_selection);
                        if (item->m_info.cans.size() == 4){
                            if (item->m_info.ams_type == AMSModel::AMS_LITE){
                                m_down_road->UpdateRight(m_nozzle_num, AMSRoadShowMode::AMS_ROAD_MODE_EXTRA_AMS);
                            }
                            else{
                                m_down_road->UpdateRight(m_nozzle_num, AMSRoadShowMode::AMS_ROAD_MODE_FOUR);
                            }

                        }
                        else {
                            AMSRoadShowMode mode = AMSRoadShowMode::AMS_ROAD_MODE_SINGLE;
                            for (auto it : pair_id)
                            {
                                if (it.first == ams_id || it.second == ams_id){
                                    mode = AMSRoadShowMode::AMS_ROAD_MODE_DOUBLE;
                                    break;
                                }
                            }
                            m_down_road->UpdateRight(m_nozzle_num, mode);
                            m_down_road->UpdatePassRoad(item->m_info.current_can_id, false, -1, item->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
                        }
                    }
                }
            }
        }
    }

    //update extruder
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == ams_id) {
            switch (m_ams_info[i].current_step) {
            case AMSPassRoadSTEP::AMS_ROAD_STEP_NONE: m_extruder->TurnOff(); break;

            case AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1: m_extruder->TurnOff(); break;

            case AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2: m_extruder->TurnOn(GetCanColour(ams_id, m_ams_info[i].current_can_id)); break;

            case AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3: m_extruder->TurnOn(GetCanColour(ams_id, m_ams_info[i].current_can_id)); break;
            }
            //SetAmsStep(ams_id, m_ams_info[i].current_can_id, AMSPassRoadType::AMS_ROAD_TYPE_LOAD, m_ams_info[i].current_step);
        }
    }
    for (auto i = 0; i < m_ext_info.size(); i++) {
        if (m_ext_info[i].ams_id == ams_id) {
            //SetAmsStep(ams_id, "0", AMSPassRoadType::AMS_ROAD_TYPE_LOAD, m_ext_info[i].current_step);
        }
    }
}

void AMSControl::SetFilamentStep(int item_idx, FilamentStepType f_type)
{/*
    wxString FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_COUNT] = {
        _L("Idling..."),
        _L("Heat the nozzle"),
        _L("Cut filament"),
        _L("Pull back current filament"),
        _L("Push new filament into extruder"),
        _L("Purge old filament"),
        _L("Feed Filament"),
        _L("Confirm extruded"),
        _L("Check filament location")
    };


    if (item_idx == FilamentStep::STEP_IDLE) {
        m_simplebook_right->SetSelection(0);
        m_filament_load_step->Idle();
        m_filament_unload_step->Idle();
        m_filament_vt_load_step->Idle();
        return;
    }

    wxString step_str = wxEmptyString;
    if (item_idx < FilamentStep::STEP_COUNT) {
        step_str = FILAMENT_CHANGE_STEP_STRING[item_idx];
    }

    if (f_type == FilamentStepType::STEP_TYPE_LOAD) {
        if (item_idx > 0 && item_idx < FilamentStep::STEP_COUNT) {
            if (m_simplebook_right->GetSelection() != 1) {
                m_simplebook_right->SetSelection(1);
            }

            m_filament_load_step->SelectItem( m_filament_load_step->GetItemUseText(step_str) );
        } else {
            m_filament_load_step->Idle();
        }
    } else if (f_type == FilamentStepType::STEP_TYPE_UNLOAD) {
        if (item_idx > 0 && item_idx < FilamentStep::STEP_COUNT) {
            if (m_simplebook_right->GetSelection() != 2) {
                m_simplebook_right->SetSelection(2);
            }
            m_filament_unload_step->SelectItem( m_filament_unload_step->GetItemUseText(step_str) );
        }
        else {
            m_filament_unload_step->Idle();
        }
    } else if (f_type == FilamentStepType::STEP_TYPE_VT_LOAD) {
        m_simplebook_right->SetSelection(3);
        if (item_idx > 0 && item_idx < FilamentStep::STEP_COUNT) {
            if (item_idx == STEP_CONFIRM_EXTRUDED) {
                m_filament_vt_load_step->SelectItem(2);
            }
            else {
                m_filament_vt_load_step->SelectItem( m_filament_vt_load_step->GetItemUseText(step_str) );
            }
        }
        else {
            m_filament_vt_load_step->Idle();
        }
    } else {
        if (item_idx > 0 && item_idx < FilamentStep::STEP_COUNT) {
            m_simplebook_right->SetSelection(1);
            m_filament_load_step->SelectItem( m_filament_load_step->GetItemUseText(step_str) );
        }
        else {
            m_filament_load_step->Idle();
        }
    }*/
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

void AMSControl::SetExtruder(bool on_off, bool is_vams, std::string ams_now, wxColour col)
{
    AmsItem* item = nullptr;
    if (m_ams_item_list.find(ams_now) != m_ams_item_list.end()){
        item = m_ams_item_list[ams_now];
    }
    if (m_ams_model == AMSModel::GENERIC_AMS || m_ext_model == AMSModel::GENERIC_AMS ) {
        if (!on_off) {
            m_extruder->TurnOff();
            m_vams_extra_road->OnVamsLoading(false);
            m_extruder->OnVamsLoading(false);
            m_vams_road->OnVamsLoading(false);
        }
        else {
            m_extruder->TurnOn(col);
            if (item){
                /*if (ams_now != GetCurentShowAms()) {
                    m_extruder->OnAmsLoading(false, item->m_info.nozzle_id, col);
                }
                else {
                    m_extruder->OnAmsLoading(true, item->m_info.nozzle_id, col);
                }*/
            }
        }

        /*if (is_vams && on_off) {
            m_extruder->OnAmsLoading(false);
            m_vams_extra_road->OnVamsLoading(true, col);
            m_extruder->OnVamsLoading(true, col);
            m_vams_road->OnVamsLoading(true, col);
        }
        else {
            m_vams_extra_road->OnVamsLoading(false);
            m_extruder->OnVamsLoading(false);
            m_vams_road->OnVamsLoading(false);
        }*/
    }
    else if (m_ams_model == AMSModel::AMS_LITE || m_ext_model == AMSModel::AMS_LITE) {
        if (!on_off) {
            m_extruder->TurnOff();
            m_extruder->OnAmsLoading(false);
            /*m_vams_extra_road->OnVamsLoading(false);
            m_vams_road->OnVamsLoading(false);*/
        }
        else {

            if (item) {
                m_extruder->TurnOn(col);
                m_extruder->OnAmsLoading(true, item->m_info.nozzle_id, col);
            }
        }
    }
}

//void AMSControl::SetAmsStep(std::string ams_id, std::string canid, AMSPassRoadType type, AMSPassRoadSTEP step)
//{
//    AmsItem *cans = nullptr;
//    auto cansit = m_ams_item_list.find(ams_id);
//    bool           notfound = true;
//
//
//    if (cansit != m_ams_item_list.end()) {
//        cans =  cansit->second;
//    }
//    else {
//        notfound = false;
//    }
//
//
//
//    if (ams_id != m_last_ams_id || m_last_tray_id != canid) {
//        SetAmsStep(m_last_ams_id, m_last_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
//        //m_down_road->UpdatePassRoad(m_last_ams_id, m_last_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
//        m_vams_extra_road->OnVamsLoading(false);
//        m_extruder->OnVamsLoading(false);
//        m_vams_road->OnVamsLoading(false);
//    }
//
//    if (notfound) return;
//    if (cans == nullptr) return;
//
//
//    m_last_ams_id = ams_id;
//    m_last_tray_id = canid;
//
//
//    if (m_ams_model == AMSModel::GENERIC_AMS) {
//        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
//            //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
//            m_extruder->OnAmsLoading(false);
//        }
//
//        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1) {
//            //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
//            m_extruder->OnAmsLoading(false);
//        }
//
//        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2) {
//            //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
//            //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
//            if (m_current_show_ams == ams_id) {
//                m_extruder->OnAmsLoading(true, cans->GetTagColr(canid));
//            }
//        }
//
//        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3) {
//            //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
//            //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
//            //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
//            m_extruder->OnAmsLoading(true, cans->GetTagColr(canid));
//        }
//    }
//    else if (m_ams_model == AMSModel::EXTRA_AMS) {
//        //cans->SetAmsStepExtra(canid, type, step);
//        if (step != AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
//            m_extruder->OnAmsLoading(true, cans->GetTagColr(canid));
//        }
//        else {
//            m_extruder->OnAmsLoading(false);
//        }
//    }
//
//    for (auto i = 0; i < m_ams_info.size(); i++) {
//        if (m_ams_info[i].ams_id == ams_id) {
//            m_ams_info[i].current_step   = step;
//            m_ams_info[i].current_can_id = canid;
//        }
//    }
//}


void AMSControl::SetAmsStep(std::string ams_id, std::string canid, AMSPassRoadType type, AMSPassRoadSTEP step)
{
    AmsItem* ams = nullptr;
    auto amsit = m_ams_item_list.find(ams_id);
    bool           notfound = false;

    if (amsit != m_ams_item_list.end()) {
        ams = amsit->second;
    }
    else {
        notfound = true;
    }

    //if (ams_id != m_last_ams_id || m_last_tray_id != canid) {
    //    m_down_road->UpdatePassRoad(m_last_ams_id, true, -1, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    //    m_down_road->UpdatePassRoad(m_last_ams_id, false, -1, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    //    //m_down_road->UpdatePassRoad(m_last_ams_id, m_last_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    //    //m_vams_extra_road->OnVamsLoading(false);
    //    m_extruder->OnVamsLoading(false);
    //    //m_vams_road->OnVamsLoading(false);
    //}

    if (notfound) return;
    if (ams == nullptr) return;

    m_last_ams_id = ams_id;
    m_last_tray_id = canid;
    int can_index = atoi(canid.c_str());
    std::string ams_id_left = GetCurentShowAms(false);
    std::string ams_id_right = GetCurentShowAms(true);
    auto model = AMSModel::AMS_LITE;

    bool left = !IsAmsInRightPanel(ams_id);

    int length = -1;

    if (ams->m_info.cans.size() == 4){
        length = left ? 134 : 150;
        model = ams->m_info.ams_type;
    }
    else if (ams->m_info.cans.size() == 1){
        for (auto it : pair_id){
            if (it.first == ams_id){
                length = left ? 150 : 50;
                break;
            }
            else if (it.second == ams_id){
                length = left ? 50 : 150;
                break;
            }
        }
        model = AMSModel::N3S_AMS;
    }
    if (model == AMSModel::AMS_LITE){
        length = left ? 150 : 50;
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
    if (m_ams_item_list[ams_id] != nullptr) {
        info = m_ams_item_list[ams_id]->m_info;
    }
    else
    {
        return;
    }
    if (can_index >= 0 && can_index < info.cans.size())
    {
        m_down_road->SetPassRoadColour(left, info.cans[can_index].material_colour);
    }

    if (model == AMSModel::GENERIC_AMS || model == AMSModel::N3F_AMS || model == AMSModel::AMS_LITE) {
        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
            //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
            ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
            if (ams_id_left == ams_id || ams_id_right == ams_id){
                m_down_road->UpdatePassRoad(canid, left, -1, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
                m_extruder->OnAmsLoading(false, ams->m_info.nozzle_id);
            }
        }

        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1) {
            ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
            if (ams_id_left == ams_id || ams_id_right == ams_id){
                m_down_road->UpdatePassRoad(canid, left, length, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
                m_extruder->OnAmsLoading(false, ams->m_info.nozzle_id);
            }
            else
            {
                m_down_road->UpdatePassRoad(canid, left, -1, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
            }
        }

        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2) {
            ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
            if (ams_id_left == ams_id || ams_id_right == ams_id) {
                m_down_road->UpdatePassRoad(canid, left, length, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
                m_extruder->OnAmsLoading(true, ams->m_info.nozzle_id, ams->GetTagColr(canid));
            }
            else
            {
                m_down_road->UpdatePassRoad(canid, left, -1, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
            }
        }
        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3) {
            ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
            if (ams_id_left == ams_id || ams_id_right == ams_id)
            {
                m_down_road->UpdatePassRoad(canid, left, length, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
                m_extruder->OnAmsLoading(true, ams->m_info.nozzle_id, ams->GetTagColr(canid));
            }
            else
            {
                m_down_road->UpdatePassRoad(canid, left, -1, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
            }
        }
    }
    else if(model == AMSModel::EXT_AMS || model == AMSModel::N3S_AMS) {
        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
            //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
            ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
            if (ams_id_left == ams_id || ams_id_right == ams_id) {
                m_down_road->UpdatePassRoad(canid, left, -1, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
                m_extruder->OnAmsLoading(false);
            }
            else
            {
                m_down_road->UpdatePassRoad(canid, left, -1, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
            }
        }

        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1) {
            ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
            if (ams_id_left == ams_id || ams_id_right == ams_id) {
                m_down_road->UpdatePassRoad(canid, left, length, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
                m_extruder->OnAmsLoading(false);
            }
            else
            {
                m_down_road->UpdatePassRoad(canid, left, -1, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
            }
        }

        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2) {
            m_down_road->UpdatePassRoad(canid, left, length, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
            if (ams_id_left == ams_id || ams_id_right == ams_id) {
                ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
                m_extruder->OnAmsLoading(true, ams->m_info.nozzle_id, ams->GetTagColr(canid));
            }
            else
            {
                m_down_road->UpdatePassRoad(canid, left, -1, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
            }
        }

        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3) {
            ams->SetAmsStep(ams_id, canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
            if (ams_id_left == ams_id || ams_id_right == ams_id) {
                m_down_road->UpdatePassRoad(canid, left, length, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
                m_extruder->OnAmsLoading(true, ams->m_info.nozzle_id, ams->GetTagColr(canid));
            }
            else
            {
                m_down_road->UpdatePassRoad(canid, left, -1, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
            }
        }
    }

    //if (m_ams_model == AMSModel::GENERIC_AMS) {
    //    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
    //        //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    //        m_down_road->UpdatePassRoad(canid, true, -1, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    //        m_down_road->UpdatePassRoad(canid, false, -1, ams->m_info, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    //        m_extruder->OnAmsLoading(false);
    //    }

    //    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1) {
    //        //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
    //        m_extruder->OnAmsLoading(false);
    //    }

    //    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2) {
    //        //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
    //        //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
    //        if (m_current_show_ams == ams_id) {
    //            m_extruder->OnAmsLoading(true, cans->GetTagColr(canid));
    //        }
    //    }

    //    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3) {
    //        //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
    //        //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
    //        //cans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
    //        m_extruder->OnAmsLoading(true, cans->GetTagColr(canid));
    //    }
    //}
    //else if (m_ams_model == AMSModel::EXTRA_AMS) {
    //    //cans->SetAmsStepExtra(canid, type, step);
    //    if (step != AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
    //        m_extruder->OnAmsLoading(true, cans->GetTagColr(canid));
    //    }
    //    else {
    //        m_extruder->OnAmsLoading(false);
    //    }
    //}

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
