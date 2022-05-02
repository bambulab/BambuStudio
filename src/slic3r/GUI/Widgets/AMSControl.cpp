#include "AMSControl.hpp"
#include "Label.hpp"
#include "../BitmapCache.hpp"
#include "../I18N.hpp"

#include <wx/simplebook.h>
#include <wx/dcgraph.h>

namespace Slic3r { namespace GUI {

static const int LOAD_STEP_COUNT   = 4;
static const int UNLOAD_STEP_COUNT = 3;

static wxString FILAMENT_LOAD_STEP_STRING[LOAD_STEP_COUNT] = {_L("Heat the nozzle to target \ntemperature"), _L("Cut filament"), _L("Pull back current filament"),
                                                              _L("Push new filament \ninto extruder")};

static wxString FILAMENT_UNLOAD_STEP_STRING[UNLOAD_STEP_COUNT] = {_L("Heat the nozzle to target \ntemperature"), _L("Cut filament"), _L("Pull back current filament")};

wxDEFINE_EVENT(EVT_AMS_LOAD, SimpleEvent);
wxDEFINE_EVENT(EVT_AMS_UNLOAD, SimpleEvent);
wxDEFINE_EVENT(EVT_AMS_SETTINGS, SimpleEvent);
wxDEFINE_EVENT(EVT_AMS_REFRESH_RFID, wxCommandEvent);
wxDEFINE_EVENT(EVT_AMS_ON_SELECTED, wxCommandEvent);
wxDEFINE_EVENT(EVT_AMS_ON_FILAMENT_EDIT, wxCommandEvent);
wxDEFINE_EVENT(EVT_AMS_CLIBRATION_AGAIN, wxCommandEvent);
wxDEFINE_EVENT(EVT_AMS_CLIBRATION_CANCEL, wxCommandEvent);

inline int hex_digit_to_int(const char c)
{
    return (c >= '0' && c <= '9') ? int(c - '0') : (c >= 'A' && c <= 'F') ? int(c - 'A') + 10 : (c >= 'a' && c <= 'f') ? int(c - 'a') + 10 : -1;
}

static wxColour decode_color(const std::string &color)
{
    std::array<int, 3> ret = {0, 0, 0};
    const char *       c   = color.data() + 1;
    if (color.size() == 8) {
        for (size_t j = 0; j < 3; ++j) {
            int digit1 = hex_digit_to_int(*c++);
            int digit2 = hex_digit_to_int(*c++);
            if (digit1 == -1 || digit2 == -1) break;
            ret[j] = float(digit1 * 16 + digit2);
        }
    }
    return wxColour(ret[0], ret[1], ret[2]);
}

bool AMSinfo::parse_ams_info(Ams *ams)
{
    if (!ams) return false;
    this->ams_id = ams->id;
    cans.clear();
    for (int i = 0; i < 4; i++) {
        auto    it = ams->trayList.find(std::to_string(i));
        Caninfo info;
        // tray is exists
        if (it != ams->trayList.end() && it->second->is_exists) {
            info.can_id        = it->second->id;
            info.material_name = it->second->type;
            if (!it->second->color.empty()) {
                info.material_colour = AmsTray::decode_color(it->second->color);
            } else {
                // set to white by default
                info.material_colour = wxColour(255, 255, 255);
            }

            if (MachineObject::is_bbl_filament(it->second->tag_uid)) {
                info.material_state = AMSCanType::AMS_CAN_TYPE_BRAND;
            } else {
                info.material_state = AMSCanType::AMS_CAN_TYPE_THIRDBRAND;
            }
        } else {
            info.can_id         = i;
            info.material_state = AMSCanType::AMS_CAN_TYPE_NONE;
        }
        cans.push_back(info);
    }
    return true;
}

/*************************************************
Description:AMSrefresh
**************************************************/

AMSrefresh::AMSrefresh() { SetFont(Label::Body_10); }

AMSrefresh::AMSrefresh(wxWindow *parent, wxWindowID id, wxString number, Caninfo info, const wxPoint &pos, const wxSize &size) : AMSrefresh()
{
    m_info = info;
    m_text = number;
    create(parent, id, pos, size);
}

AMSrefresh::AMSrefresh(wxWindow *parent, wxWindowID id, int number, Caninfo info, const wxPoint &pos, const wxSize &size) : AMSrefresh()
{
    m_info = info;
    m_text = wxString::Format("%d", number);
    create(parent, id, pos, size);
}

void AMSrefresh::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    wxWindow::Create(parent, id, pos, size, wxBORDER_NONE);
    SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);

    Bind(wxEVT_PAINT, &AMSrefresh::paintEvent, this);
    Bind(wxEVT_ENTER_WINDOW, &AMSrefresh::OnEnterWindow, this);
    Bind(wxEVT_LEAVE_WINDOW, &AMSrefresh::OnLeaveWindow, this);
    Bind(wxEVT_LEFT_DOWN, &AMSrefresh::OnClick, this);

    m_bitmap_normal   = create_scaled_bitmap("ams_refresh_normal", this, AMS_REFRESH_SIZE.x);
    m_bitmap_selected = create_scaled_bitmap("ams_refresh_selected", this, AMS_REFRESH_SIZE.x);

    m_animationCtrl = new wxAnimationCtrl(this, wxID_ANY, wxNullAnimation, wxPoint(0, 0), AMS_REFRESH_SIZE, wxAC_NO_AUTORESIZE);

    auto path       = (boost::format("%1%/images/refresh.gif") % resources_dir()).str();
    if (m_animationCtrl->LoadFile(path)) m_animationCtrl->Hide();


    SetSize(m_bitmap_normal.GetSize());
    SetMinSize(m_bitmap_normal.GetSize());
}

void AMSrefresh::PlayLoading()
{
    this->m_animationCtrl->Show();
    this->m_animationCtrl->Play();
}

void AMSrefresh::StopLoading()
{
    this->m_animationCtrl->Stop();
    this->m_animationCtrl->Hide();
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

void AMSrefresh::OnClick(wxMouseEvent &evt) { post_event(wxCommandEvent(EVT_AMS_REFRESH_RFID)); }

void AMSrefresh::post_event(wxCommandEvent &&event)
{
    event.SetString(m_info.can_id);
    event.SetEventObject(m_parent);
    wxPostEvent(m_parent, event);
    event.Skip();
}

void AMSrefresh::paintEvent(wxPaintEvent &evt)
{
    wxSize    size = GetSize();
    wxPaintDC dc(this);

    auto colour = AMS_CONTROL_GRAY700;
    if (!wxWindow::IsEnabled()) { colour = AMS_CONTROL_GRAY500; }

    auto pot = wxPoint((size.x - m_bitmap_selected.GetSize().x) / 2, (size.y - m_bitmap_selected.GetSize().y) / 2);
    dc.DrawBitmap(m_selected ? m_bitmap_selected : m_bitmap_normal, pot);

    dc.SetPen(wxPen(colour));
    dc.SetBrush(wxBrush(colour));
    dc.SetFont(Label::Body_10);
    dc.SetTextForeground(colour);
    auto tsize = dc.GetTextExtent(m_text);
    pot        = wxPoint((size.x - tsize.x) / 2, (size.y - tsize.y) / 2 + 1);
    dc.DrawText(m_text, pot);
}

void AMSrefresh::Update(Caninfo info)
{
    m_info = info;
    StopLoading();
}

void AMSrefresh::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    wxWindow::DoSetSize(x, y, width, height, sizeFlags);
    m_bitmap_normal   = create_scaled_bitmap("ams_refresh_normal", this, AMS_REFRESH_SIZE.x);
    m_bitmap_selected = create_scaled_bitmap("ams_refresh_selected", this, AMS_REFRESH_SIZE.x);
}

