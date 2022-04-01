#include "AMSControl.hpp"
#include "Label.hpp"
#include "../BitmapCache.hpp"
#include "../I18N.hpp"

#include <wx/simplebook.h>
#include <wx/dcgraph.h>

namespace Slic3r {
namespace GUI {


static wxString FILAMENT_STEP_STRING[STEP_COUNT] = {
    _L("Choose the position"),
    _L("Click the load below"),
    _L("Heat the extruder"),
    _L("Load"),
    _L("Complete")
};

wxDEFINE_EVENT(EVT_AMS_FEED, SimpleEvent);
wxDEFINE_EVENT(EVT_AMS_RETURN, SimpleEvent);
wxDEFINE_EVENT(EVT_AMS_SETTINGS, SimpleEvent);

static wxColour decode_color(std::string &color)
{
    // TODO
    return wxColour(rand() % 255, rand() % 255, rand() % 255);
}

bool AMSinfo::parse_ams_info(Ams *ams)
{
    if (!ams) return false;
    this->ams_id = ams->id;
    cans.clear();
    for (int i = 0; i < 4; i++) {
        auto    it = ams->trayList.find(std::to_string(i));
        Caninfo info;
        if (it != ams->trayList.end()) {
            info.can_id          = it->second->id;
            info.material_name   = it->second->type;
            info.material_colour = decode_color(it->second->color);
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

AMSrefresh::AMSrefresh(wxWindow *parent, wxWindowID id, wxString number, const wxPoint &pos, const wxSize &size) : AMSrefresh()
{
    m_text = number;
    create(parent, id, pos, size);
}

AMSrefresh::AMSrefresh(wxWindow *parent, wxWindowID id, int number, const wxPoint &pos, const wxSize &size) : AMSrefresh()
{
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

    m_bitmap_normal   = create_scaled_bitmap("ams_refresh_normal", this, AMS_REFRESH_SIZE.y);
    m_bitmap_selected = create_scaled_bitmap("ams_refresh_selected", this, AMS_REFRESH_SIZE.y);
    SetSize(AMS_REFRESH_SIZE);
    SetMinSize(AMS_REFRESH_SIZE);
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

void AMSrefresh::paintEvent(wxPaintEvent &evt)
{
    wxSize    size = GetSize();
    wxPaintDC dc(this);

    auto pot = wxPoint((size.x - m_bitmap_selected.GetSize().x) / 2, (size.y - m_bitmap_selected.GetSize().y) / 2);
    dc.DrawBitmap(m_selected ? m_bitmap_selected : m_bitmap_normal, pot);
    dc.SetPen(wxPen(wxColour(107, 107, 107)));
    dc.SetBrush(wxBrush(wxColour(107, 107, 107)));

    dc.SetFont(Label::Body_10);
    auto tsize = dc.GetMultiLineTextExtent(m_text);
    pot        = wxPoint((size.x - tsize.x) / 2 - 2, (size.y - tsize.y) / 2);
    dc.DrawText(m_text, pot);
}

void AMSrefresh::DoSetSize(int x, int y, int width, int height, int sizeFlags) { wxWindow::DoSetSize(x, y, width, height, sizeFlags); }

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
AMSLib::AMSLib() : m_border_color(wxColour(130, 130, 128)), m_lib_color(AMS_CONTROL_WHITE_COLOUR), m_road_def_color(AMS_CONTROL_ROAD_DEF_COLOUR) { SetFont(Label::Body_12); }

AMSLib::AMSLib(wxWindow *parent, wxWindowID id, Caninfo info, const wxPoint &pos, const wxSize &size) : AMSLib()
{
    Update(info, false);
    create(parent, id, pos, size);
    Bind(wxEVT_PAINT, &AMSLib::paintEvent, this);
    //Bind(wxEVT_LEFT_DOWN, &AMSLib::OnSelected, this);
    wxWindow::SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    m_bitmap_editable = create_scaled_bitmap("ams_editable", this, FromDIP(14));
}

void AMSLib::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size) { wxWindow::Create(parent, id, pos, size); }

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
        UnableSelected();
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

void AMSLib::OnSelected()
{
    if (m_unable_selected) return;
    

   /* auto event = SimpleEvent(EVT_AMS_SETLIB);
     event.SetId(m_can_index);
     event.SetEventObject(m_parent);
     wxPostEvent(m_parent, event);*/

    m_selected = true;
    Refresh();
}

void AMSLib::UnSelected()
{
    m_selected = false;
    Refresh();
}

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
    wxSize size = GetSize();

    // selected
    if (m_selected) {
        // lib
        dc.SetPen(wxPen(m_lib_color, 2, wxSOLID));
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        if (m_radius == 0) {
            dc.DrawRectangle(0, 0, size.x, size.y);
        } else {
            dc.DrawRoundedRectangle(FromDIP(1), FromDIP(1), size.x - FromDIP(1), size.y - FromDIP(1), m_radius);
        }

        dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
        dc.SetBrush(wxBrush(m_lib_color));
        if (m_radius == 0) {
            dc.DrawRectangle(0, 0, size.x, size.y);
        } else {
            dc.DrawRoundedRectangle(FromDIP(4), FromDIP(4), size.x - FromDIP(8), size.y - FromDIP(8), m_radius);
        }
    } else {
        // lib
        dc.SetPen(wxPen(m_border_color, 1, wxSOLID));
        dc.SetBrush(wxBrush(m_lib_color));
        if (m_radius == 0) {
            dc.DrawRectangle(0, 0, size.x, size.y);
        } else {
            dc.DrawRoundedRectangle(FromDIP(4), FromDIP(4), size.x - FromDIP(8), size.y - FromDIP(8), m_radius);
        }
    }

    // text
    if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_BRAND || m_info.material_state == AMSCanType::AMS_CAN_TYPE_NONE) {
        auto                     textcolor = AMS_CONTROL_WHITE_COLOUR;
        Slic3r::GUI::BitmapCache bmcache;
        float                    gray = 0.299 * m_lib_color.Red() + 0.587 * m_lib_color.Green() + 0.114 * m_lib_color.Blue();
        if (gray < 130)
            textcolor = AMS_CONTROL_WHITE_COLOUR;
        else
            textcolor = AMS_CONTROL_BLACK_COLOUR;

        auto tsize = dc.GetMultiLineTextExtent(m_info.material_name);
        auto pot   = wxPoint((size.x - tsize.x) / 2, size.y * 0.7);
        dc.SetFont(::Label::Head_12);
        dc.SetTextForeground(textcolor);
        dc.DrawText(m_info.material_name, pot);
    }

    // edit
    if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_THIRDBRAND) { dc.DrawBitmap(m_bitmap_editable, {(size.x - m_bitmap_editable.GetSize().x) / 2, int(size.y * 0.7)}); }
}
/*************************************************
Description:AMSRoad
**************************************************/
AMSRoad::AMSRoad() : m_road_def_color(AMS_CONTROL_ROAD_DEF_COLOUR), m_road_color(AMS_CONTROL_ROAD_DEF_COLOUR) {}
AMSRoad::AMSRoad(wxWindow *parent, wxWindowID id, Caninfo info, int canindex, int maxcan, const wxPoint &pos, const wxSize &size) : AMSRoad()
{
    m_caninfo  = info;
    m_canindex = canindex;
    // road type
    auto mode = AMSRoadMode::AMS_ROAD_MODE_END;
    if (m_canindex == 0) {
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
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_END) {
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
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_END) {
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

void AMSItem::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    wxWindow::Create(parent, id, pos, size);
    SetBackgroundColour(AMS_CONTROL_WHITE_COLOUR);
    SetCubeSize(AMS_ITEM_CUBE_SIZE);
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

void AMSItem::SetCubeSize(wxSize size)
{
    /*  m_cube_size = size;
      wxSize new_size;
      new_size.x = m_cube_size.x * 4 + m_space * 3 + m_padding * 2;
      new_size.y = m_cube_size.y + m_padding * 2;*/
    SetSize(AMS_ITEM_SIZE);
    SetMinSize(AMS_ITEM_SIZE);
    Refresh();
}

void AMSItem::OnSelected()
{
    m_selected = true;
    Refresh();
}

void AMSItem::UnSelected()
{
    m_selected = false;
    Refresh();
}

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
        dc.SetBrush(wxBrush(iter->material_colour));
        dc.DrawRoundedRectangle(left, (size.y - AMS_ITEM_CUBE_SIZE.y) / 2, AMS_ITEM_CUBE_SIZE.x, AMS_ITEM_CUBE_SIZE.y, 2);
        left += AMS_ITEM_CUBE_SIZE.x;
        left += m_space;
    }

    if (m_hover) {
        dc.SetPen(wxPen(m_border_colour, 1));
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        dc.DrawRoundedRectangle(0, 0, size.x, size.y, 3);
    }

    if (m_selected) {
        dc.SetPen(wxPen(m_border_colour, 1));
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
    // sizer_can->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(30));
    UpdateCan(info);
}

void AmsCans::UpdateCan(AMSinfo info)
{
    m_info = info;
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

void AmsCans::AddCan(Caninfo caninfo, int canindex, int maxcan)
{
    auto        amscan      = new wxWindow(this, wxID_ANY);
    wxBoxSizer *m_sizer_ams = new wxBoxSizer(wxVERTICAL);
    m_sizer_ams->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(14));
    auto m_panel_refresh = new AMSrefresh(amscan, wxID_ANY, m_can_count + 1);
    m_sizer_ams->Add(m_panel_refresh, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    m_sizer_ams->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(2));
    auto m_panel_lib  = new AMSLib(amscan, wxID_ANY, caninfo, wxDefaultPosition, AMS_CAN_LIB_SIZE);
    m_panel_lib->Bind(wxEVT_LEFT_DOWN, [this, canindex, caninfo](wxMouseEvent &ev) {
        m_canlib_selection = canindex;
        m_canlib_id        = caninfo.can_id;

       CanLibsHash::iterator ci = m_can_lib_list.begin();
        while (ci != m_can_lib_list.end()) {
            CanLibs *cust = ci->second;
            if (cust->canLib->m_can_id == m_canlib_id) {
                cust->canLib->OnSelected();
            } else {
                cust->canLib->UnSelected();
            }

            ci++;
        }
    });
    m_panel_lib->m_can_id = caninfo.can_id;
    m_panel_lib->m_can_index = canindex;
    auto m_panel_road = new AMSRoad(amscan, wxID_ANY, caninfo, canindex, maxcan, wxDefaultPosition, wxSize(-1, AMS_CAN_ROAD_SIZE));
    m_sizer_ams->Add(m_panel_lib, 1, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, FromDIP(4));
    m_sizer_ams->Add(m_panel_road, 0, wxEXPAND, 0);

    amscan->SetSizer(m_sizer_ams);
    amscan->Layout();
    m_sizer_ams->Fit(amscan);
    sizer_can->Add(amscan, 1, wxALL, 0);

    CanLibs *canlib               = new CanLibs;
    canlib->canID                 = caninfo.can_id;
    canlib->canLib                = m_panel_lib;
    m_can_lib_list[canlib->canID] = canlib;

    CanRoads *canroad              = new CanRoads;
    canroad->canID                 = caninfo.can_id;
    canroad->canRoad               = m_panel_road;
    m_can_road_list[canlib->canID] = canroad;
}

void AmsCans::SetAmsStep(wxString canid, AMSPassRoadType type, AMSPassRoadSTEP step)
{
    auto                    tag_can_index = -1;
    CansRoadsHash::iterator i             = m_can_road_list.begin();
    while (i != m_can_road_list.end()) {
        wxString  id       = i->first;
        CanRoads *tag_road = i->second;
        if (canid == id) tag_can_index = tag_road->canRoad->m_canindex;
        i++;
    }

    if (tag_can_index == -1) return;

    // unload
    if (type == AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD) {
        auto iter = m_can_road_list.begin();
        while (iter != m_can_road_list.end()) {
            CanRoads *road = iter->second;

            auto index = road->canRoad->m_canindex;
            auto pr    = std::vector<AMSPassRoadMode>{};

            pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_BOTTOM);
            if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_2) {
                if (index == tag_can_index && index > 0) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_LEFT); }
                if (index < tag_can_index && index > 0) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_LEFT_RIGHT); }
                if (index == 0 && tag_can_index == index) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_TOP); }
                if (index == 0 && tag_can_index > index) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_RIGHT); }
            }
            road->canRoad->OnPassRoad(pr);
            iter++;
        }
    }

    // load
    if (type == AMSPassRoadType::AMS_ROAD_TYPE_LOAD) {
        auto iter = m_can_road_list.begin();
        while (iter != m_can_road_list.end()) {
            CanRoads *road = iter->second;

            auto index = road->canRoad->m_canindex;
            auto pr    = std::vector<AMSPassRoadMode>{};

            if (index == tag_can_index && index > 0) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_LEFT); }
            if (index < tag_can_index && index > 0) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_LEFT_RIGHT); }
            if (index == 0 && tag_can_index == index) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_TOP); }
            if (index == 0 && tag_can_index > index) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_RIGHT); }

            if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_2) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_BOTTOM); }

            road->canRoad->OnPassRoad(pr);
            iter++;
        }
    }
}

