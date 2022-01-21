#include "ImageGrid.h"
#include "Printer/PrinterFileSystem.h"
#include "wxExtensions.hpp"
#include "Widgets/Label.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"


BEGIN_EVENT_TABLE(Slic3r::GUI::ImageGrid, wxPanel)

EVT_MOTION(Slic3r::GUI::ImageGrid::mouseMoved)
EVT_ENTER_WINDOW(Slic3r::GUI::ImageGrid::mouseEnterWindow)
EVT_LEAVE_WINDOW(Slic3r::GUI::ImageGrid::mouseLeaveWindow)
EVT_MOUSEWHEEL(Slic3r::GUI::ImageGrid::mouseWheelMoved)
EVT_LEFT_DOWN(Slic3r::GUI::ImageGrid::mouseDown)
EVT_LEFT_UP(Slic3r::GUI::ImageGrid::mouseReleased)
EVT_SIZE(Slic3r::GUI::ImageGrid::resize)

// catch paint events
EVT_PAINT(Slic3r::GUI::ImageGrid::paintEvent)

END_EVENT_TABLE()

namespace Slic3r {
namespace GUI {

ImageGrid::ImageGrid(wxWindow * parent)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(0xEEEEEE);
    SetForegroundColour(*wxWHITE); // time text color
    SetFont(Label::Head_20);
    m_timer.Bind(wxEVT_TIMER, [this](auto & e) { Refresh(); });
}

void ImageGrid::SetFileSystem(boost::shared_ptr<PrinterFileSystem> file_sys)
{
    if (m_file_sys)
        m_file_sys->Unbind(EVT_FILE_CHANGED, &ImageGrid::changedEvent, this);
    m_file_sys = file_sys;
    if (m_file_sys)
        m_file_sys->Bind(EVT_FILE_CHANGED, &ImageGrid::changedEvent, this);
    UpdateFileSystem();
}

void Slic3r::GUI::ImageGrid::SetGroupMode(int mode)
{
    if (!m_file_sys)
        return;
    wxSize size = GetClientSize();
    int index = (m_row_offset + 1 < m_row_count || m_row_count == 0) 
        ? m_row_offset / 4 * m_col_count 
        : ((m_file_sys->GetCount() + m_col_count - 1) / m_col_count - (size.y + m_image_size.GetHeight() - 1) / m_cell_size.GetHeight()) * m_col_count;
    auto & file = m_file_sys->GetFile(index);
    m_file_sys->SetGroupMode((PrinterFileSystem::GroupMode) mode);
    index = m_file_sys->GetIndexAtTime(file.time);
    // UpdateFileSystem(); call by changed event
    m_row_offset = index / m_col_count * 4;
    if (m_row_offset >= m_row_count)
        m_row_offset = m_row_count == 0 ? 0 : m_row_count - 1;
}

void Slic3r::GUI::ImageGrid::Rescale()
{
    UpdateFileSystem();
}

void Slic3r::GUI::ImageGrid::Select(int index)
{
    if (m_file_sys->GetGroupMode() == PrinterFileSystem::G_NONE) {
        return;
    }
    index = m_file_sys->EnterSubGroup(index);
    // UpdateFileSystem(); call by changed event
    m_row_offset = index / m_col_count * 4;
    if (m_row_offset >= m_row_count)
        m_row_offset = m_row_count == 0 ? 0 : m_row_count - 1;
}

void Slic3r::GUI::ImageGrid::UpdateFileSystem()
{
    if (!m_file_sys) return;
    wxSize mask_size{0, 60};
    if (m_file_sys->GetGroupMode() == PrinterFileSystem::G_NONE) {
        m_image_size.Set(256, 144);
        m_cell_size.Set(272, 160);
    }
    else {
        m_image_size.Set(480, 270);
        m_cell_size.Set(496, 296);
    }
    m_image_size = m_image_size * em_unit(this) / 10;
    m_cell_size = m_cell_size * em_unit(this) / 10;
    UpdateLayout();
}

void ImageGrid::UpdateLayout()
{
    if (!m_file_sys) return;
    wxSize size = GetClientSize();
    int cell_width = m_cell_size.GetWidth();
    int cell_height = m_cell_size.GetHeight();
    int ncol = (size.GetWidth() - cell_width + m_image_size.GetWidth()) / cell_width;
    if (ncol <= 0) ncol = 1;
    int total_height = (m_file_sys->GetCount() + ncol -1) / ncol * cell_height + cell_height - m_image_size.GetHeight();
    int nrow = (total_height - size.GetHeight() + cell_height / 4 - 1) / (cell_height / 4);
    m_row_offset = m_row_offset * m_col_count / ncol;
    m_col_count = ncol;
    m_row_count = nrow > 0 ? nrow + 1 : 0;
    if (m_row_offset >= m_row_count)
        m_row_offset = m_row_count == 0 ? 0 : m_row_count - 1;
    // create mask
    wxSize mask_size{0, 60 * em_unit(this) / 10};
    if (m_file_sys->GetGroupMode() == PrinterFileSystem::G_NONE) {
        mask_size.x = (m_col_count - 1) * m_cell_size.GetWidth() + m_image_size.GetWidth();
    }
    else {
        mask_size.x = m_image_size.x;
    }
    if (!m_mask.IsOk() || m_mask.GetSize() != mask_size) {
        wxImage image(mask_size);
        image.InitAlpha();
        unsigned char *rgb   = image.GetData();
        unsigned char *alpha = image.GetAlpha();
        memset(rgb, 0x6f, mask_size.GetWidth() * mask_size.GetHeight() * 3);
        for (int i = 0; i < mask_size.GetHeight(); ++i) {
            memset(alpha + i * mask_size.GetWidth(), 255 - i * 255 / mask_size.GetHeight(), mask_size.GetWidth());
        }
        m_mask = wxBitmap(std::move(image));
    }
    Refresh();
}

void ImageGrid::mouseMoved(wxMouseEvent& event)
{
}

void ImageGrid::mouseEnterWindow(wxMouseEvent& event)
{
    if (!m_hovered)
    {
        m_hovered = true;
    }
}

void ImageGrid::mouseLeaveWindow(wxMouseEvent& event)
{
    if (m_hovered)
    {
        m_hovered = false;
        m_pressed = false;
    }
}

void ImageGrid::mouseDown(wxMouseEvent& event)
{
    if (!m_pressed)
    {
        m_pressed = true;
    }
}

void ImageGrid::mouseReleased(wxMouseEvent& event)
{
    if (m_pressed)
    {
        m_pressed = false;
        if (!m_file_sys) return;
        if (m_file_sys->GetCount() == 0) return;
        wxSize size = GetClientSize();
        int offx = (size.x - (m_col_count - 1) * m_cell_size.GetWidth() - m_image_size.GetWidth()) / 2;
        int offy = (m_row_offset + 1 < m_row_count || m_row_count == 0)
            ? m_cell_size.GetHeight() - m_image_size.GetHeight() - m_row_offset * m_cell_size.GetHeight() / 4 + m_row_offset / 4 * m_cell_size.GetHeight()
            : size.y - (size.y + m_image_size.GetHeight() - 1) / m_cell_size.GetHeight() * m_cell_size.GetHeight();
        int index = (m_row_offset + 1 < m_row_count || m_row_count == 0) 
            ? m_row_offset / 4 * m_col_count 
            : ((m_file_sys->GetCount() + m_col_count - 1) / m_col_count - (size.y + m_image_size.GetHeight() - 1) / m_cell_size.GetHeight()) * m_col_count;
        offx = event.GetPosition().x - offx;
        offy = event.GetPosition().y - offy;
        int n = 0;
        while (offx > m_cell_size.GetWidth() && n + 1 < m_col_count)
        {
            ++n;
            offx -= m_cell_size.GetWidth();
        }
        if (offx < 0 || offx >= m_image_size.GetWidth()) {
            return;
        }
        index += n;
        while (offy > m_cell_size.GetHeight())
        {
            index += m_col_count;
            offy -= m_cell_size.GetHeight();
        }
        if (index < m_file_sys->GetCount()) {
            Select(index);
        }
    }
}

void ImageGrid::resize(wxSizeEvent& event)
{
    UpdateLayout();
}

void ImageGrid::mouseWheelMoved(wxMouseEvent &event)
{
    auto delta = (event.GetWheelRotation() < 0 == event.IsWheelInverted()) ? -1 : 1;
    int off = m_row_offset + delta;
    if (off >= 0 && off < m_row_count) {
        m_row_offset = off;
        m_timer.StartOnce(4000);
        Refresh();
    }
}

void Slic3r::GUI::ImageGrid::changedEvent(wxCommandEvent& evt)
{
    evt.Skip();
    UpdateFileSystem();
}

void ImageGrid::paintEvent(wxPaintEvent& evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxPaintDC dc(this);
    render(dc);
}

/*
* Here we do the actual rendering. I put it in a separate
* method so that it can work no matter what type of DC
* (e.g. wxPaintDC or wxClientDC) is used.
*/
void ImageGrid::render(wxDC& dc)
{
    if (!m_file_sys) return;
    wxSize size = GetClientSize();
    int offx = (size.x - (m_col_count - 1) * m_cell_size.GetWidth() - m_image_size.GetWidth()) / 2;
    int offy = (m_row_offset + 1 < m_row_count || m_row_count == 0)
        ? m_cell_size.GetHeight() - m_image_size.GetHeight() - m_row_offset * m_cell_size.GetHeight() / 4 + m_row_offset / 4 * m_cell_size.GetHeight()
        : size.y - (size.y + m_image_size.GetHeight() - 1) / m_cell_size.GetHeight() * m_cell_size.GetHeight();
    int index = (m_row_offset + 1 < m_row_count || m_row_count == 0) 
        ? m_row_offset / 4 * m_col_count 
        : ((m_file_sys->GetCount() + m_col_count - 1) / m_col_count - (size.y + m_image_size.GetHeight() - 1) / m_cell_size.GetHeight()) * m_col_count;
    // background: left/right/top side
    dc.SetPen(wxPen(GetBackgroundColour()));
    dc.SetBrush(wxBrush(GetBackgroundColour()));
    dc.DrawRectangle({0, 0, offx, size.y});
    dc.DrawRectangle({size.x - offx - 1, 0, offx + 1, size.y});
    if (offy > 0)
        dc.DrawRectangle({0, 0, size.x, offy});
    constexpr wchar_t const * formats[] = {_T("%Y-%m-%d"), _T("%Y-%m"), _T("%Y")};
    int start = index;
    int end = index;
    while (offy < size.y)
    {
        wxPoint pt{offx, offy};
        end = (index + m_col_count) < m_file_sys->GetCount() ? index + m_col_count : m_file_sys->GetCount();
        while (index < end) {
            auto & file = m_file_sys->GetFile(index);
            if (file.thumbnail.IsOk()) {
                float hs = (float) m_image_size.GetWidth() / file.thumbnail.GetWidth();
                float vs = (float) m_image_size.GetHeight() / file.thumbnail.GetHeight();
                dc.SetUserScale(hs, vs);
                dc.DrawBitmap(file.thumbnail, {(int) (pt.x / hs), (int) (pt.y / vs)});
                dc.SetUserScale(1, 1);
                if (m_file_sys->GetGroupMode() != PrinterFileSystem::G_NONE) {
                    dc.DrawBitmap(m_mask, pt);
                }
                // can' handle alpha
                // dc.GradientFillLinear({pt.x, pt.y, m_image_size.GetWidth(), 60}, wxColour(0x6F, 0x6F, 0x6F, 0x99), wxColour(0x6F, 0x6F, 0x6F, 0), wxBOTTOM);
                if (m_file_sys->GetGroupMode() != PrinterFileSystem::G_NONE) {
                    auto date = wxDateTime((time_t) file.time).Format(_L(formats[m_file_sys->GetGroupMode()]));
                    dc.DrawText(date, pt + wxPoint{24, 16});
                }
                dc.DrawRectangle({pt.x + m_image_size.GetWidth(), pt.y, m_cell_size.GetWidth() - m_image_size.GetWidth(), m_image_size.GetHeight()});
            }
            ++index;
            pt.x += m_cell_size.GetWidth();
        }
        if (end < index + m_col_count)
            dc.DrawRectangle({pt.x, pt.y, size.x - pt.x - offx, m_image_size.GetHeight()});
        dc.DrawRectangle({offx, pt.y + m_image_size.GetHeight(), size.x - offx * 2, m_cell_size.GetHeight() - m_image_size.GetHeight()});
        offy += m_cell_size.GetHeight();
    }
    if (m_file_sys->GetGroupMode() == PrinterFileSystem::G_NONE) {
        dc.DrawBitmap(m_mask, {offx, 0});
        auto & file1 = m_file_sys->GetFile(start);
        auto & file2 = m_file_sys->GetFile(end - 1);
        auto date1 = wxDateTime((time_t) file1.time).Format(_L(formats[m_file_sys->GetGroupMode()]));
        auto date2 = wxDateTime((time_t) file2.time).Format(_L(formats[m_file_sys->GetGroupMode()]));
        dc.DrawText(date1 + " - " + date2, wxPoint{offx + 24, 16});
    }
    if (offy < size.y)
        dc.DrawRectangle({offx, offy, size.x - offx * 2, size.y - offy});
    // draw position bar
    if (m_timer.IsRunning()) {
        int total_height = (m_file_sys->GetCount() + m_col_count - 1) / m_col_count * m_cell_size.GetHeight() + m_cell_size.GetHeight() - m_image_size.GetHeight();
        if (total_height > size.y) {
            int offset = (m_row_offset + 1 < m_row_count || m_row_count == 0) ? m_row_offset * (m_cell_size.GetHeight() / 4) : total_height - size.y;
            wxRect rect = {size.x - 16, offset * size.y / total_height, 8,
                size.y * size.y / total_height};
            dc.SetBrush(wxBrush(*wxLIGHT_GREY));
            dc.DrawRoundedRectangle(rect, 4);
        }
    }
}

}}