/*************************************************
Description:AMSextruder
**************************************************/
AMSextruder::AMSextruder(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size) { create(parent, id, pos, size); }

void AMSextruder::TurnOn()
{
    m_turn_on = true;
    Refresh();
}

void AMSextruder::TurnOff()
{
    m_turn_on = false;
    Refresh();
}

void AMSextruder::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    wxWindow::Create(parent, id, pos, size, wxBORDER_NONE);
    monitor_ams_extruder_off = create_scaled_bitmap("monitor_ams_extruder_off", nullptr, AMS_EXTRUDER_SIZE.y);
    monitor_ams_extruder_on  = create_scaled_bitmap("monitor_ams_extruder_on", nullptr, AMS_EXTRUDER_SIZE.y);
    Bind(wxEVT_PAINT, &AMSextruder::paintEvent, this);
    SetBackgroundColour(AMS_CONTROL_WHITE_COLOUR);
}

void AMSextruder::paintEvent(wxPaintEvent &evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AMSextruder::render(wxDC &dc)
{
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
}

void AMSextruder::doRender(wxDC &dc)
{
    wxSize size = GetSize();
    auto   midx = FromDIP(42.5);

    auto pot = wxPoint(midx - monitor_ams_extruder_on.GetSize().x / 2, 0);

    if (m_turn_on) {
        dc.DrawBitmap(monitor_ams_extruder_on, pot);
        // dc.DrawCircle(pot.x + FromDIP(17), pot.y + monitor_ams_extruder_on.GetSize().y / 2 + FromDIP(2), FromDIP(7));
    } else {
        dc.DrawBitmap(monitor_ams_extruder_off, pot);
        // dc.DrawCircle(pot.x + FromDIP(17), pot.y + monitor_ams_extruder_off.GetSize().y / 2 + FromDIP(2), FromDIP(7));
    }
}

/*************************************************
Description:AMSLib
**************************************************/
AMSLib::AMSLib(wxWindow *parent, wxWindowID id, Caninfo info, const wxPoint &pos, const wxSize &size)
{
    m_border_color   = (wxColour(130, 130, 128));
    m_lib_color      = AMS_CONTROL_WHITE_COLOUR;
    m_road_def_color = AMS_CONTROL_GRAY500;
    SetFont(Label::Body_12);
    wxWindow::SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    create(parent, id, pos, size);
    Bind(wxEVT_PAINT, &AMSLib::paintEvent, this);
    Update(info, false);
}

void AMSLib::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    wxWindow::Create(parent, id, pos, size);

    auto m_sizer_body = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *m_sizer_edit = new wxBoxSizer(wxHORIZONTAL);

    m_bitmap_editable       = create_scaled_bitmap("ams_editable", this, FromDIP(16));
    m_bitmap_editable_lifht = create_scaled_bitmap("ams_editable_light", this, FromDIP(16));

    m_edit_bitmp       = new wxStaticBitmap(this, wxID_ANY, m_bitmap_editable, wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)), wxTAB_TRAVERSAL);
    m_edit_bitmp_light = new wxStaticBitmap(this, wxID_ANY, m_bitmap_editable_lifht, wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)), wxTAB_TRAVERSAL);
    m_sizer_edit->Add(m_edit_bitmp, 0, wxALIGN_CENTER, 0);
    m_sizer_edit->Add(m_edit_bitmp_light, 0, wxALIGN_CENTER, 0);
    m_edit_bitmp->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { post_event(wxCommandEvent(EVT_AMS_ON_FILAMENT_EDIT)); });

    m_edit_bitmp_light->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { post_event(wxCommandEvent(EVT_AMS_ON_FILAMENT_EDIT)); });
    m_edit_bitmp->Hide();
    m_edit_bitmp_light->Hide();
    m_sizer_body->Add(0, 0, 1, wxEXPAND, 0);
    m_sizer_body->Add(m_sizer_edit, 0, wxALIGN_CENTER, 0);
    m_sizer_body->Add(0, 0, 0, wxBOTTOM, GetSize().y * 0.12);

    SetSizer(m_sizer_body);
    Layout();
}

void AMSLib::paintEvent(wxPaintEvent &evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AMSLib::Update(Caninfo info, bool refresh)
{
    m_info = info;
    switch (m_info.material_state) {
    case AMSCanType::AMS_CAN_TYPE_NONE:
        m_info.material_name = _L("None");
        SetLibColour(AMS_CONTROL_WHITE_COLOUR);
        break;
    case AMSCanType::AMS_CAN_TYPE_BRAND: SetLibColour(m_info.material_colour); break;
    case AMSCanType::AMS_CAN_TYPE_THIRDBRAND: SetLibColour(m_info.material_colour); break;
    default: break;
    }

    if (refresh) Refresh();
}

void AMSLib::SetLibColour(wxColour const &color)
{
    m_lib_color = color;
    Refresh();
}

wxColour AMSLib::GetLibColour() { return m_lib_color; }

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

void AMSLib::render(wxDC &dc)
{
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
}

void AMSLib::doRender(wxDC &dc)
{
    wxSize size             = GetSize();
    auto   tmp_lib_colour   = m_lib_color;
    auto   temp_text_colour = AMS_CONTROL_DISABLE_TEXT_COLOUR;

    Slic3r::GUI::BitmapCache bmcache;
    if (tmp_lib_colour.GetLuminance() < 0.5) {
        temp_text_colour = AMS_CONTROL_WHITE_COLOUR;
    } else {
        temp_text_colour = AMS_CONTROL_BLACK_COLOUR;
    }

    if (!wxWindow::IsEnabled()) {
        tmp_lib_colour   = AMS_CONTROL_DISABLE_COLOUR;
        temp_text_colour = AMS_CONTROL_DISABLE_TEXT_COLOUR;
    }

    // selected
    if (m_selected) {
        // lib
        dc.SetPen(wxPen(tmp_lib_colour, 2, wxSOLID));
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        if (m_radius == 0) {
            dc.DrawRectangle(0, 0, size.x, size.y);
        } else {
            dc.DrawRoundedRectangle(FromDIP(1), FromDIP(1), size.x - FromDIP(1), size.y - FromDIP(1), m_radius);
        }

        dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
        dc.SetBrush(wxBrush(tmp_lib_colour));
        if (m_radius == 0) {
            dc.DrawRectangle(0, 0, size.x, size.y);
        } else {
            dc.DrawRoundedRectangle(FromDIP(4), FromDIP(4), size.x - FromDIP(8), size.y - FromDIP(8), m_radius);
        }
    } else {
        // lib
        dc.SetPen(wxPen(tmp_lib_colour, 1, wxSOLID));
        dc.SetBrush(wxBrush(tmp_lib_colour));
        if (m_radius == 0) {
            dc.DrawRectangle(0, 0, size.x, size.y);
        } else {
            dc.DrawRoundedRectangle(FromDIP(4), FromDIP(4), size.x - FromDIP(8), size.y - FromDIP(8), m_radius);
        }
    }

    // text
    if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_BRAND || m_info.material_state == AMSCanType::AMS_CAN_TYPE_NONE) {
        auto tsize = dc.GetMultiLineTextExtent(m_info.material_name);
        auto pot   = wxPoint((size.x - tsize.x) / 2, size.y * 0.7);
        dc.SetFont(::Label::Head_12);
        dc.SetTextForeground(temp_text_colour);
        dc.DrawText(m_info.material_name, pot);
    }

    // edit
    if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_THIRDBRAND) {
        // dc.DrawBitmap(m_bitmap_editable, {(size.x - m_bitmap_editable.GetSize().x) / 2, int(size.y * 0.7)});
        m_edit_bitmp->SetBackgroundColour(tmp_lib_colour);
        m_edit_bitmp_light->SetBackgroundColour(tmp_lib_colour);

        if (tmp_lib_colour.GetLuminance() < 0.5) {
            m_edit_bitmp->Hide();
            m_edit_bitmp_light->Show();
        } else {
            m_edit_bitmp->Show();
            m_edit_bitmp_light->Hide();
        }

        Layout();
    } else {
        m_edit_bitmp->Hide();
    }
}
/*************************************************
Description:AMSRoad
**************************************************/
AMSRoad::AMSRoad() : m_road_def_color(AMS_CONTROL_GRAY500), m_road_color(AMS_CONTROL_GRAY500) {}
AMSRoad::AMSRoad(wxWindow *parent, wxWindowID id, Caninfo info, int canindex, int maxcan, const wxPoint &pos, const wxSize &size) : AMSRoad()
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
    }

    create(parent, id, pos, size);
    Bind(wxEVT_PAINT, &AMSRoad::paintEvent, this);
    wxWindow::SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
}

