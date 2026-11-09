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
#include "FilamentBitmapUtils.hpp"
#include "slic3r/GUI/DeviceCore/DevNozzleSystem.h"

#include <wx/dcgraph.h>
#include <wx/image.h>

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

    // Left-aligned layout for the hotend card: the spool icon sits 6px from the
    // left edge, and the material name, the slot label and the nozzle spec all
    // share the same left x (the column just right of the spool icon).
    const int wheel_bmp_w  = m_ams_wheel_mitem.GetBmpSize().x;
    const int wheel_left   = FromDIP(6);
    const int content_left = wheel_left + wheel_bmp_w + FromDIP(2);

    if (mcolor.Alpha() == 0) {
        // "transparent_material_up" is authored 65px wide for the base card;
        // this hotend card is wider (80px), so stretch the checkerboard to the
        // full width, otherwise the right part of the color bar stays blank.
        wxImage transparent_img = m_transparent_mitem.bmp().ConvertToImage();
        if (transparent_img.IsOk()) {
            const int bar_w = size.x - FromDIP(2);
            const int bar_h = m_transparent_mitem.GetBmpHeight();
            transparent_img.Rescale(bar_w, bar_h, wxIMAGE_QUALITY_HIGH);
            dc.DrawBitmap(wxBitmap(std::move(transparent_img)), FromDIP(1), FromDIP(1));
        } else {
            dc.DrawBitmap(m_transparent_mitem.bmp(), FromDIP(1), FromDIP(1));
        }
    }

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

    // material name (left-aligned at content_left)
    auto material_name_colour = mcolor.GetLuminance() < 0.6 ? *wxWHITE : wxColour(0x26, 0x2E, 0x30);
    if (mcolor.Alpha() == 0) { material_name_colour = wxColour(0x26, 0x2E, 0x30); }
    dc.SetTextForeground(material_name_colour);
    dc.SetFont(::Label::Body_12);
    if (dc.GetTextExtent(m_material_name).x > GetSize().x - 10) { dc.SetFont(::Label::Body_10); }
    auto material_txt_size = dc.GetTextExtent(m_material_name);
    dc.DrawText(m_material_name, wxPoint(content_left, (FromDIP(20) - material_txt_size.y) / 2));

    dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
    dc.DrawLine(FromDIP(1), FromDIP(20), FromDIP(size.x), FromDIP(20));

    auto up = FromDIP(20) + FromDIP(2);

    const int rec_w      = FromDIP(17);
    const int rec_h      = FromDIP(16);
    const int left_off   = FromDIP(2);

    // spool area (color swatch stays centered inside the left-aligned spool icon)
    auto paint_recty = up + (m_ams_wheel_mitem.GetBmpSize().y - rec_h) / 2;
    auto left        = wheel_left + (wheel_bmp_w - rec_w) / 2 + FromDIP(3);
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
            dc.DrawRectangle(left - left_off, paint_recty, rec_w - FromDIP(1), rec_h);
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

    // spool icon (left-aligned 6px from the edge; not-match icon shares the same anchor)
    auto wheel_top  = up;
    if (!m_match) {
        dc.DrawBitmap(m_ams_not_match.bmp(), wheel_left, wheel_top);
    } else {
        if (acolor.Alpha() == 0)
            dc.DrawBitmap(m_filament_wheel_transparent.bmp(), wheel_left, wheel_top);
        else
            dc.DrawBitmap(m_ams_wheel_mitem.bmp(), wheel_left, wheel_top);
    }

    // slot label + arrow
    dc.SetTextForeground(StateColor::darkModeColorFor(wxColour(0x26, 0x2E, 0x30)));
    int  text_pos_x = 0, text_pos_y = 0;
    auto ams_name_size = wxSize(0, 0);
    // Always single line, one weight for every label ("6", "A1", "HT-A",
    // "5-Ext") so cards in the same row read consistently.
    dc.SetFont(::Label::Head_12);
    ams_name_size = dc.GetTextExtent(m_ams_name);
    text_pos_y = up + (m_ams_wheel_mitem.GetBmpSize().y - ams_name_size.y) / 2;
    text_pos_x = 0;
    int arrow_left = size.x - m_arraw_bitmap_white.GetBmpSize().x - FromDIP(4) - left_off;
    if (m_match) {
        int text_x = content_left + text_pos_x;
        dc.DrawText(m_ams_name, wxPoint(text_x, text_pos_y));
        int arrow_y = text_pos_y + (ams_name_size.y - m_arraw_bitmap_white.GetBmpHeight()) / 2;
        if ((acolor.Red() > 160 && acolor.Green() > 160 && acolor.Blue() > 160) &&
            (acolor.Red() < 180 && acolor.Green() < 180 && acolor.Blue() < 180))
            dc.DrawBitmap(m_arraw_bitmap_white.bmp(), arrow_left, arrow_y);
        else
            dc.DrawBitmap(m_arraw_bitmap_gray.bmp(), arrow_left, arrow_y);
    }

    up += m_ams_wheel_mitem.GetBmpSize().y;

    // nozzle info row: the nozzle icon sits under the spool, and the nozzle
    // dia/type text is left-aligned with the slot label column (content_left).
    if (!m_mapped_nozzle_str.IsEmpty()) {
        wxPen dashed_pen(WXCOLOUR_GREY400, 1, wxPENSTYLE_SHORT_DASH);
        dc.SetPen(dashed_pen);
        up += FromDIP(4);
        dc.DrawLine(FromDIP(1), up, FromDIP(size.x), up);

        int buttom_h     = size.y - up;
        int nozzle_bmp_w = m_rack_nozzle_bitmap.GetBmpWidth();
        int nozzle_bmp_h = m_rack_nozzle_bitmap.GetBmpHeight();

        // nozzle icon centered under the spool wheel
        int noz_x = wheel_left + (wheel_bmp_w - nozzle_bmp_w) / 2;
        dc.DrawBitmap(m_rack_nozzle_bitmap.bmp(), noz_x, up + (buttom_h - nozzle_bmp_h) / 2);

        // typography follows the design: 12px / weight 400 / #262E30 (gray-900).
        const wxString tpu285_tag = wxString::FromUTF8(DevNozzle::ToNozzleVolumeShortString(NozzleVolumeType::nvtTPU285));
        dc.SetFont(m_nozzle_dia_and_type.EndsWith(tpu285_tag) ? ::Label::Body_10 : ::Label::Body_12);
        dc.SetTextForeground(StateColor::darkModeColorFor(wxColour(0x26, 0x2E, 0x30)));
        auto dia_and_type_sz = dc.GetTextExtent(m_nozzle_dia_and_type);
        dc.DrawText(m_nozzle_dia_and_type, content_left, up + (buttom_h - dia_and_type_sz.y) / 2);
    }
}

void MaterialItemHotend::messure_size()
{
    SetSize(wxSize(FromDIP(92), FromDIP(84)));
    SetMinSize(wxSize(FromDIP(92), FromDIP(84)));
    SetMaxSize(wxSize(FromDIP(92), FromDIP(84)));
}

}} // namespace Slic3r::GUI
