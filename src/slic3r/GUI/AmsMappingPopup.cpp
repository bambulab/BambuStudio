#include "AmsMappingPopup.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/Thread.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_Preview.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/StaticBox.hpp"

#include <wx/progdlg.h>
#include <wx/clipbrd.h>
#include <wx/dcgraph.h>
#include <miniz.h>
#include <algorithm>
#include "Plater.hpp"
#include "BitmapCache.hpp"
#include "BindDialog.hpp"

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_SET_FINISH_MAPPING, wxCommandEvent);

 MaterialItem::MaterialItem(wxWindow *parent, wxColour mcolour, wxString mname) 
 : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
 {
    m_arraw_bitmap_gray =  ScalableBitmap(this, "drop_down", FromDIP(12));
    m_arraw_bitmap_white =  ScalableBitmap(this, "topbar_dropdown", FromDIP(12));


    m_material_coloul = mcolour;
    m_material_name = mname;
    m_ams_coloul      = wxColour(0xEE,0xEE,0xEE);

#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    SetSize(MATERIAL_ITEM_SIZE);
    SetMinSize(MATERIAL_ITEM_SIZE);
    SetMaxSize(MATERIAL_ITEM_SIZE);
    SetBackgroundColour(*wxWHITE);

    m_main_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    auto main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_main_panel, 1, wxEXPAND);
    this->SetSizer(main_sizer);
    this->Layout();

    m_main_panel->Bind(wxEVT_PAINT, &MaterialItem::paintEvent, this);
    wxGetApp().UpdateDarkUI(this);
 }

 MaterialItem::~MaterialItem() {}

void MaterialItem::msw_rescale() {}

void MaterialItem::set_ams_info(wxColour col, wxString txt)
{
    auto need_refresh = false;
    if (m_ams_coloul != col) { m_ams_coloul = col; need_refresh = true;}
    if (m_ams_name != txt) {m_ams_name   = txt;need_refresh = true;}
    if (need_refresh) { Refresh();}
}

void MaterialItem::on_selected()
{
    if (!m_selected) {
        m_selected = true;
        Refresh();
    }
}

void MaterialItem::on_warning()
{
    if (!m_warning) {
        m_warning = true;
        Refresh();
    }
}

void MaterialItem::on_normal()
{
    if (m_selected || m_warning) {
        m_selected = false;
        m_warning  = false;
        Refresh();
    }
}


void MaterialItem::paintEvent(wxPaintEvent &evt) 
{  
    wxPaintDC dc(m_main_panel);
    render(dc);

    //PrepareDC(buffdc);
    //PrepareDC(dc);
    
}

void MaterialItem::render(wxDC &dc) 
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

    // materials name
    dc.SetFont(::Label::Body_13);

    auto material_name_colour = m_material_coloul.GetLuminance() < 0.5 ? *wxWHITE : wxColour(0x26, 0x2E, 0x30);
    dc.SetTextForeground(material_name_colour);

    if (dc.GetTextExtent(m_material_name).x > GetSize().x - 10) {
        dc.SetFont(::Label::Body_10);

    }

    auto material_txt_size = dc.GetTextExtent(m_material_name);
    dc.DrawText(m_material_name, wxPoint((MATERIAL_ITEM_SIZE.x - material_txt_size.x) / 2, (FromDIP(22) - material_txt_size.y) / 2));

    // mapping num
    dc.SetFont(::Label::Body_10);
    dc.SetTextForeground(m_ams_coloul.GetLuminance() < 0.5 ? *wxWHITE : wxColour(0x26, 0x2E, 0x30));

    wxString mapping_txt = wxEmptyString;
    if (m_ams_name.empty()) {
        mapping_txt = "-";
    } else {
        mapping_txt = m_ams_name;
    }

    auto mapping_txt_size = dc.GetTextExtent(mapping_txt);
    dc.DrawText(mapping_txt, wxPoint((MATERIAL_ITEM_SIZE.x - mapping_txt_size.x) / 2, FromDIP(20) + (FromDIP(14) - mapping_txt_size.y) / 2));
}

void MaterialItem::doRender(wxDC &dc) 
{
    //top
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(m_material_coloul));
    dc.DrawRoundedRectangle(FromDIP(1), FromDIP(1), MATERIAL_ITEM_REAL_SIZE.x, FromDIP(18), 5);
    
    //bottom
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(wxColour(m_ams_coloul)));
    dc.DrawRoundedRectangle(FromDIP(1), FromDIP(18), MATERIAL_ITEM_REAL_SIZE.x, FromDIP(16), 5);

    ////middle
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(m_material_coloul));
    dc.DrawRectangle(FromDIP(1), FromDIP(11), MATERIAL_ITEM_REAL_SIZE.x, FromDIP(8));

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(m_ams_coloul));
    dc.DrawRectangle(FromDIP(1), FromDIP(18), MATERIAL_ITEM_REAL_SIZE.x, FromDIP(8));

    ////border
#if __APPLE__
    if (m_material_coloul == *wxWHITE || m_ams_coloul == *wxWHITE) {
        dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(1, 1, MATERIAL_ITEM_SIZE.x - 1, MATERIAL_ITEM_SIZE.y - 1, 5);
    }

    if (m_selected) {
        dc.SetPen(wxColour(0x00, 0xAE, 0x42));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(1, 1, MATERIAL_ITEM_SIZE.x - 1, MATERIAL_ITEM_SIZE.y - 1, 5);
    }
#else
    if (m_material_coloul == *wxWHITE || m_ams_coloul == *wxWHITE) {
        dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(0, 0, MATERIAL_ITEM_SIZE.x, MATERIAL_ITEM_SIZE.y, 5);
    }

    if (m_selected) {
        dc.SetPen(wxColour(0x00, 0xAE, 0x42));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(0, 0, MATERIAL_ITEM_SIZE.x, MATERIAL_ITEM_SIZE.y, 5);
    }