/*************************************************
Description:AMSControl
**************************************************/
AMSControl::AMSControl(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size) : wxWindow(parent, wxID_ANY, pos, size)
{
    this->SetBackgroundColour(*wxWHITE);
    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxVERTICAL);

    Freeze();
    // top - ams tag
    m_panel_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, AMS_CAN_ITEM_HEIGHT_SIZE), wxTAB_TRAVERSAL);
    m_sizer_top = new wxBoxSizer(wxHORIZONTAL);
    m_panel_top->SetSizer(m_sizer_top);
    m_panel_top->Layout();
    m_sizer_top->Fit(m_panel_top);

    m_sizer_body->Add(m_panel_top, 0, wxEXPAND, 0);

    m_sizer_body->Add(0, 0, 1, wxEXPAND | wxTOP, 18);

    wxBoxSizer *m_sizer_bottom = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *m_sizer_left = new wxBoxSizer(wxVERTICAL);

    m_panel_can = new StaticBox(this, wxID_ANY, wxDefaultPosition, AMS_CANS_SIZE, wxTAB_TRAVERSAL | wxBORDER_NONE);
    m_panel_can->SetMinSize(AMS_CANS_SIZE);
    m_panel_can->SetCornerRadius(6);
    m_panel_can->SetBackgroundColor(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);

    m_sizer_cans = new wxBoxSizer(wxHORIZONTAL);
    m_simplebook = new wxSimplebook(m_panel_can, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0);
    m_simplebook->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    m_sizer_cans->Add(m_simplebook, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

    m_panel_can->SetSizer(m_sizer_cans);
    m_panel_can->Layout();
    m_sizer_cans->Fit(m_panel_can);

    m_sizer_left->Add(m_panel_can, 1, wxEXPAND, 0);

    wxBoxSizer *m_sizer_left_bottom = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *sizer_sextruder     = new wxBoxSizer(wxHORIZONTAL);

    auto extruder = new AMSextruder(this, wxID_ANY, wxDefaultPosition, AMS_EXTRUDER_SIZE);
    extruder->TurnOn();
    sizer_sextruder->Add(extruder, 1, wxEXPAND | wxALL, 0);
    m_sizer_left_bottom->Add(sizer_sextruder, 1, wxEXPAND, 0);

    m_sizer_left_bottom->Add(0, 0, 0, wxEXPAND, 0);
    m_sizer_left_bottom->Add(0, 0, 0, wxALL | wxLEFT, 26);

    StateColor extruder_bg(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                           std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));

    StateColor extruder_bd(std::pair<wxColour, int>(wxColour(107, 107, 107), StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));

    m_button_extruder_feed = new Button(this, _L("Feed"));
    m_button_extruder_feed->SetBackgroundColor(extruder_bg);
    m_button_extruder_feed->SetBorderColor(extruder_bd);
    m_button_extruder_feed->SetFont(Label::Body_10);
    m_sizer_left_bottom->Add(m_button_extruder_feed, 0, wxTOP, 20);

    m_sizer_left_bottom->Add(0, 0, 0, wxALL | wxLEFT, 10);

    m_button_extruder_back = new Button(this, _L("Back"));
    m_button_extruder_back->SetBackgroundColor(extruder_bg);
    m_button_extruder_back->SetBorderColor(extruder_bd);
    m_button_extruder_back->SetFont(Label::Body_10);
    m_sizer_left_bottom->Add(m_button_extruder_back, 0, wxTOP, 20);

    m_sizer_left->Add(m_sizer_left_bottom, 0, wxEXPAND, 5);

    m_sizer_bottom->Add(m_sizer_left, 0, wxEXPAND, 5);

    m_sizer_bottom->Add(0, 0, 0, wxEXPAND | wxLEFT, 43);

    wxBoxSizer *m_sizer_right = new wxBoxSizer(wxVERTICAL);

    m_filament_step = new ::StepIndicator(this, wxID_ANY);
    m_filament_step->SetMinSize(AMS_STEP_SIZE);
    m_filament_step->SetSize(AMS_STEP_SIZE);
    m_sizer_right->Add(m_filament_step, 0, wxALL, 0);

    wxBoxSizer *m_sizer_right_bottom = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_right_bottom->Add(0, 0, 1, wxEXPAND, 5);

    m_button_ams_setting = new Button(this, _L("Ams set"));
    m_button_ams_setting->SetBackgroundColor(extruder_bg);
    m_button_ams_setting->SetBorderColor(extruder_bd);
    m_button_ams_setting->SetFont(Label::Body_10);
    m_sizer_right_bottom->Add(m_button_ams_setting, 0, wxTOP, 20);

    m_sizer_right->Add(m_sizer_right_bottom, 0, wxEXPAND, 5);

    m_sizer_bottom->Add(m_sizer_right, 0, wxEXPAND, 5);

    m_sizer_body->Add(m_sizer_bottom, 0, wxEXPAND | wxLEFT, 11);

    init_scaled_buttons();
    SetSizer(m_sizer_body);
    Layout();
    Fit();

    Thaw();

    UpdateStepCtrl();

    m_button_extruder_feed->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AMSControl::on_extruder_feed), NULL, this);   // TODO
    m_button_extruder_back->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AMSControl::on_extruder_return), NULL, this); // TODO
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