void AMSRoad::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size) { wxWindow::Create(parent, id, pos, size); }

void AMSRoad::Update(Caninfo info, int canindex, int maxcan)
{
    m_info = info;
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

void AMSRoad::SetPassRoadColour(wxColour col) { m_road_color = col; }

void AMSRoad::SetMode(AMSRoadMode mode)
{
    m_rode_mode = mode;
    Refresh();
}

void AMSRoad::paintEvent(wxPaintEvent &evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AMSRoad::render(wxDC &dc)
{
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

    // mode none
    // if (m_pass_rode_mode.size() == 1 && m_pass_rode_mode[0] == AMSPassRoadMode::AMS_ROAD_MODE_NONE) return;

    dc.SetPen(wxPen(m_road_color, m_passroad_width, wxSOLID));
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

    // end mode
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_END || m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_END_ONLY) {
        dc.SetPen(wxPen(m_road_def_color, 2, wxSOLID));
        dc.SetBrush(wxBrush(m_road_def_color));
        dc.DrawRoundedRectangle(size.x * 0.37 / 2, size.y * 0.6 - size.y / 6, size.x * 0.63, size.y / 3, m_radius);
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

    Refresh();
}

/*************************************************
Description:AMSControl
**************************************************/
AMSItem::AMSItem() {}

AMSItem::AMSItem(wxWindow *parent, wxWindowID id, AMSinfo amsinfo, const wxSize cube_size, const wxPoint &pos, const wxSize &size) : AMSItem()
{
    m_amsinfo   = amsinfo;
    m_cube_size = cube_size;
    create(parent, id, pos, size);
    Bind(wxEVT_PAINT, &AMSItem::paintEvent, this);
    Bind(wxEVT_ENTER_WINDOW, &AMSItem::OnEnterWindow, this);
    Bind(wxEVT_LEAVE_WINDOW, &AMSItem::OnLeaveWindow, this);
    // Bind(wxEVT_LEFT_DOWN, &AMSItem::OnSelected, this);
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
    Refresh();
}

void AMSItem::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    wxWindow::Create(parent, id, pos, size);
    SetBackgroundColour(AMS_CONTROL_WHITE_COLOUR);
    HideHumidity();
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

void AMSItem::ShowHumidity() 
{
    m_show_humidity = true;
    SetSize(AMS_ITEM_HUMIDITY_SIZE);
    SetMinSize(AMS_ITEM_HUMIDITY_SIZE);
    Refresh();
}

void AMSItem::HideHumidity() 
{
    m_show_humidity = false;
    SetSize(AMS_ITEM_SIZE);
    SetMinSize(AMS_ITEM_SIZE);
    Refresh();
}

void AMSItem::SetHumidity(int humidity) 
{
    m_humidity = humidity;
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
}

void AMSItem::doRender(wxDC &dc)
{
    wxSize size = GetSize();
    dc.SetPen(wxPen(m_background_colour));
    dc.SetBrush(wxBrush(m_background_colour));
    dc.DrawRoundedRectangle(0, 0, size.x, size.y, 3);

    auto left = m_padding;
    for (std::vector<Caninfo>::iterator iter = m_amsinfo.cans.begin(); iter != m_amsinfo.cans.end(); iter++) {
        dc.SetPen(wxPen(*wxTRANSPARENT_PEN));

        if (wxWindow::IsEnabled()) {
            dc.SetBrush(wxBrush(iter->material_colour));
        } else {
            dc.SetBrush(AMS_CONTROL_DISABLE_COLOUR);
        }

        dc.DrawRoundedRectangle(left, (size.y - AMS_ITEM_CUBE_SIZE.y) / 2, AMS_ITEM_CUBE_SIZE.x, AMS_ITEM_CUBE_SIZE.y, 2);
        left += AMS_ITEM_CUBE_SIZE.x;
        left += m_space;
    }

    if (m_show_humidity) {
        left = 4 * AMS_ITEM_CUBE_SIZE.x + 6 * m_space;
        dc.SetPen(wxPen(AMS_CONTROL_GRAY500, 1));
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        dc.DrawLine(left, (size.y - AMS_ITEM_CUBE_SIZE.y) / 2, left, AMS_ITEM_CUBE_SIZE.y);

        left += m_space;
        dc.SetFont(::Label::Body_13);
        dc.SetTextForeground(AMS_CONTROL_GRAY800);
        auto tsize = dc.GetTextExtent("00% RH");
        auto text  = wxString::Format("%d%% RH", m_humidity);
        dc.DrawText(text, wxPoint(left, (size.y - tsize.y) / 2 -2));
    }

    auto border_colour = m_border_colour;
    if (!wxWindow::IsEnabled()) { border_colour = AMS_CONTROL_DISABLE_COLOUR; }

    if (m_hover) {
        dc.SetPen(wxPen(border_colour, 1));
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        dc.DrawRoundedRectangle(0, 0, size.x, size.y, 3);
    }

    if (m_selected) {
        dc.SetPen(wxPen(border_colour, 1));
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        dc.DrawRoundedRectangle(0, 0, size.x, size.y, 3);
    }
}

void AMSItem::DoSetSize(int x, int y, int width, int height, int sizeFlags /*= wxSIZE_AUTO*/) { wxWindow::DoSetSize(x, y, width, height, sizeFlags); }

/*************************************************
Description:AmsCan
**************************************************/

AmsCans::AmsCans() {}

AmsCans::AmsCans(wxWindow *parent, wxWindowID id, AMSinfo info, const wxPoint &pos, const wxSize &size) : AmsCans()
{
    wxWindow::Create(parent, wxID_ANY, pos, size);
    create(parent, id, info, pos, size);
}

void AmsCans::create(wxWindow *parent, wxWindowID id, AMSinfo info, const wxPoint &pos, const wxSize &size)
{
    sizer_can = new wxBoxSizer(wxHORIZONTAL);
    m_info    = info;

    Freeze();
    for (auto it = m_info.cans.begin(); it != m_info.cans.end(); it++) {
        AddCan(*it, m_can_count, m_info.cans.size());
        m_can_count++;
    }

    SetSizer(sizer_can);
    Layout();
    Fit();
    Thaw();
}

void AmsCans::Update(AMSinfo info)
{
    m_info      = info;
    m_can_count = info.cans.size();

   
    for (auto i = 0; i < m_can_refresh_list.GetCount(); i++){
        Canrefreshs *refresh = m_can_refresh_list[i];
        if (i < m_can_count) {
            refresh->canrefresh->Update(info.cans[i]);
            refresh->canrefresh->Show();
        } else {
            refresh->canrefresh->Hide();
        }
    }

   
    for (auto i = 0; i < m_can_lib_list.GetCount(); i++) {
        CanLibs *lib = m_can_lib_list[i];
        if (i < m_can_count) {
            lib->canLib->Update(info.cans[i]);
            lib->canLib->Show();
        } else {
            lib->canLib->Hide();
        }
    }

   for (auto i = 0; i < m_can_road_list.GetCount(); i++) {
        CanRoads *road = m_can_road_list[i];
        if (i < m_can_count) {
            road->canRoad->Update(info.cans[i], i, m_can_count);
            road->canRoad->Show();
        } else {
            road->canRoad->Hide();
        }
    }

    Layout();
}

void AmsCans::AddCan(Caninfo caninfo, int canindex, int maxcan)
{
    auto        amscan      = new wxWindow(this, wxID_ANY);
    wxBoxSizer *m_sizer_ams = new wxBoxSizer(wxVERTICAL);
    m_sizer_ams->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(14));
    auto m_panel_refresh = new AMSrefresh(amscan, wxID_ANY, m_can_count + 1, caninfo);
    m_sizer_ams->Add(m_panel_refresh, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    m_sizer_ams->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(2));
    auto m_panel_lib = new AMSLib(amscan, wxID_ANY, caninfo, wxDefaultPosition, AMS_CAN_LIB_SIZE);
    m_panel_lib->Bind(wxEVT_LEFT_DOWN, [this, canindex](wxMouseEvent &ev) {
        m_canlib_selection = canindex;
        //m_canlib_id        = caninfo.can_id;

        for (auto i = 0; i < m_can_lib_list.GetCount(); i++) {
            CanLibs *lib = m_can_lib_list[i];
            if (lib->canLib->m_can_index == m_canlib_selection) {
                lib->canLib->OnSelected();
            } else {
                lib->canLib->UnSelected();
            }
        }
    });

    m_panel_lib->m_info.can_id = caninfo.can_id;
    m_panel_lib->m_can_index   = canindex;
    auto m_panel_road          = new AMSRoad(amscan, wxID_ANY, caninfo, canindex, maxcan, wxDefaultPosition, wxSize(-1, AMS_CAN_ROAD_SIZE));
    m_sizer_ams->Add(m_panel_lib, 1, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, FromDIP(4));
    m_sizer_ams->Add(m_panel_road, 0, wxEXPAND, 0);

    amscan->SetSizer(m_sizer_ams);
    amscan->Layout();
    m_sizer_ams->Fit(amscan);
    sizer_can->Add(amscan, 0, wxALL, 0);

    Canrefreshs *canrefresh               = new Canrefreshs;
    canrefresh->canID                     = caninfo.can_id;
    canrefresh->canrefresh                = m_panel_refresh;
    m_can_refresh_list.Add(canrefresh);

    CanLibs *canlib               = new CanLibs;
    canlib->canID                 = caninfo.can_id;
    canlib->canLib                = m_panel_lib;
    m_can_lib_list.Add(canlib);

    CanRoads *canroad              = new CanRoads;
    canroad->canID                 = caninfo.can_id;
    canroad->canRoad               = m_panel_road;
    m_can_road_list.Add(canroad);
}

void AmsCans::SelectCan(std::string can_id)
{
    for (auto i = 0; i < m_can_lib_list.GetCount(); i++) { 
        CanLibs *lib = m_can_lib_list[i]; 
        if (lib->canLib->m_info.can_id == can_id) {
            m_canlib_selection = lib->canLib->m_can_index;
        }
    }

    m_canlib_id = can_id;

   for (auto i = 0; i < m_can_lib_list.GetCount(); i++) {
        CanLibs *lib = m_can_lib_list[i];
        if (lib->canLib->m_info.can_id == m_canlib_id) {
            lib->canLib->OnSelected();
        } else {
            lib->canLib->UnSelected();
        }
    }
}

void AmsCans::SetAmsStep(wxString canid, AMSPassRoadType type, AMSPassRoadSTEP step)
{
    auto                    tag_can_index = -1;
    for (auto i = 0; i < m_can_road_list.GetCount(); i++) {
        CanRoads *road = m_can_road_list[i];
        if (canid == road->canRoad->m_info.can_id) { tag_can_index = road->canRoad->m_canindex; }
    }

    if (tag_can_index == -1) return;

    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
        for (auto i = 0; i < m_can_road_list.GetCount(); i++) {
            CanRoads *road = m_can_road_list[i];
            auto      pr   = std::vector<AMSPassRoadMode>{};
            pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_NONE);
            road->canRoad->OnPassRoad(pr);
        }

        return;
    }

    // get colour
    auto                  tag_colour = *wxWHITE;
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