#endif
    //arrow

    if ( (m_ams_coloul.Red() > 160 && m_ams_coloul.Green() > 160 && m_ams_coloul.Blue() > 160) &&
        (m_ams_coloul.Red() < 180 && m_ams_coloul.Green() < 180 && m_ams_coloul.Blue() < 180)) {
        dc.DrawBitmap(m_arraw_bitmap_white.bmp(), GetSize().x - m_arraw_bitmap_white.GetBmpSize().x - FromDIP(7),  GetSize().y - m_arraw_bitmap_white.GetBmpSize().y);
    }
    else {
        dc.DrawBitmap(m_arraw_bitmap_gray.bmp(), GetSize().x - m_arraw_bitmap_gray.GetBmpSize().x - FromDIP(7),  GetSize().y - m_arraw_bitmap_gray.GetBmpSize().y);
    }

    
}

 AmsMapingPopup::AmsMapingPopup(wxWindow *parent) 
    : PopupWindow(parent, wxBORDER_NONE)
 {
     SetSize(wxSize(FromDIP(252), -1));
     SetMinSize(wxSize(FromDIP(252), -1));
     SetMaxSize(wxSize(FromDIP(252), -1));
     Bind(wxEVT_PAINT, &AmsMapingPopup::paintEvent, this);


     #if __APPLE__
     Bind(wxEVT_LEFT_DOWN, &AmsMapingPopup::on_left_down, this); 
     #endif

     SetBackgroundColour(*wxWHITE);
     m_sizer_main         = new wxBoxSizer(wxVERTICAL);
     //m_sizer_main->Add(0, 0, 1, wxEXPAND, 0);

     auto title_panel = new wxPanel(this, wxID_ANY);
     title_panel->SetBackgroundColour(wxColour(0xF8, 0xF8, 0xF8));
     title_panel->SetSize(wxSize(-1, FromDIP(30)));
     title_panel->SetMinSize(wxSize(-1, FromDIP(30)));
     

     wxBoxSizer *title_sizer_h= new wxBoxSizer(wxHORIZONTAL);

     wxBoxSizer *title_sizer_v = new wxBoxSizer(wxVERTICAL);

     auto title_text = new wxStaticText(title_panel, wxID_ANY, _L("AMS Slots"));
     title_text->SetForegroundColour(wxColour(0x32, 0x3A, 0x3D));
     title_text->SetFont(::Label::Head_13);
     title_sizer_v->Add(title_text, 0, wxALIGN_CENTER, 5);
     title_sizer_h->Add(title_sizer_v, 1, wxALIGN_CENTER, 5);
     title_panel->SetSizer(title_sizer_h);
     title_panel->Layout();
     title_panel->Fit();

     m_sizer_list = new wxBoxSizer(wxVERTICAL);
     for (auto i = 0; i < AMS_TOTAL_COUNT; i++) {
         auto sizer_mapping_list = new wxBoxSizer(wxHORIZONTAL);
         /*auto ams_mapping_item_container = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("ams_mapping_container", this, 78), wxDefaultPosition,
             wxSize(FromDIP(230), FromDIP(78)), 0);*/
         auto ams_mapping_item_container = new MappingContainer(this);
         ams_mapping_item_container->SetSizer(sizer_mapping_list);
         ams_mapping_item_container->Layout();
         //ams_mapping_item_container->Hide();
         m_amsmapping_container_sizer_list.push_back(sizer_mapping_list);
         m_amsmapping_container_list.push_back(ams_mapping_item_container);
         m_sizer_list->Add(ams_mapping_item_container, 0, wxALIGN_CENTER_HORIZONTAL|wxTOP|wxBOTTOM, FromDIP(5));
     }

     m_warning_text = new wxStaticText(this, wxID_ANY, wxEmptyString);
     m_warning_text->SetForegroundColour(wxColour(0xFF, 0x6F, 0x00));
     m_warning_text->SetFont(::Label::Body_12);
     auto cant_not_match_tip = _L("Note: Only the AMS slots loaded with the same material type can be selected.");
     m_warning_text->SetLabel(format_text(cant_not_match_tip));
     m_warning_text->SetMinSize(wxSize(FromDIP(248), FromDIP(-1)));
     m_warning_text->Wrap(FromDIP(248));

     m_sizer_main->Add(title_panel, 0, wxEXPAND | wxALL, FromDIP(2));
     m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(5));
     m_sizer_main->Add(m_sizer_list, 0, wxEXPAND | wxALL, FromDIP(0));
     m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(5));
     m_sizer_main->Add(m_warning_text, 0, wxEXPAND | wxALL, FromDIP(6));

     SetSizer(m_sizer_main);
     Layout();
     Fit();
 }

 wxString AmsMapingPopup::format_text(wxString &m_msg)
 {
     if (wxGetApp().app_config->get("language") != "zh_CN") { return m_msg; }

     wxString out_txt      = m_msg;
     wxString count_txt    = "";
     int      new_line_pos = 0;

     for (int i = 0; i < m_msg.length(); i++) {
         auto text_size = m_warning_text->GetTextExtent(count_txt);
         if (text_size.x < (FromDIP(280))) {
             count_txt += m_msg[i];
         } else {
             out_txt.insert(i - 1, '\n');
             count_txt = "";
         }
     }
     return out_txt;
 }

void AmsMapingPopup::update_materials_list(std::vector<std::string> list) 
{ 
    m_materials_list = list;
}

void AmsMapingPopup::set_tag_texture(std::string texture) 
{ 
    m_tag_material = texture;
}


bool AmsMapingPopup::is_match_material(std::string material)
{
    return m_tag_material == material ? true : false;
}


void AmsMapingPopup::on_left_down(wxMouseEvent &evt)
{
    auto pos = ClientToScreen(evt.GetPosition());
    for (MappingItem *item : m_mapping_item_list) {
        auto p_rect = item->ClientToScreen(wxPoint(0, 0));
        auto left = item->GetSize();

        if (pos.x > p_rect.x && pos.y > p_rect.y && pos.x < (p_rect.x + item->GetSize().x) && pos.y < (p_rect.y + item->GetSize().y)) {
            if (item->m_tray_data.type == TrayType::NORMAL  && !is_match_material(item->m_tray_data.filament_type)) return;
            item->send_event(m_current_filament_id);
            Dismiss();
        }
    }
}

void AmsMapingPopup::update_ams_data(std::map<std::string, Ams*> amsList) 
{ 
    m_has_unmatch_filament = false;
    //m_mapping_item_list.clear();

    for (auto& ams_container : m_amsmapping_container_list) {
        ams_container->Hide();
    }


    for (wxWindow *mitem : m_mapping_item_list) {
        mitem->Destroy();
        mitem = nullptr;
    }
     m_mapping_item_list.clear();

    if (m_amsmapping_container_sizer_list.size() > 0) {
        for (wxBoxSizer *siz : m_amsmapping_container_sizer_list) { 
            siz->Clear(true); 
        }
    }
   
    std::map<std::string, Ams *>::iterator ams_iter;

    BOOST_LOG_TRIVIAL(trace) << "ams_mapping total count " << amsList.size();
    int m_amsmapping_container_list_index = 0;

    for (ams_iter = amsList.begin(); ams_iter != amsList.end(); ams_iter++) {
        
        BOOST_LOG_TRIVIAL(trace) << "ams_mapping ams id " << ams_iter->first.c_str();

        auto ams_indx = atoi(ams_iter->first.c_str());
        Ams *ams_group = ams_iter->second;
        std::vector<TrayData>                      tray_datas;
        std::map<std::string, AmsTray *>::iterator tray_iter;

        for (tray_iter = ams_group->trayList.begin(); tray_iter != ams_group->trayList.end(); tray_iter++) {
            AmsTray *tray_data = tray_iter->second;
            TrayData td;

            td.id = ams_indx * AMS_TOTAL_COUNT + atoi(tray_data->id.c_str());

            if (!tray_data->is_exists) {
                td.type = EMPTY;
            } else {
                if (!tray_data->is_tray_info_ready()) {
                    td.type = THIRD;
                } else {
                    td.type   = NORMAL;
                    td.colour = AmsTray::decode_color(tray_data->color);
                    td.name   = tray_data->get_display_filament_type();
                    td.filament_type = tray_data->get_filament_type();
                }
            }

            tray_datas.push_back(td);
        }

        m_amsmapping_container_list[m_amsmapping_container_list_index]->Show();
        add_ams_mapping(tray_datas, m_amsmapping_container_list[m_amsmapping_container_list_index], m_amsmapping_container_sizer_list[m_amsmapping_container_list_index]);
        m_amsmapping_container_list_index++;
    }


    m_warning_text->Show(m_has_unmatch_filament);
    Layout();
    Fit();
}