std::string AMSControl::GetCurentAms() 
{ 
    return m_current_ams; 
}

std::string AMSControl::GetCurrentCan(std::string amsid) 
{
    std::string           current_can;
    AmsCansHash::iterator ci = m_ams_cans_list.begin();
    while (ci != m_ams_cans_list.end()) {
        AmsCansWindow *ams = ci->second;
        if (ci->first == amsid) {

            current_can = ams->amsCans->GetCurrentCan();
          

            return current_can;
        }
        ci++;
    }

    return current_can;
}

void AMSControl::msw_rescale()
{
    m_sizer_top->Layout();
    Layout();
}

void AMSControl::UpdateStepCtrl()
{
    for (int i = 0; i < (int)FilamentStep::STEP_COUNT; i++) {
        m_filament_step->AppendItem(FILAMENT_STEP_STRING[i]);
    }
}

void AMSControl::UpdateAms(std::vector<AMSinfo> info)
{
    m_ams_info = info;
    std::vector<AMSinfo>::iterator it;
    Freeze();
    for (it = info.begin(); it != info.end(); it++) { AddAms(*it, true); }
    m_sizer_top->Layout();
    Thaw();

    if (m_current_senect.empty() && info.size() > 0) { SwitchAms(info[0].ams_id);}
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

    AmsItems *item               = new AmsItems;
    item->amsID                  = info.ams_id;
    item->amsItem                = amsitem;
    m_ams_item_list[item->amsID] = item;
    m_sizer_top->Add(amsitem, 0, wxALL | wxEXPAND, 3);

    AmsCansWindow *canswin = new AmsCansWindow();
    auto           amscans = new AmsCans(m_simplebook, wxID_ANY, info);

    canswin->amsID                  = info.ams_id;
    canswin->amsCans                = amscans;
    m_ams_cans_list[canswin->amsID] = canswin;

    m_simplebook->AddPage(amscans, wxEmptyString, false);
    amscans->m_selection = m_simplebook->GetPageCount() - 1;

    if (refresh) { m_sizer_top->Layout(); }
    m_ams_count++;
    m_ams_info.push_back(info);
}