void AmsCans::PlayRridLoading(wxString canid)
{
    for (auto i = 0; i < m_can_refresh_list.GetCount(); i++) {
        Canrefreshs *refresh = m_can_refresh_list[i];
        if (refresh->canrefresh->m_info.can_id == canid) { refresh->canrefresh->PlayLoading(); }
    }
}

std::string AmsCans::GetCurrentCan() 
{ 
    if (m_canlib_selection > -1 && m_canlib_selection < m_can_lib_list.size()) {
        CanLibs *lib = m_can_lib_list[m_canlib_selection];
        return lib->canLib->m_info.can_id; 
    }
    return "";
}

void AmsCans::StopRridLoading(wxString canid)
{
    for (auto i = 0; i < m_can_refresh_list.GetCount(); i++) {
        Canrefreshs *refresh = m_can_refresh_list[i];
        if (refresh->canrefresh->m_info.can_id == canid) { refresh->canrefresh->StopLoading(); }
    }
}

/*************************************************
Description:AMSControl
**************************************************/
//WX_DEFINE_OBJARRAY(AmsItemsHash);
AMSControl::AMSControl(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size) : wxSimplebook(parent, wxID_ANY, pos, size)
{
    SetBackgroundColour(*wxWHITE);

    // normal mode
    Freeze();
    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxVERTICAL);
    auto        amswin       = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxSize(-1, AMS_CAN_ITEM_HEIGHT_SIZE));
    amswin->SetBackgroundColour(*wxWHITE);
    // top - ams tag
    m_simplebook_amsitems = new wxSimplebook(amswin, wxID_ANY);
    m_simplebook_amsitems->SetSize(wxSize(-1, AMS_CAN_ITEM_HEIGHT_SIZE));
    m_simplebook_amsitems->SetMinSize(wxSize(-1, AMS_CAN_ITEM_HEIGHT_SIZE));
    auto m_sizer_amsitems = new wxBoxSizer(wxHORIZONTAL);
    m_simplebook_amsitems->SetSizer(m_sizer_amsitems);
    m_simplebook_amsitems->Layout();
    m_sizer_amsitems->Fit(m_simplebook_amsitems);
    m_sizer_body->Add(m_simplebook_amsitems, 0, wxEXPAND, 0);

    m_panel_top = new wxPanel(m_simplebook_amsitems, wxID_ANY, wxDefaultPosition, wxSize(-1, AMS_CAN_ITEM_HEIGHT_SIZE));
    m_sizer_top = new wxBoxSizer(wxHORIZONTAL);
    m_panel_top->SetSizer(m_sizer_top);
    m_panel_top->Layout();
    m_sizer_top->Fit(m_panel_top);

    auto m_panel_top_empty = new wxPanel(m_simplebook_amsitems, wxID_ANY, wxDefaultPosition, wxSize(-1, AMS_CAN_ITEM_HEIGHT_SIZE));
    auto m_sizer_top_empty = new wxBoxSizer(wxHORIZONTAL);
    m_panel_top_empty->SetSizer(m_sizer_top_empty);
    m_panel_top_empty->Layout();
    m_sizer_top_empty->Fit(m_panel_top_empty);

    m_simplebook_amsitems->AddPage(m_panel_top, wxEmptyString, false);
    m_simplebook_amsitems->AddPage(m_panel_top_empty, wxEmptyString, false);

    m_sizer_body->Add(0, 0, 1, wxEXPAND | wxTOP, 18);

    wxBoxSizer *m_sizer_bottom = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *m_sizer_left   = new wxBoxSizer(wxVERTICAL);

    m_panel_can = new StaticBox(amswin, wxID_ANY, wxDefaultPosition, AMS_CANS_SIZE, wxTAB_TRAVERSAL | wxBORDER_NONE);
    m_panel_can->SetMinSize(AMS_CANS_SIZE);
    m_panel_can->SetCornerRadius(10);
    m_panel_can->SetBackgroundColor(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);

    m_sizer_cans = new wxBoxSizer(wxHORIZONTAL);

    m_simplebook_ams = new wxSimplebook(m_panel_can, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0);
    m_simplebook_ams->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    m_sizer_cans->Add(m_simplebook_ams, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

    // ams mode
    m_simplebook_cans = new wxSimplebook(m_simplebook_ams, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0);
    m_simplebook_cans->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);

    // none ams mode
    m_none_ams_panel = new wxPanel(m_simplebook_ams, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0);
    m_none_ams_panel->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);

    wxBoxSizer *sizer_ams_panel = new wxBoxSizer(wxHORIZONTAL);
    AMSinfo     none_ams        = AMSinfo{"0", std::vector<Caninfo>{Caninfo{"0", _L("None"), *wxWHITE, AMSCanType::AMS_CAN_TYPE_NONE}}};
    auto        amscans         = new AmsCans(m_none_ams_panel, wxID_ANY, none_ams);
    sizer_ams_panel->Add(amscans, 0, wxALL, 0);
    sizer_ams_panel->Add(0, 0, 0, wxLEFT, 20);
    auto m_tip_none_ams = new wxStaticText(m_none_ams_panel, wxID_ANY, _L("Tip: Click the button to edit your filament"), wxDefaultPosition, wxDefaultSize, 0);
    m_tip_none_ams->Wrap(150);
    m_tip_none_ams->SetFont(::Label::Body_13);
    m_tip_none_ams->SetForegroundColour(AMS_CONTROL_GRAY500);
    m_tip_none_ams->SetMinSize({150, -1});
    sizer_ams_panel->Add(m_tip_none_ams, 0, wxALIGN_CENTER, 0);
    m_none_ams_panel->SetSizer(sizer_ams_panel);
    m_none_ams_panel->Layout();

    m_simplebook_ams->AddPage(m_simplebook_cans, wxEmptyString, true);
    m_simplebook_ams->AddPage(m_none_ams_panel, wxEmptyString, false);

    m_panel_can->SetSizer(m_sizer_cans);
    m_panel_can->Layout();
    m_sizer_cans->Fit(m_panel_can);

    m_sizer_left->Add(m_panel_can, 1, wxEXPAND, 0);

    wxBoxSizer *m_sizer_left_bottom = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *sizer_sextruder     = new wxBoxSizer(wxHORIZONTAL);

    m_extruder = new AMSextruder(amswin, wxID_ANY, wxDefaultPosition, AMS_EXTRUDER_SIZE);
    sizer_sextruder->Add(m_extruder, 1, wxEXPAND | wxALL, 0);
    m_sizer_left_bottom->Add(sizer_sextruder, 1, wxEXPAND, 0);

    m_sizer_left_bottom->Add(0, 0, 0, wxEXPAND, 0);
    m_sizer_left_bottom->Add(0, 0, 0, wxALL | wxLEFT, 26);

    StateColor extruder_bg(std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled), std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Pressed),
                           std::pair<wxColour, int>(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, StateColor::Hovered),
                           std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Normal));

    StateColor extruder_bd(std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));

    m_button_extruder_feed = new Button(amswin, _L("Load"));
    m_button_extruder_feed->SetBackgroundColor(extruder_bg);
    m_button_extruder_feed->SetBorderColor(extruder_bd);
    m_button_extruder_feed->SetFont(Label::Body_10);
    m_sizer_left_bottom->Add(m_button_extruder_feed, 0, wxTOP, 20);
    m_sizer_left_bottom->Add(0, 0, 0, wxALL | wxLEFT, 10);

    m_button_extruder_back = new Button(amswin, _L("Unload"));
    m_button_extruder_back->SetBackgroundColor(extruder_bg);
    m_button_extruder_back->SetBorderColor(extruder_bd);
    m_button_extruder_back->SetFont(Label::Body_10);
    m_sizer_left_bottom->Add(m_button_extruder_back, 0, wxTOP, 20);

    m_sizer_left->Add(m_sizer_left_bottom, 0, wxEXPAND, 5);
    m_sizer_bottom->Add(m_sizer_left, 0, wxEXPAND, 5);
    m_sizer_bottom->Add(0, 0, 0, wxEXPAND | wxLEFT, 43);

    wxBoxSizer *m_sizer_right = new wxBoxSizer(wxVERTICAL);
    m_simplebook_right        = new wxSimplebook(amswin, wxID_ANY);
    m_simplebook_right->SetMinSize(AMS_STEP_SIZE);
    m_simplebook_right->SetSize(AMS_STEP_SIZE);
    m_sizer_right->Add(m_simplebook_right, 0, wxALL, 0);

    auto        tip_right         = new wxPanel(m_simplebook_right, wxID_ANY, wxDefaultPosition, AMS_STEP_SIZE, wxTAB_TRAVERSAL);
    wxBoxSizer *m_sizer_right_tip = new wxBoxSizer(wxVERTICAL);
    auto        tip_right_top     = new wxStaticText(tip_right, wxID_ANY, _L("Tips"), wxDefaultPosition, wxDefaultSize, 0);
    tip_right_top->SetFont(::Label::Head_13);
    tip_right_top->SetForegroundColour(AMS_CONTROL_BRAND_COLOUR);
    tip_right_top->Wrap(AMS_STEP_SIZE.x);
    m_sizer_right_tip->Add(tip_right_top, 0, 0, 0);
    m_sizer_right_tip->Add(0, 0, 0, wxTOP, 10);
    m_tip_load_info = new wxStaticText(tip_right, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    m_tip_load_info->SetFont(::Label::Body_13);
    m_tip_load_info->SetForegroundColour(AMS_CONTROL_GRAY700);
    m_sizer_right_tip->Add(m_tip_load_info, 0, 0, 0);
    tip_right->SetSizer(m_sizer_right_tip);
    tip_right->Layout();

    m_filament_load_step = new ::StepIndicator(m_simplebook_right, wxID_ANY);
    m_filament_load_step->SetMinSize(AMS_STEP_SIZE);
    m_filament_load_step->SetSize(AMS_STEP_SIZE);

    m_filament_unload_step = new ::StepIndicator(m_simplebook_right, wxID_ANY);
    m_filament_unload_step->SetMinSize(AMS_STEP_SIZE);
    m_filament_unload_step->SetSize(AMS_STEP_SIZE);

    m_simplebook_right->AddPage(tip_right, wxEmptyString, false);
    m_simplebook_right->AddPage(m_filament_load_step, wxEmptyString, false);
    m_simplebook_right->AddPage(m_filament_unload_step, wxEmptyString, false);

    wxBoxSizer *m_sizer_right_bottom = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_right_bottom->Add(0, 0, 1, wxEXPAND, 5);
    m_button_ams_setting = new Button(amswin, _L("AMS Settings"));
    m_button_ams_setting->SetBackgroundColor(extruder_bg);
    m_button_ams_setting->SetBorderColor(extruder_bd);
    m_button_ams_setting->SetFont(Label::Body_10);
    m_sizer_right_bottom->Add(m_button_ams_setting, 0, wxTOP, 20);
    m_sizer_right->Add(m_sizer_right_bottom, 0, wxEXPAND, 5);
    m_sizer_bottom->Add(m_sizer_right, 0, wxEXPAND, 5);
    m_sizer_body->Add(m_sizer_bottom, 0, wxEXPAND | wxLEFT, 11);

    init_scaled_buttons();
    amswin->SetSizer(m_sizer_body);
    amswin->Layout();
    amswin->Fit();
    Thaw();

    SetSize(amswin->GetSize());
    SetMinSize(amswin->GetSize());

    // calibration mode
    m_simplebook_calibration = new wxSimplebook(this, wxID_ANY, wxDefaultPosition, amswin->GetSize(), wxTAB_TRAVERSAL);

    auto m_in_calibration_panel = new wxWindow(m_simplebook_calibration, wxID_ANY, wxDefaultPosition, amswin->GetSize(), wxTAB_TRAVERSAL);
    m_in_calibration_panel->SetBackgroundColour(AMS_CONTROL_WHITE_COLOUR);
    wxBoxSizer *sizer_calibration_h = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *sizer_calibration_v = new wxBoxSizer(wxVERTICAL);
    auto        thumbnail           = new wxStaticBitmap(m_in_calibration_panel, wxID_ANY, create_scaled_bitmap("ams_icon", nullptr, 126), wxDefaultPosition, wxDefaultSize);
    m_text_calibration_percent      = new wxStaticText(m_in_calibration_panel, wxID_ANY, wxT("0%"), wxDefaultPosition, wxDefaultSize, 0);
    m_text_calibration_percent->SetFont(::Label::Head_16);
    m_text_calibration_percent->SetForegroundColour(AMS_CONTROL_BRAND_COLOUR);
    auto m_text_calibration_tip = new wxStaticText(m_in_calibration_panel, wxID_ANY, _L("Calibrating AMS..."), wxDefaultPosition, wxDefaultSize, 0);
    m_text_calibration_tip->SetFont(::Label::Body_14);
    m_text_calibration_tip->SetForegroundColour(AMS_CONTROL_GRAY700);
    sizer_calibration_v->Add(thumbnail, 0, wxALIGN_CENTER, 0);
    sizer_calibration_v->Add(0, 0, 0, wxTOP, 16);
    sizer_calibration_v->Add(m_text_calibration_percent, 0, wxALIGN_CENTER, 0);
    sizer_calibration_v->Add(0, 0, 0, wxTOP, 8);
    sizer_calibration_v->Add(m_text_calibration_tip, 0, wxALIGN_CENTER, 0);
    sizer_calibration_h->Add(sizer_calibration_v, 1, wxALIGN_CENTER, 0);
    m_in_calibration_panel->SetSizer(sizer_calibration_h);
    m_in_calibration_panel->Layout();

    auto m_calibration_err_panel = new wxWindow(m_simplebook_calibration, wxID_ANY, wxDefaultPosition, amswin->GetSize(), wxTAB_TRAVERSAL);
    m_calibration_err_panel->SetBackgroundColour(AMS_CONTROL_WHITE_COLOUR);
    wxBoxSizer *sizer_err_calibration_h = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *sizer_err_calibration_v = new wxBoxSizer(wxVERTICAL);
    m_hyperlink = new wxHyperlinkCtrl(m_calibration_err_panel, wxID_ANY, wxEmptyString, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxHL_DEFAULT_STYLE);
    m_hyperlink->SetVisitedColour(wxColour(31, 142, 234));
    auto m_tip_calibration_err = new wxStaticText(m_calibration_err_panel, wxID_ANY, _L("calibration problem, click to see the solution"), wxDefaultPosition, wxDefaultSize, 0);
    m_tip_calibration_err->SetFont(::Label::Body_14);
    m_tip_calibration_err->SetForegroundColour(AMS_CONTROL_GRAY700);

    wxBoxSizer *sizer_button = new wxBoxSizer(wxHORIZONTAL);

    auto       m_button_calibration_again = new Button(m_calibration_err_panel, _L("calibrate again"));
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    m_button_calibration_again->SetBackgroundColor(btn_bg_green);
    m_button_calibration_again->SetBorderColor(AMS_CONTROL_BRAND_COLOUR);
    m_button_calibration_again->SetTextColor(AMS_CONTROL_WHITE_COLOUR);
    m_button_calibration_again->SetMinSize(AMS_CONTRO_CALIBRATION_BUTTON_SIZE);
    m_button_calibration_again->SetCornerRadius(12);
    m_button_calibration_again->Bind(wxEVT_LEFT_DOWN, &AMSControl::on_clibration_again_click, this);

    sizer_button->Add(m_button_calibration_again, 0, wxALL, 5);


    auto       m_button_calibration_cancel = new Button(m_calibration_err_panel, _L("cancel calibrate"));
    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Disabled), std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
                            std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered), std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Enabled),
                            std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));
    m_button_calibration_cancel->SetBackgroundColor(btn_bg_white);
    m_button_calibration_cancel->SetBorderColor(AMS_CONTROL_GRAY700);
    m_button_calibration_cancel->SetTextColor(AMS_CONTROL_GRAY800);
    m_button_calibration_cancel->SetMinSize(AMS_CONTRO_CALIBRATION_BUTTON_SIZE);
    m_button_calibration_cancel->SetCornerRadius(12);
    m_button_calibration_cancel->Bind(wxEVT_LEFT_DOWN, &AMSControl::on_clibration_cancel_click, this);
    sizer_button->Add(m_button_calibration_cancel, 0, wxALL, 5);

    sizer_err_calibration_v->Add(m_hyperlink, 0, wxALIGN_CENTER, 0);
    sizer_err_calibration_v->Add(0, 0, 0, wxTOP, 6);
    sizer_err_calibration_v->Add(m_tip_calibration_err, 0, wxALIGN_CENTER, 0);
    sizer_err_calibration_v->Add(0, 0, 0, wxTOP, 8);
    sizer_err_calibration_v->Add(sizer_button, 0, wxALIGN_CENTER | wxTOP, 18);
    sizer_err_calibration_h->Add(sizer_err_calibration_v, 1, wxALIGN_CENTER, 0);
    m_calibration_err_panel->SetSizer(sizer_err_calibration_h);
    m_calibration_err_panel->Layout();

    m_simplebook_calibration->AddPage(m_in_calibration_panel, wxEmptyString, false);
    m_simplebook_calibration->AddPage(m_calibration_err_panel, wxEmptyString, false);

    AddPage(amswin, wxEmptyString, false);
    AddPage(m_in_calibration_panel, wxEmptyString, false);

    UpdateStepCtrl();

    m_button_extruder_feed->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AMSControl::on_filament_load), NULL, this);
    m_button_extruder_back->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AMSControl::on_filament_unload), NULL, this);
    m_button_ams_setting->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AMSControl::on_ams_setting_click), NULL, this);

    CreateAms();
    SetSelection(0);
    EnterNoneAMSMode();

   
    //a->SetPosition({200,200});
}