std::vector<TrayData> AmsMapingPopup::parse_ams_mapping(std::map<std::string, Ams*> amsList)
{
    std::vector<TrayData> m_tray_data;
    std::map<std::string, Ams *>::iterator ams_iter;

    for (ams_iter = amsList.begin(); ams_iter != amsList.end(); ams_iter++) {

        BOOST_LOG_TRIVIAL(trace) << "ams_mapping ams id " << ams_iter->first.c_str();

        auto ams_indx = atoi(ams_iter->first.c_str());
        Ams* ams_group = ams_iter->second;
        std::vector<TrayData>                      tray_datas;
        std::map<std::string, AmsTray*>::iterator tray_iter;

        for (tray_iter = ams_group->trayList.begin(); tray_iter != ams_group->trayList.end(); tray_iter++) {
            AmsTray* tray_data = tray_iter->second;
            TrayData td;

            td.id = ams_indx * AMS_TOTAL_COUNT + atoi(tray_data->id.c_str());

            if (!tray_data->is_exists) {
                td.type = EMPTY;
            }
            else {
                if (!tray_data->is_tray_info_ready()) {
                    td.type = THIRD;
                }
                else {
                    td.type = NORMAL;
                    td.colour = AmsTray::decode_color(tray_data->color);
                    td.name = tray_data->get_display_filament_type();
                    td.filament_type = tray_data->get_filament_type();
                }
            }

            m_tray_data.push_back(td);
        }
    }

    return m_tray_data;
}

void AmsMapingPopup::add_ams_mapping(std::vector<TrayData> tray_data, wxWindow* container, wxBoxSizer* sizer)
{ 
    sizer->Add(0,0,0,wxLEFT,FromDIP(6));
    for (auto i = 0; i < tray_data.size(); i++) {

        // set number
       /* auto number = new wxStaticText(this, wxID_ANY, wxGetApp().transition_tridid(tray_data[i].id), wxDefaultPosition, wxDefaultSize, 0);
        number->SetFont(::Label::Body_13);
        number->SetForegroundColour(wxColour(0X6B, 0X6B, 0X6B));
        number->Wrap(-1);*/
        

        // set button
        MappingItem *m_mapping_item = new MappingItem(container);
        m_mapping_item->SetSize(wxSize(FromDIP(68 * 0.7), FromDIP(100 * 0.6)));
        m_mapping_item->SetMinSize(wxSize(FromDIP(68 * 0.7), FromDIP(100 * 0.6)));
        m_mapping_item->SetMaxSize(wxSize(FromDIP(68 * 0.7), FromDIP(100 * 0.6)));
        //m_mapping_item->SetCornerRadius(5);
        m_mapping_item->SetFont(::Label::Body_12);
        m_mapping_item_list.push_back(m_mapping_item);

        if (tray_data[i].type == NORMAL) {
            if (is_match_material(tray_data[i].filament_type)) { 
                m_mapping_item->set_data(tray_data[i].colour, tray_data[i].name, tray_data[i]);
            } else {
                m_mapping_item->set_data(wxColour(0xEE,0xEE,0xEE), tray_data[i].name, tray_data[i], true);
                m_has_unmatch_filament = true;
            }

            m_mapping_item->Bind(wxEVT_LEFT_DOWN, [this, tray_data, i, m_mapping_item](wxMouseEvent &e) {
                if (!is_match_material(tray_data[i].filament_type)) return;
                m_mapping_item->send_event(m_current_filament_id);
                Dismiss();
            });
        }
        

        // temp
        if (tray_data[i].type == EMPTY) {
            m_mapping_item->set_data(wxColour(0xCE, 0xCE, 0xCE), "-", tray_data[i]);
            m_mapping_item->Bind(wxEVT_LEFT_DOWN, [this, tray_data, i, m_mapping_item](wxMouseEvent &e) {
                m_mapping_item->send_event(m_current_filament_id);
                Dismiss();
            });
        }

        // third party
        if (tray_data[i].type == THIRD) {
            m_mapping_item->set_data(wxColour(0xCE, 0xCE, 0xCE), "?", tray_data[i]);
            m_mapping_item->Bind(wxEVT_LEFT_DOWN, [this, tray_data, i, m_mapping_item](wxMouseEvent &e) {
                m_mapping_item->send_event(m_current_filament_id);
                Dismiss();
            });
        }


        //sizer_mapping_item->Add(number, 0, wxALIGN_CENTER_HORIZONTAL, 0);
        //sizer_mapping_item->Add(m_mapping_item, 0, wxALIGN_CENTER_HORIZONTAL, 0);
        m_mapping_item->set_tray_index(wxGetApp().transition_tridid(tray_data[i].id));
        sizer->Add(0,0,0,wxRIGHT,FromDIP(6));
        sizer->Add(m_mapping_item, 0, wxTOP, FromDIP(1));
    }

}

void AmsMapingPopup::OnDismiss()
{

}

bool AmsMapingPopup::ProcessLeftDown(wxMouseEvent &event) 
{
    return PopupWindow::ProcessLeftDown(event);
}

void AmsMapingPopup::paintEvent(wxPaintEvent &evt) 
{
    wxPaintDC dc(this);
    dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRoundedRectangle(0, 0, GetSize().x, GetSize().y, 0);
}

 MappingItem::MappingItem(wxWindow *parent) 
 : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__
    SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    Bind(wxEVT_PAINT, &MappingItem::paintEvent, this);
}

 MappingItem::~MappingItem() 
{
}


void MappingItem::send_event(int fliament_id) 
{
    auto number = wxGetApp().transition_tridid(m_tray_data.id);
    wxCommandEvent event(EVT_SET_FINISH_MAPPING);
    event.SetInt(m_tray_data.id);

    wxString param = wxString::Format("%d|%d|%d|%s|%d", m_coloul.Red(), m_coloul.Green(), m_coloul.Blue(), number, fliament_id);
    event.SetString(param);
    event.SetEventObject(this->GetParent()->GetParent());
    wxPostEvent(this->GetParent()->GetParent()->GetParent(), event);
}

 void MappingItem::msw_rescale() 
{
}

void MappingItem::paintEvent(wxPaintEvent &evt)
{
    wxPaintDC dc(this);
    render(dc);

    // PrepareDC(buffdc);
    // PrepareDC(dc);
}