void AMSControl::RemoveAms(std::string ams_id)
{
    AmsItemsHash::iterator ii = m_ams_item_list.begin();
    while (ii != m_ams_item_list.end()) {
        AmsItems *cust = ii->second;
        if (ii->first == ams_id) {
            cust->amsItem->Destroy();
            m_ams_item_list.erase(ii);
            delete cust;
            break;
        }
        ii++;
    }
    AmsCansHash::iterator ci = m_ams_cans_list.begin();
    while (ci != m_ams_cans_list.end()) {
        AmsCansWindow *cust = ci->second;
        if (ci->first == ams_id) {
            cust->amsCans->Destroy();
            m_ams_cans_list.erase(ci);
            delete cust;
            break;
        }
        ci++;
    }

    m_sizer_top->Layout();
    m_ams_count--;
    m_ams_info.pop_back();
}

void AMSControl::RemoveAll()
{
    m_simplebook->DeleteAllPages();
    AmsItemsHash::iterator ii = m_ams_item_list.begin();
    while (ii != m_ams_item_list.end()) {
        AmsItems *cust = ii->second;
        cust->amsItem->Destroy();
        ii++;
    }

  /*  AmsCansHash::iterator ci = m_ams_cans_list.begin();
      while (ci != m_ams_cans_list.end()) {
          AmsCansWindow *cust = ci->second;
          cust->amsCans->Destroy();
          ci++;
      }*/

    m_ams_item_list.clear();
    m_ams_cans_list.clear();

    //m_sizer_cans->Layout();
    m_sizer_top->Layout();

    m_ams_count      = 0;
    m_current_senect = "";
    m_ams_info.clear();
}