void AMSControl::init_scaled_buttons()
{
    m_button_extruder_feed->SetMinSize(wxSize(FromDIP(48), FromDIP(24)));
    m_button_extruder_feed->SetCornerRadius(FromDIP(12));
    m_button_extruder_back->SetMinSize(wxSize(FromDIP(48), FromDIP(24)));
    m_button_extruder_back->SetCornerRadius(FromDIP(12));
    m_button_ams_setting->SetMinSize(wxSize(FromDIP(88), FromDIP(33)));
    m_button_ams_setting->SetCornerRadius(FromDIP(12));
}

std::string AMSControl::GetCurentAms() { return m_current_ams; }

std::string AMSControl::GetCurrentCan(std::string amsid)
{
    std::string           current_can;
    for (auto i = 0; i< m_ams_cans_list.GetCount(); i++){
        AmsCansWindow *ams = m_ams_cans_list[i];
        if (ams->amsCans->m_info.ams_id == amsid) {
            current_can = ams->amsCans->GetCurrentCan();
            return current_can; 
        }
    }
    return current_can;
}

void AMSControl::EnterNoneAMSMode()
{
    m_simplebook_amsitems->SetSelection(1);
    m_simplebook_ams->SetSelection(1);
    ShowFilamentTip(false);
}

void AMSControl::ExitNoneAMSMode()
{
    m_simplebook_ams->SetSelection(0);
    m_simplebook_amsitems->SetSelection(0);
}

