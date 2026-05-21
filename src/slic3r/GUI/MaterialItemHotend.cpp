/*****************************************************************//**
 * \file   MaterialItemHotend.cpp
 * \brief  Implementation of MaterialItemHotend (moved from
 *         AmsMappingHotend.cpp; identical render logic).
 *
 * \author xin.zhang
 * \date   May 2026
 *********************************************************************/
#include "MaterialItemHotend.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Widgets/Label.hpp"

#include <wx/dcgraph.h>

namespace Slic3r { namespace GUI {

MaterialItemHotend::MaterialItemHotend(wxWindow *parent, wxColour mcolour, wxString mname, std::string filament_id)
    : MaterialItem(parent, mcolour, mname, filament_id)
{
    m_arraw_bitmap_gray          = ScalableBitmap(this, "drop_down2", FromDIP(8));
    m_arraw_bitmap_white         = ScalableBitmap(this, "topbar_dropdown", 12);
    m_transparent_mitem          = ScalableBitmap(this, "transparent_material_up", 20);
    m_filament_wheel_transparent = ScalableBitmap(this, "filament_transparent", 25);
    m_ams_wheel_mitem            = ScalableBitmap(this, "ams_wheel_narrow", 25);
    m_ams_not_match              = ScalableBitmap(this, "filament_not_mactch", 25);
    m_rack_nozzle_bitmap         = ScalableBitmap(this, "dev_rack_nozzle_print_job", 22);

    m_material_coloul = mcolour;
    m_material_name   = mname;
    m_ams_coloul      = wxColour(0xEE, 0xEE, 0xEE);
    m_filament_id     = filament_id;

#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif

    messure_size();
    SetBackgroundColour(*wxWHITE);

    Bind(wxEVT_PAINT, &MaterialItem::paintEvent, this);
    wxGetApp().UpdateDarkUI(this);
}

MaterialItemHotend::~MaterialItemHotend() {}

void MaterialItemHotend::msw_rescale()
{
    MaterialItem::msw_rescale();
    m_rack_nozzle_bitmap.msw_rescale();
}

void MaterialItemHotend::set_ams_filament_type(const wxString& ams_filament_type)
{
    if (m_ams_filament_type != ams_filament_type) {
        m_ams_filament_type = ams_filament_type;
        Refresh();
    }
}

void MaterialItemHotend::set_nozzle_dia_and_type(const wxString& dia_type_info)
{
    if (dia_type_info != m_nozzle_dia_and_type) {
        m_nozzle_dia_and_type = dia_type_info;
        Refresh();
    }
}

void MaterialItemHotend::render(wxDC& dc)
{
    if (m_ams_name.empty() || m_ams_name == "-") {
        m_match = false;
    } else {
        m_match = true;
    }

#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({0, 0}, size, &dc, {0, 0});
    { wxGCDC dc2(memdc); doRender(dc2); }
    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void MaterialItemHotend::doRender(wxDC& dc)
{
    wxSize size   = GetSize();
    auto   mcolor = m_material_coloul;
    auto   acolor = m_ams_coloul;
    change_the_opacity(acolor);

    if (mcolor.Alpha() == 0) { dc.DrawBitmap(m_transparent_mitem.bmp(), FromDIP(1), FromDIP(1)); }

    if (!IsEnabled()) {
        mcolor = wxColour(0x90, 0x90, 0x90);
        acolor = wxColour(0x90, 0x90, 0x90);
    }

    // top color bar
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(mcolor));
    dc.DrawRoundedRectangle(0, 0, size.x, FromDIP(20), 5);
    dc.SetBrush(wxBrush(mcolor));
    dc.DrawRectangle(0, FromDIP(10), size.x, FromDIP(10));

    // material name
    auto material_name_colour = mcolor.GetLuminance() < 0.6 ? *wxWHITE : wxColour(0x26, 0x2E, 0x30);
    if (mcolor.Alpha() == 0) { material_name_colour = wxColour(0x26, 0x2E, 0x30); }
    dc.SetTextForeground(material_name_colour);
    dc.SetFont(::Label::Body_12);
    if (dc.GetTextExtent(m_material_name).x > GetSize().x - 10) { dc.SetFont(::Label::Body_10); }
    auto material_txt_size = dc.GetTextExtent(m_material_name);
    dc.DrawText(m_material_name, wxPoint((GetSize().x - material_txt_size.x) / 2, (FromDIP(20) - material_txt_size.y) / 2));

    dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
    dc.DrawLine(FromDIP(1), FromDIP(20), FromDIP(size.x), FromDIP(20));

    auto up = FromDIP(20) + FromDIP(2);

    const int rec_w      = FromDIP(17);
    const int rec_h      = FromDIP(16);
    const int left_off   = FromDIP(2);

    // spool area
    auto paint_recty = up + (m_ams_wheel_mitem.GetBmpSize().y - rec_h) / 2;
    auto left        = (size.x / 2 - rec_w) / 2 + FromDIP(3);
    auto right       = left + rec_w - FromDIP(3);
    dc.SetPen(*wxTRANSPARENT_PEN);

    if (m_ams_cols.size() > 1) {
        int gwidth = std::round(rec_w / (m_ams_cols.size() - 1));
        if (m_ams_ctype == 0) {
            for (size_t i = 0; i < m_ams_cols.size() - 1; i++) {
                auto rect = wxRect(left, paint_recty, right - left, rec_h);
                dc.GradientFillLinear(rect, m_ams_cols[i], m_ams_cols[i + 1], wxEAST);
                left += gwidth;
            }
        } else {
            int cols_size = (int)m_ams_cols.size();
            for (int i = 0; i < cols_size; i++) {
                dc.SetBrush(wxBrush(m_ams_cols[i]));
                float x = left + ((float)rec_w) * i / cols_size;
                if (i != cols_size - 1)
                    dc.DrawRoundedRectangle(x - left_off, paint_recty, ((float)rec_w) / cols_size + FromDIP(3), rec_h, 3);
                else
                    dc.DrawRoundedRectangle(x - left_off, paint_recty, ((float)rec_w) / cols_size - FromDIP(1), rec_h, 3);
            }
        }
    } else {
        if (m_match) {
            dc.SetBrush(wxBrush(acolor));
            dc.DrawRectangle((size.x / 2 - rec_w) / 2 + FromDIP(3) - left_off,
                             paint_recty, rec_w - FromDIP(1), rec_h);
        }
    }

    // border
    if (m_match)
        dc.SetPen(wxPen(wxGetApp().dark_mode() ? wxColour(107,107,107) : wxColour(0xAC,0xAC,0xAC), FromDIP(1)));
    else
        dc.SetPen(wxPen(wxColour(234,31,48), FromDIP(1)));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRoundedRectangle(0, 0, size.x, size.y, 5);

    if (m_selected) {
        dc.SetPen(wxPen(wxColour(0x00,0xAE,0x42), FromDIP(2)));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(FromDIP(1), FromDIP(1), size.x - FromDIP(1), size.y - FromDIP(1), 5);
    } else if (m_warning) {
        dc.SetPen(wxPen(wxColour(0xFF,0x6F,0x00), FromDIP(2)));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRoundedRectangle(FromDIP(1), FromDIP(1), size.x - FromDIP(1), size.y - FromDIP(1), 5);
    }

    // spool icon
    auto wheel_left = (GetSize().x / 2 - m_ams_wheel_mitem.GetBmpSize().x) / 2 + FromDIP(2) - left_off;
    auto wheel_top  = up;
    if (!m_match) {
        wheel_left = (size.x - m_ams_not_match.GetBmpWidth()) / 2 - left_off;
        dc.DrawBitmap(m_ams_not_match.bmp(), wheel_left, wheel_top);
    } else {
        if (acolor.Alpha() == 0)
            dc.DrawBitmap(m_filament_wheel_transparent.bmp(), wheel_left, wheel_top);
        else
            dc.DrawBitmap(m_ams_wheel_mitem.bmp(), wheel_left, wheel_top);
    }

    // filament type text + arrow
    dc.SetTextForeground(StateColor::darkModeColorFor(wxColour(0x26, 0x2E, 0x30)));
    int  text_pos_x = 0, text_pos_y = 0;
    auto ams_filament_type_size = wxSize(0, 0);
    if (m_ams_filament_type.size() >= 4) {
        m_ams_filament_type.insert(m_mapping_text.size() / 2, "\n");
        // Choose a smaller font when the text is long
        dc.SetFont(::Label::Body_10);
        ams_filament_type_size = dc.GetTextExtent(m_ams_filament_type);
        text_pos_y = up + (m_ams_wheel_mitem.GetBmpSize().y - ams_filament_type_size.y) / 2;
        text_pos_x = ams_filament_type_size.x / 4;
    } else {
        dc.SetFont(::Label::Head_12);
        ams_filament_type_size = dc.GetTextExtent(m_ams_filament_type);
        text_pos_y = up + (m_ams_wheel_mitem.GetBmpSize().y - ams_filament_type_size.y) / 2;
        text_pos_x = 0;
    }
    int arrow_left = size.x - m_arraw_bitmap_white.GetBmpSize().x - FromDIP(4) - left_off;
    if (m_match) {
        int text_x = GetSize().x / 2 + (GetSize().x / 2 - ams_filament_type_size.x) / 2 - FromDIP(8) - left_off + text_pos_x;
        dc.DrawText(m_ams_filament_type, wxPoint(text_x, text_pos_y));
        int arrow_y = text_pos_y + (ams_filament_type_size.y - m_arraw_bitmap_white.GetBmpHeight()) / 2;
        if ((acolor.Red() > 160 && acolor.Green() > 160 && acolor.Blue() > 160) &&
            (acolor.Red() < 180 && acolor.Green() < 180 && acolor.Blue() < 180))
            dc.DrawBitmap(m_arraw_bitmap_white.bmp(), arrow_left, arrow_y);
        else
            dc.DrawBitmap(m_arraw_bitmap_gray.bmp(), arrow_left, arrow_y);
    }

    up += m_ams_wheel_mitem.GetBmpSize().y;

    // nozzle info row
    if (!m_mapped_nozzle_str.IsEmpty()) {
        wxPen dashed_pen(WXCOLOUR_GREY400, 1, wxPENSTYLE_SHORT_DASH);
        dc.SetPen(dashed_pen);
        up += FromDIP(4);
        dc.DrawLine(FromDIP(1), up, FromDIP(size.x), up);

        int buttom_h = size.y - up;
        dc.SetFont(::Label::Head_12);
        auto nozzle_pos_sz  = dc.GetTextExtent(m_mapped_nozzle_str);
        int  nozzle_bmp_w   = m_rack_nozzle_bitmap.GetBmpWidth();
        int  nozzle_bmp_h   = m_rack_nozzle_bitmap.GetBmpHeight();
        dc.SetFont(::Label::Body_10);
        auto dia_and_type_sz = dc.GetTextExtent(m_nozzle_dia_and_type);

        int gap     = FromDIP(2);
        int total_w = nozzle_pos_sz.x + gap + nozzle_bmp_w + dia_and_type_sz.x;
        int cur_x   = (size.x - total_w) / 2;

        dc.SetFont(::Label::Head_12);
        dc.DrawText(m_mapped_nozzle_str, cur_x, up + (buttom_h - nozzle_pos_sz.y) / 2);
        cur_x += nozzle_pos_sz.x + gap;

        dc.DrawBitmap(m_rack_nozzle_bitmap.bmp(), cur_x, up + (buttom_h - nozzle_bmp_h) / 2);
        cur_x += nozzle_bmp_w + gap;

        dc.SetFont(::Label::Body_10);
        dc.DrawText(m_nozzle_dia_and_type, cur_x, up + (buttom_h - dia_and_type_sz.y) / 2);
    }
}

void MaterialItemHotend::messure_size()
{
    SetSize(wxSize(FromDIP(80), FromDIP(84)));
    SetMinSize(wxSize(FromDIP(80), FromDIP(84)));
    SetMaxSize(wxSize(FromDIP(80), FromDIP(84)));
}

}} // namespace Slic3r::GUI