void AMSControl::SwitchAms(std::string ams_id)
{
    AmsItemsHash::iterator ii = m_ams_item_list.begin();
    while (ii != m_ams_item_list.end()) {
        AmsItems *cust = ii->second;
        if (ii->first == ams_id) {
            cust->amsItem->OnSelected();
            m_current_senect = ams_id;
        } else {
            cust->amsItem->UnSelected();
        }
        ii++;
    }

    AmsCansHash::iterator ci = m_ams_cans_list.begin();
    while (ci != m_ams_cans_list.end()) {
        AmsCansWindow *cust = ci->second;
        if (ci->first == ams_id) { m_simplebook->SetSelection(cust->amsCans->m_selection); } 
        ci++;
    }

    m_current_ams = ams_id;
}

void AMSControl::SetFilamentStep(int item_idx)
{
    if (item_idx >= 0 && item_idx < FilamentStep::STEP_COUNT)
        m_filament_step->SelectItem(item_idx);
}

void AMSControl::SetAmsStep(std::string ams_id, std::string canid, AMSPassRoadType type, AMSPassRoadSTEP STEP)
{
    AmsCansHash::iterator iter     = m_ams_cans_list.find(ams_id);
    bool                  notfound = (iter == m_ams_cans_list.end());
    if (notfound) return;
    AmsCansWindow *cust = iter->second;
    //cust->amsCans->SetAmsStep(canid, type, STEP);
}

void AMSControl::on_extruder_feed(wxCommandEvent &event)
{
    post_event(SimpleEvent(EVT_AMS_FEED));
}

void AMSControl::on_extruder_return(wxCommandEvent &event)
{
   post_event(SimpleEvent(EVT_AMS_RETURN));
}

void AMSControl::post_event(wxEvent &&event)
{
    event.SetEventObject(m_parent);
    wxPostEvent(m_parent, event);
}

}
}