void AMSControl::EnterCalibrationMode(bool read_to_calibration)
{
    SetSelection(1);
    if (read_to_calibration)
        m_simplebook_calibration->SetSelection(0);
    else
        m_simplebook_calibration->SetSelection(1);
}

void AMSControl::ExitcClibrationMode() { SetSelection(0); }

void AMSControl::SetClibrationpercent(int percent) { m_text_calibration_percent->SetLabelText(wxString::Format("%d%%", percent)); }

void AMSControl::SetClibrationLink(wxString link)
{
    m_hyperlink->SetLabel(link);
    m_hyperlink->SetURL(link);
    m_hyperlink->Refresh();
    m_hyperlink->Update();
}

void AMSControl::PlayRridLoading(wxString amsid, wxString canid)
{
    AmsCansHash::iterator iter        = m_ams_cans_list.begin();
    auto                   count_item_index = 0;

   for (auto i = 0; i < m_ams_cans_list.GetCount(); i++) {
        AmsCansWindow *cans = m_ams_cans_list[i];
        if (cans->amsCans->m_info.ams_id == amsid) {cans->amsCans->PlayRridLoading(canid); }
        iter++;
    }
}

void AMSControl::StopRridLoading(wxString amsid, wxString canid)
{
    AmsCansHash::iterator iter             = m_ams_cans_list.begin();
    auto                  count_item_index = 0;

    for (auto i = 0; i < m_ams_cans_list.GetCount(); i++) {
        AmsCansWindow *cans = m_ams_cans_list[i];
        if (cans->amsCans->m_info.ams_id == amsid) { cans->amsCans->StopRridLoading(canid); }
        iter++;
    }
}