void MappingItem::render(wxDC &dc)
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

    // materials name
    dc.SetFont(::Label::Head_13);

    auto txt_colour = m_coloul.GetLuminance() < 0.5 ? *wxWHITE : wxColour(0x26, 0x2E, 0x30);
    txt_colour      = m_unmatch ? wxColour(0xCE, 0xCE, 0xCE) : txt_colour;

    dc.SetTextForeground(txt_colour);

    /*if (dc.GetTextExtent(m_name).x > GetSize().x - 10) {
        dc.SetFont(::Label::Body_10);
        m_name = m_name.substr(0, 3) + "." + m_name.substr(m_name.length() - 1);
    }*/

    auto txt_size = dc.GetTextExtent(m_tray_index);
    auto top = (GetSize().y - MAPPING_ITEM_REAL_SIZE.y) / 2 + FromDIP(8);
    dc.DrawText(m_tray_index, wxPoint((GetSize().x - txt_size.x) / 2, top));


    top += txt_size.y + FromDIP(7);
    dc.SetFont(::Label::Body_12);
    txt_size = dc.GetTextExtent(m_name);
    dc.DrawText(m_name, wxPoint((GetSize().x - txt_size.x) / 2, top));
}

void MappingItem::set_data(wxColour colour, wxString name, TrayData data, bool unmatch)
{
    m_unmatch = unmatch;
    m_tray_data = data;
    if (m_coloul != colour || m_name != name) {
        m_coloul = colour;
        m_name   = name;
        Refresh();
    }
}

void MappingItem::doRender(wxDC &dc)
{
    dc.SetPen(m_coloul);
    dc.SetBrush(wxBrush(m_coloul));
    dc.DrawRectangle(0, (GetSize().y - MAPPING_ITEM_REAL_SIZE.y) / 2, MAPPING_ITEM_REAL_SIZE.x, MAPPING_ITEM_REAL_SIZE.y);

//    if (m_coloul == *wxWHITE) {
//        dc.SetPen(wxPen(wxColour(0xAC, 0xAC, 0xAC), 1));
//#ifdef __APPLE__
//        dc.DrawRectangle(1, 1, GetSize().x - 1, GetSize().y - 1);
//#else
//        dc.DrawRectangle(0, 0, tray_size.x, tray_size.y);
//#endif // __APPLE__
//    }

    wxColour side_colour = wxColour(0xE4E4E4);

    dc.SetPen(side_colour);
    dc.SetBrush(wxBrush(side_colour));
#ifdef __APPLE__
    dc.DrawRectangle(0, 0, FromDIP(4), GetSize().y);
    dc.DrawRectangle(GetSize().x - FromDIP(4), 0, FromDIP(4), GetSize().y);
#else
    dc.DrawRectangle(0, 0, FromDIP(4), GetSize().y);
    dc.DrawRectangle(GetSize().x - FromDIP(4), 0, FromDIP(4), GetSize().y);
#endif // __APPLE__
}

AmsMapingTipPopup::AmsMapingTipPopup(wxWindow *parent) 
    :PopupWindow(parent, wxBORDER_NONE)
{
    SetBackgroundColour(*wxWHITE);
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);

    m_sizer_main->Add(0, 0, 1, wxTOP, FromDIP(28));

    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_body->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(20));

    m_panel_enable_ams = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(220), -1), wxTAB_TRAVERSAL);
    m_panel_enable_ams->SetBackgroundColour(*wxWHITE);
    wxBoxSizer *sizer_enable_ams = new wxBoxSizer(wxVERTICAL);

    m_title_enable_ams = new wxStaticText(m_panel_enable_ams, wxID_ANY, _L("Enable AMS"), wxDefaultPosition, wxDefaultSize, 0);
    m_title_enable_ams->SetForegroundColour(*wxBLACK);
    m_title_enable_ams->SetBackgroundColour(*wxWHITE);
    m_title_enable_ams->Wrap(-1);
    sizer_enable_ams->Add(m_title_enable_ams, 0, 0, 0);

    m_tip_enable_ams = new wxStaticText(m_panel_enable_ams, wxID_ANY, _L("Print with filaments in the AMS"), wxDefaultPosition, wxDefaultSize, 0);
    m_tip_enable_ams->SetMinSize(wxSize(FromDIP(200), FromDIP(50)));
    m_tip_enable_ams->Wrap(FromDIP(200));
    m_tip_enable_ams->SetForegroundColour(*wxBLACK);
    m_tip_enable_ams->SetBackgroundColour(*wxWHITE);
    sizer_enable_ams->Add(m_tip_enable_ams, 0, wxTOP, 8);

    wxBoxSizer *sizer_enable_ams_img;
    sizer_enable_ams_img = new wxBoxSizer(wxVERTICAL);

    auto img_enable_ams = new wxStaticBitmap(m_panel_enable_ams, wxID_ANY, create_scaled_bitmap("monitor_upgrade_ams", this, 108), wxDefaultPosition,
                                             wxSize(FromDIP(118), FromDIP(108)), 0);
    sizer_enable_ams_img->Add(img_enable_ams, 0, wxALIGN_CENTER_HORIZONTAL, 0);

    sizer_enable_ams->Add(sizer_enable_ams_img, 1, wxEXPAND | wxTOP, FromDIP(20));

    m_panel_enable_ams->SetSizer(sizer_enable_ams);
    m_panel_enable_ams->Layout();
    m_sizer_body->Add(m_panel_enable_ams, 0, 0, 0);

    m_split_lines = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(1, FromDIP(150)), wxTAB_TRAVERSAL);
    m_split_lines->SetBackgroundColour(wxColour(238, 238, 238));

    m_sizer_body->Add(m_split_lines, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(10));

    m_panel_disable_ams = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(220), -1), wxTAB_TRAVERSAL);
    m_panel_disable_ams->SetBackgroundColour(*wxWHITE);
    wxBoxSizer *sizer_disable_ams;
    sizer_disable_ams = new wxBoxSizer(wxVERTICAL);

    m_title_disable_ams = new wxStaticText(m_panel_disable_ams, wxID_ANY, _L("Disable AMS"), wxDefaultPosition, wxDefaultSize, 0);
    m_title_disable_ams->SetBackgroundColour(*wxWHITE);
    m_title_disable_ams->SetForegroundColour(*wxBLACK);
    m_title_disable_ams->Wrap(-1);
    sizer_disable_ams->Add(m_title_disable_ams, 0, 0, 0);

    m_tip_disable_ams = new wxStaticText(m_panel_disable_ams, wxID_ANY, _L("Print with the filament mounted on the back of chassis"), wxDefaultPosition, wxDefaultSize, 0);
    m_tip_disable_ams->SetMinSize(wxSize(FromDIP(200), FromDIP(50)));
    m_tip_disable_ams->Wrap(FromDIP(200));
    m_tip_disable_ams->SetForegroundColour(*wxBLACK);
    m_tip_disable_ams->SetBackgroundColour(*wxWHITE);
    sizer_disable_ams->Add(m_tip_disable_ams, 0, wxTOP, FromDIP(8));

    wxBoxSizer *sizer_disable_ams_img;
    sizer_disable_ams_img = new wxBoxSizer(wxVERTICAL);

    auto img_disable_ams = new wxStaticBitmap(m_panel_disable_ams, wxID_ANY, create_scaled_bitmap("disable_ams_demo_icon", this, 95), wxDefaultPosition,
                                              wxSize(FromDIP(95), FromDIP(109)), 0);
    sizer_disable_ams_img->Add(img_disable_ams, 0, wxALIGN_CENTER_HORIZONTAL, 0);

    sizer_disable_ams->Add(sizer_disable_ams_img, 1, wxEXPAND | wxTOP, FromDIP(20));

    m_panel_disable_ams->SetSizer(sizer_disable_ams);
    m_panel_disable_ams->Layout();
    m_sizer_body->Add(m_panel_disable_ams, 0, 0, 0);

    m_sizer_body->Add(0, 0, 0, wxEXPAND | wxRIGHT, FromDIP(20));

    m_sizer_main->Add(m_sizer_body, 0, wxEXPAND, 0);

    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(28));

    this->SetSizer(m_sizer_main);
    this->Layout();
    this->Fit();
    Bind(wxEVT_PAINT, &AmsMapingTipPopup::paintEvent, this);
}