void AMSControl::msw_rescale()
{
    m_sizer_top->Layout();
    Layout();
}

void AMSControl::UpdateStepCtrl()
{
    for (int i = 0; i < LOAD_STEP_COUNT; i++) { m_filament_load_step->AppendItem(FILAMENT_LOAD_STEP_STRING[i]); }
    for (int i = 0; i < UNLOAD_STEP_COUNT; i++) { m_filament_unload_step->AppendItem(FILAMENT_UNLOAD_STEP_STRING[i]); }
}

void AMSControl::CreateAms()
{
    auto caninfo0_0 = Caninfo{"def_can_0", _L(""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_NONE};
    auto caninfo0_1 = Caninfo{"def_can_1", _L(""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_NONE};
    auto caninfo0_2 = Caninfo{"def_can_2", _L(""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_NONE};
    auto caninfo0_3 = Caninfo{"def_can_3", _L(""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_NONE};

    AMSinfo                        ams1 = AMSinfo{"def_ams_0", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}};
    AMSinfo                        ams2 = AMSinfo{"def_ams_1", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}};
    AMSinfo                        ams3 = AMSinfo{"def_ams_2", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}};
    AMSinfo                        ams4 = AMSinfo{"def_ams_3", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}};
    std::vector<AMSinfo>           ams_info{ams1, ams2, ams3, ams4};
    std::vector<AMSinfo>::iterator it;
    Freeze();
    for (it = ams_info.begin(); it != ams_info.end(); it++) {
        AddAms(*it, true); 
    }
    m_sizer_top->Layout();
    Thaw();
}

void AMSControl::UpdateAms(std::vector<AMSinfo> info, bool keep_selection)
{
    std::string curr_ams_id = GetCurentAms();
    std::string curr_can_id = GetCurrentCan(curr_ams_id);
    if (info.size() > 0) ExitNoneAMSMode();

    // update item
    m_ams_info                              = info;
    for (auto i = 0; i < m_ams_item_list.GetCount(); i++) {
         AmsItems* item  = m_ams_item_list[i]; 
        if (i < info.size()) {
            item->amsItem->Update(m_ams_info[i]);
            item->amsItem->Open();
        } else {
            item->amsItem->Close();
        }
    }

    // update cans
    for (auto i = 0; i < m_ams_cans_list.GetCount(); i++) {
        AmsCansWindow *cans = m_ams_cans_list[i];
        if (i < info.size()) {
            cans->amsCans->m_info = m_ams_info[i];
            cans->amsCans->Update(m_ams_info[i]);
        }
    }

    if (m_current_senect.empty() && info.size() > 0) {
        if (curr_ams_id.empty()) {
            SwitchAms(info[0].ams_id);
            return;
        }

        if (keep_selection) {
            SwitchAms(curr_ams_id);

             for (auto i = 0; i < m_ams_cans_list.GetCount(); i++) {
                AmsCansWindow *cans = m_ams_cans_list[i];
                if (i < info.size()) {
                    cans->amsCans->SelectCan(curr_can_id);
                }
            }

          /*  auto iter = m_ams_cans_list.find(curr_can_id);
            if (iter != m_ams_cans_list.end()) {
                if (iter->second && iter->second->amsCans) iter->second->amsCans->SelectCan(curr_can_id);
            }*/
        }
    }
}