void AmsMapingTipPopup::paintEvent(wxPaintEvent &evt)
{
    wxPaintDC dc(this);
    dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRoundedRectangle(0, 0, GetSize().x, GetSize().y, 0);
}

void AmsMapingTipPopup::OnDismiss() {}

bool AmsMapingTipPopup::ProcessLeftDown(wxMouseEvent &event) { 
    return PopupWindow::ProcessLeftDown(event); }


AmsHumidityTipPopup::AmsHumidityTipPopup(wxWindow* parent)
    :PopupWindow(parent, wxBORDER_NONE)
{
    SetBackgroundColour(*wxWHITE);

    wxBoxSizer* main_sizer;
    main_sizer = new wxBoxSizer(wxVERTICAL);


    main_sizer->Add(0, 0, 0, wxTOP, 28);

    wxBoxSizer* m_sizer_body;
    m_sizer_body = new wxBoxSizer(wxHORIZONTAL);

    m_img = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("ams_humidity_tips", this, 125), wxDefaultPosition, wxSize(FromDIP(125), FromDIP(145)), 0);

    m_sizer_body->Add(m_img, 0, wxEXPAND | wxALL, 2);


    m_sizer_body->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(18));

    wxBoxSizer* m_sizer_tips = new wxBoxSizer(wxVERTICAL);

    m_staticText1 = new Label(this, _L("Cabin humidity"));
    m_staticText1->SetFont(::Label::Head_13);
   

    m_staticText2 = new Label(this, _L("Green means that AMS humidity is normal, orange represent humidity is high, red represent humidity is too high.(Hygrometer: lower the better.)"));
    m_staticText2->SetFont(::Label::Body_13);
    m_staticText2->SetSize(wxSize(FromDIP(357), -1));
    m_staticText2->SetMinSize(wxSize(FromDIP(357), -1));
    m_staticText2->SetMaxSize(wxSize(FromDIP(357), -1));
    m_staticText2->Wrap(FromDIP(357));
    

    m_staticText3 = new Label(this, _L("Desiccant status"));
    m_staticText3->SetFont(::Label::Head_13);
  

    m_staticText4 = new Label(this, _L("A desiccant status lower than two bars indicates that desiccant may be inactive. Please change the desiccant.(The bars: higher the better.)"));
    m_staticText4->SetFont(::Label::Body_13);
    m_staticText4->SetSize(wxSize(FromDIP(357), -1));
    m_staticText4->SetMinSize(wxSize(FromDIP(357), -1));
    m_staticText4->SetMaxSize(wxSize(FromDIP(357), -1));
    m_staticText4->Wrap(FromDIP(357));

    m_sizer_tips->Add(m_staticText1, 0, wxLEFT|wxRIGHT, 3);
    m_sizer_tips->Add(0,0,0,wxTOP,2);
    m_sizer_tips->Add(m_staticText2, 0, wxLEFT|wxRIGHT, 3);
    m_sizer_tips->Add(0,0,0,wxTOP,8);
    m_sizer_tips->Add(m_staticText3, 0, wxLEFT|wxRIGHT, 3);
    m_sizer_tips->Add(0,0,0,wxTOP,2);
    m_sizer_tips->Add(m_staticText4, 0, wxLEFT|wxRIGHT, 3);


    m_sizer_body->Add(m_sizer_tips, 0, wxEXPAND, 0);


    main_sizer->Add(m_sizer_body, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));

    m_staticText_note = new Label(this, _L("Note: When the lid is open or the desiccant pack is changed, it can take hours or a night to absorb the moisture. Low temperatures also slow down the process. During this time, the indicator may not represent the chamber accurately."));
    m_staticText4->SetFont(::Label::Body_13);
    m_staticText_note->SetMinSize(wxSize(FromDIP(523), -1));
    m_staticText_note->SetMaxSize(wxSize(FromDIP(523), -1));
    m_staticText_note->Wrap(FromDIP(523));
    main_sizer->Add(m_staticText_note, 0, wxALL | wxLEFT | wxRIGHT, 22);

    m_button_confirm = new Button(this, _L("OK"));
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Pressed), std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    m_button_confirm->SetBackgroundColor(btn_bg_green);
    m_button_confirm->SetBorderColor(wxColour(0, 174, 66));
    m_button_confirm->SetTextColor(wxColour(0xFFFFFE));
    m_button_confirm->SetSize(wxSize(FromDIP(72), FromDIP(24)));
    m_button_confirm->SetMinSize(wxSize(FromDIP(72), FromDIP(24)));
    m_button_confirm->SetCornerRadius(FromDIP(12));


    m_button_confirm->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
         Dismiss();
    });

    Bind(wxEVT_LEFT_UP, [this](auto& e) {
        auto mouse_pos = ClientToScreen(e.GetPosition());
        auto rect = m_button_confirm->ClientToScreen(wxPoint(0, 0));
        if (mouse_pos.x > rect.x && mouse_pos.y > rect.y
            && mouse_pos.x < (rect.x + m_button_confirm->GetSize().x)
            && mouse_pos.y < (rect.y + m_button_confirm->GetSize().y)) 
        {
            Dismiss();
        }
    });
    main_sizer->Add(m_button_confirm, 0, wxALIGN_CENTER | wxALL, 0);


    main_sizer->Add(0, 0, 0, wxEXPAND | wxTOP, 18);


    SetSizer(main_sizer);
    Layout();
    Fit();

    Bind(wxEVT_PAINT, &AmsHumidityTipPopup::paintEvent, this);
    wxGetApp().UpdateDarkUIWin(this);
}

void AmsHumidityTipPopup::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRoundedRectangle(0, 0, GetSize().x, GetSize().y, 0);
}

void AmsHumidityTipPopup::OnDismiss() {}

bool AmsHumidityTipPopup::ProcessLeftDown(wxMouseEvent& event) {
    return PopupWindow::ProcessLeftDown(event);
}

AmsTutorialPopup::AmsTutorialPopup(wxWindow* parent)
:PopupWindow(parent, wxBORDER_NONE)
{
    Bind(wxEVT_PAINT, &AmsTutorialPopup::paintEvent, this);
    SetBackgroundColour(*wxWHITE);

    wxBoxSizer* sizer_main;
    sizer_main = new wxBoxSizer(wxVERTICAL);

    text_title = new Label(this, Label::Head_14, _L("Config which AMS slot should be used for a filament used in the print job"));
    text_title->SetSize(wxSize(FromDIP(350), -1));
    text_title->Wrap(FromDIP(350));
    sizer_main->Add(text_title, 0, wxALIGN_CENTER | wxTOP, 18);


    sizer_main->Add(0, 0, 0, wxTOP, 30);

    wxBoxSizer* sizer_top;
    sizer_top = new wxBoxSizer(wxHORIZONTAL);

    img_top = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("ams_item_examples", this, 30), wxDefaultPosition, wxSize(FromDIP(50), FromDIP(30)), 0);
    sizer_top->Add(img_top, 0, wxALIGN_CENTER, 0);


    sizer_top->Add(0, 0, 0, wxLEFT, 10);

    wxBoxSizer* sizer_top_tips = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* sizer_tip_top = new wxBoxSizer(wxHORIZONTAL);

    arrows_top = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("ams_arrow", this, 8), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(8)), 0);
    sizer_tip_top->Add(arrows_top, 0, wxALIGN_CENTER, 0);

    tip_top = new wxStaticText(this, wxID_ANY, _L("Filament used in this print job"), wxDefaultPosition, wxDefaultSize, 0);
    tip_top->SetForegroundColour(wxColour("#686868"));
    
    sizer_tip_top->Add(tip_top, 0, wxALL, 0);


    sizer_top_tips->Add(sizer_tip_top, 0, wxEXPAND, 0);


    sizer_top_tips->Add(0, 0, 0, wxTOP, 6);

    wxBoxSizer* sizer_tip_bottom = new wxBoxSizer(wxHORIZONTAL);

    arrows_bottom = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("ams_arrow", this, 8), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(8)), 0);
    tip_bottom = new wxStaticText(this, wxID_ANY, _L("AMS slot used for this filament"), wxDefaultPosition, wxDefaultSize, 0);
    tip_bottom->SetForegroundColour(wxColour("#686868"));


    sizer_tip_bottom->Add(arrows_bottom, 0, wxALIGN_CENTER, 0);
    sizer_tip_bottom->Add(tip_bottom, 0, wxALL, 0);


    sizer_top_tips->Add(sizer_tip_bottom, 0, wxEXPAND, 0);


    sizer_top->Add(sizer_top_tips, 0, wxALIGN_CENTER, 0);


    

    wxBoxSizer* sizer_middle = new wxBoxSizer(wxHORIZONTAL);

    img_middle= new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("ams_item_examples", this, 30), wxDefaultPosition, wxSize(FromDIP(50), FromDIP(30)), 0);
    sizer_middle->Add(img_middle, 0, wxALIGN_CENTER, 0);

    tip_middle = new wxStaticText(this, wxID_ANY, _L("Click to select AMS slot manually"), wxDefaultPosition, wxDefaultSize, 0);
    tip_middle->SetForegroundColour(wxColour("#686868"));
    sizer_middle->Add(0, 0, 0,wxLEFT, 15);
    sizer_middle->Add(tip_middle, 0, wxALIGN_CENTER, 0);


    sizer_main->Add(sizer_top, 0, wxLEFT, 40);
    sizer_main->Add(0, 0, 0, wxTOP, 10);
    sizer_main->Add(sizer_middle, 0, wxLEFT, 40);
    sizer_main->Add(0, 0, 0, wxTOP, 10);


    img_botton = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("ams_mapping_examples", this, 87), wxDefaultPosition, wxDefaultSize, 0);
    sizer_main->Add(img_botton, 0, wxLEFT | wxRIGHT, 40);
    sizer_main->Add(0, 0, 0, wxTOP, 12);

    SetSizer(sizer_main);
    Layout();
    Fit();
}

void AmsTutorialPopup::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRoundedRectangle(0, 0, GetSize().x, GetSize().y, 0);
}

void AmsTutorialPopup::OnDismiss() {}

bool AmsTutorialPopup::ProcessLeftDown(wxMouseEvent& event) {
    return PopupWindow::ProcessLeftDown(event);
}


AmsIntroducePopup::AmsIntroducePopup(wxWindow* parent)
:PopupWindow(parent, wxBORDER_NONE)
{
    Bind(wxEVT_PAINT, &AmsIntroducePopup::paintEvent, this);
    SetBackgroundColour(*wxWHITE);

    SetMinSize(wxSize(FromDIP(200), FromDIP(200)));
    SetMaxSize(wxSize(FromDIP(200), FromDIP(200)));

    wxBoxSizer* bSizer4 = new wxBoxSizer(wxVERTICAL);

    m_staticText_top = new Label(this, _L("Do not Enable AMS"));
    m_staticText_top->SetFont(::Label::Head_13);
   // m_staticText_top->SetForegroundColour(wxColour(0x323A3D));
    m_staticText_top->Wrap(-1);
    bSizer4->Add(m_staticText_top, 0, wxALL, 5);

    m_staticText_bottom =  new Label(this, _L("Print using materials mounted on the back of the case"));
    m_staticText_bottom->Wrap(-1);
    m_staticText_bottom->SetFont(::Label::Body_13);
    m_staticText_bottom->SetForegroundColour(wxColour(0x6B6B6B));
    bSizer4->Add(m_staticText_bottom, 0, wxALL, 5);

    wxBoxSizer* bSizer5;
    bSizer5 = new wxBoxSizer(wxHORIZONTAL);

    m_img_enable_ams = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("monitor_upgrade_ams", this, FromDIP(140)), wxDefaultPosition, wxDefaultSize, 0);
    m_img_disable_ams = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("disable_ams_demo_icon", this, FromDIP(110)), wxDefaultPosition, wxDefaultSize, 0);

    m_img_enable_ams->SetMinSize(wxSize(FromDIP(96), FromDIP(110)));
    m_img_disable_ams->SetMinSize(wxSize(FromDIP(96), FromDIP(110)));

    bSizer5->Add(m_img_enable_ams, 0, wxALIGN_CENTER, 0);
    bSizer5->Add(m_img_disable_ams, 0, wxALIGN_CENTER, 0);

    m_img_disable_ams->Hide();
    m_img_disable_ams->Hide();


    bSizer4->Add(bSizer5, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(1));


    SetSizer(bSizer4);
    Layout();
    Fit();

    wxGetApp().UpdateDarkUIWin(this);
}