void AMSControl::AddAms(AMSinfo info, bool refresh)
{
    if (m_ams_count >= AMS_CONTROL_MAX_COUNT) return;

    // item
    auto amsitem = new AMSItem(m_panel_top, wxID_ANY, info);
    amsitem->Bind(wxEVT_LEFT_DOWN, [this, amsitem](wxMouseEvent &e) {
        SwitchAms(amsitem->m_amsinfo.ams_id);
        e.Skip();
    });

    AmsItems *item                  = new AmsItems();
    item->amsIndex                  = info.ams_id;
    item->amsItem                   = amsitem;

    m_ams_item_list.Add(item);
    m_sizer_top->Add(amsitem, 0, wxALL | wxEXPAND, 3);

    AmsCansWindow *canswin = new AmsCansWindow();
    auto           amscans = new AmsCans(m_simplebook_cans, wxID_ANY, info);

    canswin->amsIndex                  = info.ams_id;
    canswin->amsCans                   = amscans;
    m_ams_cans_list.Add(canswin);

    m_simplebook_cans->AddPage(amscans, wxEmptyString, false);
    amscans->m_selection = m_simplebook_cans->GetPageCount() - 1;

    if (refresh) { m_sizer_top->Layout(); }
    m_ams_count++;
    m_ams_info.push_back(info);
}

void AMSControl::RemoveAll()
{
    m_sizer_top->Layout();
    m_ams_count      = 0;
    m_current_senect = "";
    m_ams_info.clear();
    EnterNoneAMSMode();
}

void AMSControl::SwitchAms(std::string ams_id)
{
     for (auto i = 0; i < m_ams_item_list.GetCount(); i++) {
        AmsItems*  item = m_ams_item_list[i]; 
        if (item->amsItem->m_amsinfo.ams_id == ams_id) {
            item->amsItem->OnSelected();
            item->amsItem->ShowHumidity();
            m_current_senect = ams_id;
        } else {
            item->amsItem->UnSelected();
            item->amsItem->HideHumidity();
        }
        m_sizer_top->Layout();
        //m_panel_top->Fit();
    }

    for (auto i = 0; i < m_ams_cans_list.GetCount(); i++) {
        AmsCansWindow *cans = m_ams_cans_list[i];
        if (cans->amsCans->m_info.ams_id == ams_id) { m_simplebook_cans->SetSelection(cans->amsCans->m_selection); }
    }
    m_current_ams = ams_id;
}

void AMSControl::SetFilamentStep(int item_idx, bool isload)
{
    if (item_idx == FilamentStep::STEP_IDLE && isload) { 
        m_filament_load_step->Idle();
        return;
    }

    if (item_idx == FilamentStep::STEP_IDLE && !isload) {
        m_filament_unload_step->Idle();
        return;
    }

    if (item_idx >= 0 && isload && item_idx < FilamentStep::STEP_COUNT) {
        m_simplebook_right->SetSelection(1);
        m_filament_load_step->SelectItem(item_idx - 1);
    }

    if (item_idx >= 0 && !isload && item_idx < FilamentStep::STEP_COUNT) {
        m_simplebook_right->SetSelection(2);
        m_filament_unload_step->SelectItem(item_idx - 1);
    }
}

void AMSControl::ShowFilamentTip(bool hasams)
{
    m_simplebook_right->SetSelection(0);
    if (hasams)
        m_tip_load_info->SetLabelText(_L("Choose an AMS slot then press \"Load\" or \"Unload\" button to automatically load or unload filiament."));
    else
        m_tip_load_info->SetLabelText(_L("Before loading, please make sure the filament is pushed into toolhead."));
    m_tip_load_info->Wrap(AMS_STEP_SIZE.x);
    m_tip_load_info->SetMinSize(AMS_STEP_SIZE);
}

void AMSControl::SetHumidity(std::string amsid, int humidity) 
{
    for (auto i = 0; i < m_ams_item_list.GetCount(); i++) {
        AmsItems *item = m_ams_item_list[i];
        if (amsid == item->amsItem->m_amsinfo.ams_id) {
            item->amsItem->SetHumidity(humidity);
        }
    }
}

bool AMSControl::Enable(bool enable)
{
    for (auto i = 0; i < m_ams_item_list.GetCount(); i++) {
      AmsItems* item = m_ams_item_list[i]; 
      item->amsItem->Enable(enable);
    }

     for (auto i = 0; i < m_ams_cans_list.GetCount(); i++) {
        AmsCansWindow *cans = m_ams_cans_list[i];
        cans->amsCans->Enable(enable);
    }
    m_button_extruder_back->Enable(enable);
    m_button_extruder_feed->Enable(enable);
    m_button_ams_setting->Enable(enable);

    m_filament_load_step->Enable(enable);
    return wxWindow::Enable(enable);
}

void AMSControl::SetAmsStep(std::string ams_id, std::string canid, AMSPassRoadType type, AMSPassRoadSTEP step)
{
    AmsCansWindow *       cans     = nullptr;
    bool                  notfound = true;

    for (auto i = 0; i < m_ams_cans_list.GetCount(); i++) {
        cans = m_ams_cans_list[i];
        if (cans->amsCans->m_info.ams_id == ams_id) {
            notfound = false;
            break;
        }
    }

    if (notfound) return;
    if (cans == nullptr) return;

    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
        m_extruder->TurnOff();
        cans->amsCans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    }

    type = AMSPassRoadType::AMS_ROAD_TYPE_LOAD;
    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1) {
        m_extruder->TurnOff();
        cans->amsCans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
        cans->amsCans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
    }

    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2) {
        m_extruder->TurnOn();
        cans->amsCans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    }

    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3) {
        m_extruder->TurnOn();
        cans->amsCans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
        cans->amsCans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
    }
}

void AMSControl::on_filament_load(wxCommandEvent &event) { 
    post_event(SimpleEvent(EVT_AMS_LOAD)); 
    GetCurrentCan(GetCurentAms());
}

void AMSControl::on_filament_unload(wxCommandEvent &event) { post_event(SimpleEvent(EVT_AMS_UNLOAD)); }

void AMSControl::on_ams_setting_click(wxCommandEvent &event) { post_event(SimpleEvent(EVT_AMS_SETTINGS)); }

void AMSControl::on_clibration_again_click(wxMouseEvent &event) { post_event(SimpleEvent(EVT_AMS_CLIBRATION_AGAIN)); }

void AMSControl::on_clibration_cancel_click(wxMouseEvent &event) { post_event(SimpleEvent(EVT_AMS_CLIBRATION_CANCEL)); }

void AMSControl::post_event(wxEvent &&event)
{
    event.SetEventObject(m_parent);
    wxPostEvent(m_parent, event);
}

}} // namespace Slic3r::GUI