void AmsIntroducePopup::set_mode(bool enable_ams) 
{
    if (enable_ams) {
        m_staticText_top->SetLabelText(_L("Enable AMS"));
        m_staticText_bottom->SetLabelText(_L("Print with filaments in ams"));
        m_img_enable_ams->Show();
        m_img_disable_ams->Hide();
    }
    else {
        m_staticText_top->SetLabelText(_L("Do not Enable AMS"));
        m_staticText_bottom->SetLabelText(_L("Print with filaments mounted on the back of the chassis"));
        m_staticText_bottom->SetMinSize(wxSize(FromDIP(180), -1));
        m_staticText_bottom->Wrap(FromDIP(180));
        m_img_enable_ams->Hide();
        m_img_disable_ams->Show();
    }
    Layout();
    Fit();
}

void AmsIntroducePopup::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRoundedRectangle(0, 0, GetSize().x, GetSize().y, 0);
}


void AmsIntroducePopup::OnDismiss() {}

bool AmsIntroducePopup::ProcessLeftDown(wxMouseEvent& event) {
    return PopupWindow::ProcessLeftDown(event);
}


MappingContainer::MappingContainer(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__
    SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    Bind(wxEVT_PAINT, &MappingContainer::paintEvent, this);

    ams_mapping_item_container = create_scaled_bitmap("ams_mapping_container", this, 78);

    SetMinSize(wxSize(FromDIP(230), FromDIP(78)));
    SetMaxSize(wxSize(FromDIP(230), FromDIP(78)));
}

MappingContainer::~MappingContainer()
{
}


void MappingContainer::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void MappingContainer::render(wxDC& dc)
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

void MappingContainer::doRender(wxDC& dc)
{
    dc.DrawBitmap(ams_mapping_item_container, 0, 0);
}

AmsReplaceMaterialDialog::AmsReplaceMaterialDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Filaments replace"), wxDefaultPosition, wxDefaultSize, wxSYSTEM_MENU | wxCAPTION | wxCLOSE_BOX)
{
    SetBackgroundColour(*wxWHITE);
    create();
    wxGetApp().UpdateDlgDarkUI(this);
}

void AmsReplaceMaterialDialog::create()
{
    SetSize(wxSize(FromDIP(376), -1));
    SetMinSize(wxSize(FromDIP(376), -1));
    SetMaxSize(wxSize(FromDIP(376), -1));

    // set icon for dialog
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));
    SetSizeHints(wxDefaultSize, wxDefaultSize);

    m_main_sizer = new wxBoxSizer(wxVERTICAL);
    auto m_top_line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_top_line->SetBackgroundColour(wxColour(166, 169, 170));
    m_main_sizer->Add(m_top_line, 0, wxEXPAND, 0);


    auto m_button_sizer = new wxBoxSizer(wxHORIZONTAL);
    
    StateColor btn_bg_white(std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled),
        std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Pressed),
        std::pair<wxColour, int>(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Normal));

    StateColor btn_bd_white(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));

    StateColor btn_text_white(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));

    auto m_button_close = new Button(this, _L("Close"));
    m_button_close->SetCornerRadius(FromDIP(11));
    m_button_close->SetBackgroundColor(btn_bg_white);
    m_button_close->SetBorderColor(btn_bd_white);
    m_button_close->SetTextColor(btn_text_white);
    m_button_close->SetFont(Label::Body_13);
    m_button_close->SetMinSize(wxSize(FromDIP(42), FromDIP(24)));
    m_button_close->Bind(wxEVT_BUTTON, [this](auto& e) {
        EndModal(wxCLOSE);
    });

    m_button_sizer->Add( 0, 0, 1, wxEXPAND, 0 );
    m_button_sizer->Add(m_button_close, 0, wxALIGN_CENTER, 0);


    m_groups_sizer = new wxBoxSizer(wxVERTICAL);
    m_main_sizer->Add(0,0,0, wxTOP, FromDIP(12));
    m_main_sizer->Add(m_groups_sizer,0,wxEXPAND|wxLEFT|wxRIGHT, FromDIP(16));
    m_main_sizer->Add(0,0,0, wxTOP, FromDIP(20));
    m_main_sizer->Add(m_button_sizer,0,wxEXPAND|wxLEFT|wxRIGHT, FromDIP(16));
    m_main_sizer->Add(0,0,0, wxTOP, FromDIP(20));
    

    CenterOnParent();
    SetSizer(m_main_sizer);
    Layout();
    Fit();
}

std::vector<bool> AmsReplaceMaterialDialog::GetStatus(unsigned int status)
{
    std::vector<bool> listStatus;
    bool current = false;
    for (int i = 0; i < 16; i++) {
        if (status & (1 << i)) {
            current = true;
        }
        else {
            current = false;
        }
        listStatus.push_back(current);
    }
    return listStatus;
}

void AmsReplaceMaterialDialog::update_machine_obj(MachineObject* obj)
{
    if (obj) {m_obj = obj;}
    else {return;}

    AmsTray* tray_list[4*4];
    for (auto i = 0; i < 4*4; i++) {
        tray_list[i] = nullptr;
    }

    try {
        for (auto ams_info : obj->amsList) {
            int ams_id_int = atoi(ams_info.first.c_str()) * 4;

            for (auto tray_info : ams_info.second->trayList) {
                int tray_id_int = atoi(tray_info.first.c_str());
                tray_id_int =  ams_id_int + tray_id_int;
                tray_list[tray_id_int] = tray_info.second;
            }
        }
    }
    catch (...) {}

    //creat group
    int group_index = 1;
    for (int filam : m_obj->filam_bak) {
         auto status_list = GetStatus(filam);

         wxColour       group_color;
         std::string    group_material;

         //get color & material
         for (auto i = 0; i < status_list.size(); i++) {
             if (status_list[i] && tray_list[i] != nullptr) {
                 group_color = AmsTray::decode_color(tray_list[i]->color);
                 group_material = tray_list[i]->get_display_filament_type();
             }
         }

         m_groups_sizer->Add(create_split_line(wxString::Format("%s%d", _L("Group"), group_index), group_color, group_material, status_list), 0, wxEXPAND, 0);
         m_groups_sizer->Add(0, 0, 0, wxTOP, FromDIP(12));
         group_index++;
    }

    Layout();
    Fit();
}

wxWindow* AmsReplaceMaterialDialog::create_split_line(wxString gname, wxColour col, wxString material, std::vector<bool> status_list)
{
    wxColour background_color = wxColour(0xF4F4F4);

    if (abs(col.Red() - background_color.Red()) <= 5 && 
        abs(col.Green() - background_color.Green()) <= 5 && 
        abs(col.Blue() - background_color.Blue()) <= 5) {
        background_color = wxColour(0xE6E6E6);
    }

    auto m_panel_group = new StaticBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_panel_group->SetCornerRadius(FromDIP(4));
    m_panel_group->SetBackgroundColor(StateColor(std::pair<wxColour, int>(background_color, StateColor::Normal)));

    m_panel_group->SetSize(wxSize(FromDIP(344), -1));
    m_panel_group->SetMinSize(wxSize(FromDIP(344), -1));
    m_panel_group->SetMaxSize(wxSize(FromDIP(344), -1));

    wxBoxSizer* group_sizer = new wxBoxSizer(wxVERTICAL);

    //group title
    wxBoxSizer* title_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto group_name = new Label(m_panel_group, gname);
    group_name->SetFont(::Label::Head_12);

    Button* material_info = new Button(m_panel_group, material);
    material_info->SetFont(Label::Head_12);
    material_info->SetCornerRadius(FromDIP(2));
    material_info->SetBorderColor(background_color);

    if (col.GetLuminance() < 0.5)
        material_info->SetTextColor(*wxWHITE);
    else
        material_info->SetTextColor(0x6B6B6B);
    
    material_info->SetMinSize(wxSize(-1, FromDIP(24)));
    material_info->SetBackgroundColor(col);


    title_sizer->Add(group_name, 0, wxALIGN_CENTER, 0);
    title_sizer->Add(0, 0, 0, wxLEFT, FromDIP(10));
    title_sizer->Add(material_info, 0, wxALIGN_CENTER, 0);


    //group item
    wxGridSizer* grid_Sizer = new wxGridSizer(0, 8, 0, 0);

    for (int i = 0; i < status_list.size(); i++) {
        if (status_list[i]) {
            AmsRMItem* amsitem = new AmsRMItem(m_panel_group, wxID_ANY, wxDefaultPosition, wxDefaultSize);
            amsitem->set_color(col);

            //set current tray
            if (!m_obj->m_tray_now.empty() && m_obj->m_tray_now == std::to_string(i)) {
                amsitem->set_focus(true);
            }

            amsitem->set_type(RMTYPE_NORMAL);
            amsitem->set_index(wxGetApp().transition_tridid(i).ToStdString());
            amsitem->SetBackgroundColour(background_color);
            grid_Sizer->Add(amsitem, 0, wxALIGN_CENTER | wxTOP | wxBottom, FromDIP(10));
        }
    }

    //add the first tray
    for (int i = 0; i < status_list.size(); i++) {
        if (status_list[i]) {
            AmsRMItem* amsitem = new AmsRMItem(m_panel_group, wxID_ANY, wxDefaultPosition, wxDefaultSize);
            amsitem->set_color(col);
            amsitem->set_type(RMTYPE_VIRTUAL);
            amsitem->set_index(wxGetApp().transition_tridid(i).ToStdString());
            amsitem->SetBackgroundColour(background_color);
            grid_Sizer->Add(amsitem, 0, wxALIGN_CENTER | wxTOP | wxBottom, FromDIP(10));
            break;
        }
    }

    group_sizer->Add(0, 0, 0, wxTOP, FromDIP(10));
    group_sizer->Add(title_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));
    group_sizer->Add(grid_Sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));
    group_sizer->Add(0, 0, 0, wxTOP, FromDIP(10));

    m_panel_group->SetSizer(group_sizer);
    m_panel_group->Layout();
    group_sizer->Fit(m_panel_group);
    return m_panel_group;
}

void AmsReplaceMaterialDialog::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRoundedRectangle(0, 0, GetSize().x, GetSize().y, 0);
}

void AmsReplaceMaterialDialog::on_dpi_changed(const wxRect& suggested_rect)
{

}

AmsRMItem::AmsRMItem(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size)
{
    wxWindow::Create(parent, id, pos, size);

    SetSize(wxSize(FromDIP(42), FromDIP(32)));
    SetMinSize(wxSize(FromDIP(42), FromDIP(32)));
    SetMaxSize(wxSize(FromDIP(42), FromDIP(32)));

    SetBackgroundColour(*wxWHITE);

    Bind(wxEVT_PAINT, &AmsRMItem::paintEvent, this);
}

void AmsRMItem::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AmsRMItem::render(wxDC& dc)
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

void AmsRMItem::doRender(wxDC& dc)
{
    wxSize size = GetSize();

    if (m_type == RMTYPE_NORMAL) {
        dc.SetPen(wxPen(m_color, 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
    }
    else {
        dc.SetPen(wxPen(m_color, 2, wxSHORT_DASH));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
    }

    //top bottom line
    dc.DrawLine(FromDIP(0), FromDIP(4), size.x - FromDIP(5), FromDIP(4));
    dc.DrawLine(FromDIP(0), size.y - FromDIP(4), size.x - FromDIP(5), size.y - FromDIP(4));

    //left right line
    dc.DrawLine(FromDIP(1), FromDIP(4), FromDIP(1), FromDIP(11));
    dc.DrawLine(FromDIP(1), FromDIP(22), FromDIP(1), size.y - FromDIP(4));

    dc.DrawLine(size.x - FromDIP(5), FromDIP(4), size.x - FromDIP(5), FromDIP(11));
    dc.DrawLine(size.x - FromDIP(5), FromDIP(22), size.x - FromDIP(5), size.y - FromDIP(4));

    //delta
    dc.DrawLine(FromDIP(0), FromDIP(11), FromDIP(5), size.y / 2);
    dc.DrawLine(FromDIP(0), FromDIP(22), FromDIP(5), size.y / 2);

    dc.DrawLine(size.x - FromDIP(5), FromDIP(11), size.x - FromDIP(1), size.y / 2);
    dc.DrawLine(size.x - FromDIP(5), FromDIP(22), size.x - FromDIP(1), size.y / 2);


    if (m_focus) {
        dc.SetPen(wxPen(wxColour(0x00AE42), 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawLine(FromDIP(0), FromDIP(1), size.x - FromDIP(5), FromDIP(1));
        dc.DrawLine(FromDIP(0), size.y - FromDIP(1), size.x - FromDIP(5), size.y - FromDIP(1));
    }

    if (m_selected) {
    }

    auto tsize = dc.GetMultiLineTextExtent(m_index);
    auto tpot = wxPoint((size.x - tsize.x) / 2 - FromDIP(2), (size.y - tsize.y) / 2 + FromDIP(2));
    dc.SetTextForeground(wxColour(0x6B6B6B));
    dc.SetFont(::Label::Head_12);
    dc.DrawText(m_index, tpot);
}

AmsRMArrow::AmsRMArrow(wxWindow* parent)
{

    wxWindow::Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    SetBackgroundColour(*wxWHITE);
    Bind(wxEVT_PAINT, &AmsRMArrow::paintEvent, this);

    m_bitmap_left = ScalableBitmap(this, "replace_arrow_left", 7);
    m_bitmap_right = ScalableBitmap(this, "replace_arrow_right", 7);
    m_bitmap_down = ScalableBitmap(this, "replace_arrow_down", 7);


        SetSize(wxSize(FromDIP(16), FromDIP(32)));
        SetMinSize(wxSize(FromDIP(16), FromDIP(32)));
        SetMaxSize(wxSize(FromDIP(16), FromDIP(32)));
}

void AmsRMArrow::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AmsRMArrow::render(wxDC& dc)
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

void AmsRMArrow::doRender(wxDC& dc)
{
    wxSize size = GetSize();

    dc.SetPen(wxPen(wxColour(0, 174, 66)));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);


    dc.SetPen(wxPen(wxColour(0xACACAC)));
    dc.SetBrush(wxBrush(wxColour(0xACACAC)));
    dc.DrawCircle(size.x / 2, size.y / 2, FromDIP(7));
}

}} // namespace Slic3r::GUI
